// engine/rhi/vulkan/async_resource_loader.h
#pragma once

#include "vulkan_common.h"
#include <memory>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <atomic>

namespace RHI::Vulkan {

class Device;
class Buffer;
class Image;
class CommandPool;
class StagingBufferPool;
class ResourceManager;

// Resource load request types
enum class ResourceType {
    Buffer,
    Texture,
    Mesh,
    Material
};

// Base load request
struct LoadRequest {
    ResourceType type;
    std::string name;
    std::string path;
    std::promise<void*> promise;
    std::function<void()> callback;
    uint32_t priority = 0;
    
    bool operator<(const LoadRequest& other) const {
        return priority < other.priority;
    }
};

// Async Resource Loader
class AsyncResourceLoader {
public:
    AsyncResourceLoader(Device* device, ResourceManager* resourceManager);
    ~AsyncResourceLoader();
    
    // Start/stop loader threads
    void Start(uint32_t numThreads = 2);
    void Stop();
    
    // Queue resource for loading
    std::future<void*> LoadTexture(const std::string& name, const std::string& path, 
                                   uint32_t priority = 0);
    std::future<void*> LoadMesh(const std::string& name, const std::string& path,
                               uint32_t priority = 0);
    
    // Queue with callback
    void LoadTextureAsync(const std::string& name, const std::string& path,
                         std::function<void(Image*)> callback, uint32_t priority = 0);
    void LoadMeshAsync(const std::string& name, const std::string& path,
                      std::function<void(void*)> callback, uint32_t priority = 0);
    
    // Process GPU uploads (call from main thread)
    void ProcessGPUUploads(CommandPool* commandPool);
    
    // Get loading progress
    float GetProgress() const {
        return m_totalRequests > 0 ? 
               static_cast<float>(m_completedRequests) / m_totalRequests : 1.0f;
    }
    
    bool IsLoading() const { return m_loading.load(); }
    
private:
    void LoaderThread();
    void ProcessLoadRequest(const LoadRequest& request);
    
    // CPU loading functions
    struct TextureData {
        std::vector<uint8_t> pixels;
        uint32_t width;
        uint32_t height;
        VkFormat format;
    };
    
    struct MeshData {
        std::vector<uint8_t> vertices;
        std::vector<uint32_t> indices;
        size_t vertexSize;
    };
    
    TextureData LoadTextureFromFile(const std::string& path);
    MeshData LoadMeshFromFile(const std::string& path);
    
    Device* m_device;
    ResourceManager* m_resourceManager;
    
    // Thread management
    std::vector<std::thread> m_loaderThreads;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_loading{false};
    
    // Request queue
    std::priority_queue<LoadRequest> m_loadQueue;
    std::mutex m_queueMutex;
    std::condition_variable m_queueCV;
    
    // GPU upload queue
    struct GPUUpload {
        enum Type { TextureUpload, MeshUpload } type;
        std::unique_ptr<TextureData> textureData;
        std::unique_ptr<MeshData> meshData;
        std::string name;
        std::function<void(void*)> callback;
    };
    
    std::queue<GPUUpload> m_gpuUploadQueue;
    std::mutex m_uploadMutex;
    
    // Statistics
    std::atomic<uint32_t> m_totalRequests{0};
    std::atomic<uint32_t> m_completedRequests{0};
};