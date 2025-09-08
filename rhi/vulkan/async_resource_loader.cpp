// engine/rhi/vulkan/async_resource_loader.cpp
#include "async_resource_loader.h"
#include "device.h"
#include "resource.h"
#include "staging_buffer_pool.h"
#include <stb_image.h>  // Assume we have stb_image for texture loading

namespace RHI::Vulkan {

AsyncResourceLoader::AsyncResourceLoader(Device* device, ResourceManager* resourceManager)
    : m_device(device), m_resourceManager(resourceManager) {
}

AsyncResourceLoader::~AsyncResourceLoader() {
    Stop();
}

void AsyncResourceLoader::Start(uint32_t numThreads) {
    if (m_running) return;
    
    m_running = true;
    
    for (uint32_t i = 0; i < numThreads; ++i) {
        m_loaderThreads.emplace_back(&AsyncResourceLoader::LoaderThread, this);
    }
}

void AsyncResourceLoader::Stop() {
    if (!m_running) return;
    
    // Signal threads to stop
    {
        std::lock_guard lock(m_queueMutex);
        m_running = false;
    }
    m_queueCV.notify_all();
    
    // Wait for threads to finish
    for (auto& thread : m_loaderThreads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    m_loaderThreads.clear();
}

std::future<void*> AsyncResourceLoader::LoadTexture(const std::string& name, 
                                                    const std::string& path,
                                                    uint32_t priority) {
    LoadRequest request;
    request.type = ResourceType::Texture;
    request.name = name;
    request.path = path;
    request.priority = priority;
    
    auto future = request.promise.get_future();
    
    {
        std::lock_guard lock(m_queueMutex);
        m_loadQueue.push(std::move(request));
        m_totalRequests++;
    }
    
    m_queueCV.notify_one();
    return future;
}

std::future<void*> AsyncResourceLoader::LoadMesh(const std::string& name,
                                                 const std::string& path,
                                                 uint32_t priority) {
    LoadRequest request;
    request.type = ResourceType::Mesh;
    request.name = name;
    request.path = path;
    request.priority = priority;
    
    auto future = request.promise.get_future();
    
    {
        std::lock_guard lock(m_queueMutex);
        m_loadQueue.push(std::move(request));
        m_totalRequests++;
    }
    
    m_queueCV.notify_one();
    return future;
}

void AsyncResourceLoader::LoadTextureAsync(const std::string& name,
                                          const std::string& path,
                                          std::function<void(Image*)> callback,
                                          uint32_t priority) {
    LoadRequest request;
    request.type = ResourceType::Texture;
    request.name = name;
    request.path = path;
    request.priority = priority;
    request.callback = [callback, ptr = &request.promise]() {
        if (callback) {
            void* result = ptr->get_future().get();
            callback(static_cast<Image*>(result));
        }
    };
    
    {
        std::lock_guard lock(m_queueMutex);
        m_loadQueue.push(std::move(request));
        m_totalRequests++;
    }
    
    m_queueCV.notify_one();
}

void AsyncResourceLoader::LoadMeshAsync(const std::string& name,
                                       const std::string& path,
                                       std::function<void(void*)> callback,
                                       uint32_t priority) {
    LoadRequest request;
    request.type = ResourceType::Mesh;
    request.name = name;
    request.path = path;
    request.priority = priority;
    request.callback = callback;
    
    {
        std::lock_guard lock(m_queueMutex);
        m_loadQueue.push(std::move(request));
        m_totalRequests++;
    }
    
    m_queueCV.notify_one();
}

void AsyncResourceLoader::LoaderThread() {
    while (m_running) {
        LoadRequest request;
        
        // Get next request
        {
            std::unique_lock lock(m_queueMutex);
            
            m_queueCV.wait(lock, [this] {
                return !m_running || !m_loadQueue.empty();
            });
            
            if (!m_running) break;
            
            if (!m_loadQueue.empty()) {
                request = std::move(const_cast<LoadRequest&>(m_loadQueue.top()));
                m_loadQueue.pop();
                m_loading = !m_loadQueue.empty();
            }
        }
        
        // Process request
        if (!request.name.empty()) {
            ProcessLoadRequest(request);
        }
    }
}

void AsyncResourceLoader::ProcessLoadRequest(const LoadRequest& request) {
    try {
        switch (request.type) {
            case ResourceType::Texture: {
                // Load texture data from file (CPU side)
                auto textureData = std::make_unique<TextureData>(
                    LoadTextureFromFile(request.path)
                );
                
                // Queue for GPU upload
                {
                    std::lock_guard lock(m_uploadMutex);
                    GPUUpload upload;
                    upload.type = GPUUpload::TextureUpload;
                    upload.textureData = std::move(textureData);
                    upload.name = request.name;
                    upload.callback = [promise = const_cast<std::promise<void*>*>(&request.promise),
                                      cb = request.callback](void* result) {
                        promise->set_value(result);
                        if (cb) cb();
                    };
                    m_gpuUploadQueue.push(std::move(upload));
                }
                break;
            }
            
            case ResourceType::Mesh: {
                // Load mesh data from file (CPU side)
                auto meshData = std::make_unique<MeshData>(
                    LoadMeshFromFile(request.path)
                );
                
                // Queue for GPU upload
                {
                    std::lock_guard lock(m_uploadMutex);
                    GPUUpload upload;
                    upload.type = GPUUpload::MeshUpload;
                    upload.meshData = std::move(meshData);
                    upload.name = request.name;
                    upload.callback = [promise = const_cast<std::promise<void*>*>(&request.promise),
                                      cb = request.callback](void* result) {
                        promise->set_value(result);
                        if (cb) cb();
                    };
                    m_gpuUploadQueue.push(std::move(upload));
                }
                break;
            }
            
            default:
                const_cast<std::promise<void*>&>(request.promise).set_value(nullptr);
                if (request.callback) request.callback();
                break;
        }
        
        m_completedRequests++;
        
    } catch (const std::exception& e) {
        // Set exception on promise
        const_cast<std::promise<void*>&>(request.promise).set_exception(
            std::make_exception_ptr(std::runtime_error(e.what()))
        );
        m_completedRequests++;
    }
}

void AsyncResourceLoader::ProcessGPUUploads(CommandPool* commandPool) {
    std::lock_guard lock(m_uploadMutex);
    
    const size_t MAX_UPLOADS_PER_FRAME = 5;
    size_t uploadsProcessed = 0;
    
    while (!m_gpuUploadQueue.empty() && uploadsProcessed < MAX_UPLOADS_PER_FRAME) {
        auto upload = std::move(m_gpuUploadQueue.front());
        m_gpuUploadQueue.pop();
        
        void* result = nullptr;
        
        switch (upload.type) {
            case GPUUpload::TextureUpload: {
                // Create GPU texture and upload data
                auto* image = m_resourceManager->CreateTexture(
                    upload.name,
                    upload.textureData->width,
                    upload.textureData->height,
                    upload.textureData->format,
                    upload.textureData->pixels.data()
                );
                result = image;
                break;
            }
            
            case GPUUpload::MeshUpload: {
                // Create GPU mesh and upload data
                // This would need proper vertex/index parsing
                // For now, simplified version
                result = nullptr;  // Would return actual mesh
                break;
            }
        }
        
        if (upload.callback) {
            upload.callback(result);
        }
        
        uploadsProcessed++;
    }
}

AsyncResourceLoader::TextureData AsyncResourceLoader::LoadTextureFromFile(const std::string& path) {
    TextureData data;
    
    // Use stb_image to load texture
    int width, height, channels;
    stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
    
    if (!pixels) {
        throw std::runtime_error("Failed to load texture: " + path);
    }
    
    data.width = static_cast<uint32_t>(width);
    data.height = static_cast<uint32_t>(height);
    data.format = VK_FORMAT_R8G8B8A8_UNORM;
    
    size_t imageSize = width * height * 4;
    data.pixels.resize(imageSize);
    memcpy(data.pixels.data(), pixels, imageSize);
    
    stbi_image_free(pixels);
    
    return data;
}

AsyncResourceLoader::MeshData AsyncResourceLoader::LoadMeshFromFile(const std::string& path) {
    MeshData data;
    
    // This would load mesh data from file (e.g., OBJ, GLTF)
    // For now, returning empty data
    
    return data;
}

} // namespace RHI::Vulkan