// engine/rhi/vulkan/resource.cpp
#include "resource.h"
#include "device.h"
#include "command_pool.h"
#include <cstring>
#include <cmath>

namespace RHI::Vulkan {

// Buffer implementation
Buffer::Buffer(Device* device, VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage)
    : m_device(device), m_size(size) {
    
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = memoryUsage;
    
    if (memoryUsage == VMA_MEMORY_USAGE_CPU_TO_GPU || memoryUsage == VMA_MEMORY_USAGE_CPU_ONLY) {
        allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }
    
    VmaAllocationInfo allocationInfo;
    VK_CHECK(vmaCreateBuffer(m_device->GetAllocator(), &bufferInfo, &allocInfo, 
                            &m_buffer, &m_allocation, &allocationInfo));
    
    if (allocInfo.flags & VMA_ALLOCATION_CREATE_MAPPED_BIT) {
        m_mappedData = allocationInfo.pMappedData;
    }
}

Buffer::~Buffer() {
    if (m_buffer) {
        vmaDestroyBuffer(m_device->GetAllocator(), m_buffer, m_allocation);
    }
}

void* Buffer::Map() {
    if (!m_mappedData) {
        VK_CHECK(vmaMapMemory(m_device->GetAllocator(), m_allocation, &m_mappedData));
    }
    return m_mappedData;
}

void Buffer::Unmap() {
    if (m_mappedData && !(m_allocation && vmaGetAllocationMemoryProperties(m_device->GetAllocator(), m_allocation))) {
        vmaUnmapMemory(m_device->GetAllocator(), m_allocation);
        m_mappedData = nullptr;
    }
}

void Buffer::Upload(const void* data, VkDeviceSize size, VkDeviceSize offset) {
    void* mapped = Map();
    memcpy(static_cast<char*>(mapped) + offset, data, size);
    
    // Flush if not coherent
    VK_CHECK(vmaFlushAllocation(m_device->GetAllocator(), m_allocation, offset, size));
}

VkDeviceAddress Buffer::GetDeviceAddress() const {
    VkBufferDeviceAddressInfo addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addressInfo.buffer = m_buffer;
    return vkGetBufferDeviceAddress(m_device->GetDevice(), &addressInfo);
}

// Добавить этот метод в существующий класс Buffer в resource.cpp

// Buffer::CopyFrom - теперь принимает командный буфер
void Buffer::CopyFrom(VkCommandBuffer cmd, Buffer* srcBuffer, VkDeviceSize size,
                     VkDeviceSize srcOffset, VkDeviceSize dstOffset) {
    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = srcOffset;
    copyRegion.dstOffset = dstOffset;
    copyRegion.size = size;
    vkCmdCopyBuffer(cmd, srcBuffer->m_buffer, m_buffer, 1, &copyRegion);
}

// Buffer::CopyFromImmediate - для совместимости, принимает CommandPool
void Buffer::CopyFromImmediate(CommandPool* pool, Buffer* srcBuffer, VkDeviceSize size,
                              VkDeviceSize srcOffset, VkDeviceSize dstOffset) {
    // Получаем командный буфер из переданного пула
    VkCommandBuffer cmd = pool->AllocateCommandBuffer();
    
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    
    vkBeginCommandBuffer(cmd, &beginInfo);
    CopyFrom(cmd, srcBuffer, size, srcOffset, dstOffset);
    vkEndCommandBuffer(cmd);
    
    // Submit через device (нужно будет передать fence)
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    
    VkFence fence;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    vkCreateFence(m_device->GetDevice(), &fenceInfo, nullptr, &fence);
    
    m_device->SubmitTransfer(&submitInfo, fence);
    vkWaitForFences(m_device->GetDevice(), 1, &fence, VK_TRUE, UINT64_MAX);
    
    vkDestroyFence(m_device->GetDevice(), fence, nullptr);
    pool->FreeCommandBuffer(cmd);
}

// Image implementation
Image::Image(Device* device, uint32_t width, uint32_t height, VkFormat format,
             VkImageUsageFlags usage, VkImageAspectFlags aspectFlags)
    : m_device(device), m_width(width), m_height(height), m_format(format) {
    
    // Calculate mip levels for color textures
    if (usage & VK_IMAGE_USAGE_SAMPLED_BIT && aspectFlags == VK_IMAGE_ASPECT_COLOR_BIT) {
        m_mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;
    } else {
        m_mipLevels = 1;
    }
    
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = m_mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    
    VK_CHECK(vmaCreateImage(m_device->GetAllocator(), &imageInfo, &allocInfo,
                           &m_image, &m_allocation, nullptr));
    
    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = m_mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    
    VK_CHECK(vkCreateImageView(m_device->GetDevice(), &viewInfo, nullptr, &m_imageView));
}

Image::~Image() {
    if (m_imageView) {
        vkDestroyImageView(m_device->GetDevice(), m_imageView, nullptr);
    }
    if (m_image) {
        vmaDestroyImage(m_device->GetAllocator(), m_image, m_allocation);
    }
}

void Image::TransitionLayout(VkCommandBuffer cmd, VkImageLayout oldLayout, VkImageLayout newLayout) {
    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = m_mipLevels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    
    // Set stage and access masks based on layouts
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        barrier.srcAccessMask = 0;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    }
    
    VkDependencyInfo depInfo{};
    depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    depInfo.imageMemoryBarrierCount = 1;
    depInfo.pImageMemoryBarriers = &barrier;
    
    vkCmdPipelineBarrier2(cmd, &depInfo);
}

// Sampler implementation
Sampler::Sampler(Device* device, VkFilter filter, VkSamplerAddressMode addressMode,
                 bool enableAnisotropy, float maxAnisotropy)
    : m_device(device) {
    
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = filter;
    samplerInfo.minFilter = filter;
    samplerInfo.addressModeU = addressMode;
    samplerInfo.addressModeV = addressMode;
    samplerInfo.addressModeW = addressMode;
    samplerInfo.anisotropyEnable = enableAnisotropy ? VK_TRUE : VK_FALSE;
    samplerInfo.maxAnisotropy = enableAnisotropy ? maxAnisotropy : 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
    samplerInfo.mipLodBias = 0.0f;
    
    VK_CHECK(vkCreateSampler(m_device->GetDevice(), &samplerInfo, nullptr, &m_sampler));
}

Sampler::~Sampler() {
    if (m_sampler) {
        vkDestroySampler(m_device->GetDevice(), m_sampler, nullptr);
    }
}

// Mesh implementation
void Mesh::UploadToGPU(Device* device, CommandPool* commandPool, 
                       StagingBufferPool* stagingPool) {
    VkDeviceSize vertexBufferSize = sizeof(Vertex) * vertices.size();
    VkDeviceSize indexBufferSize = sizeof(uint32_t) * indices.size();
    
    // Create GPU buffers
    vertexBuffer = std::make_unique<Buffer>(
        device, vertexBufferSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY
    );
    
    indexBuffer = std::make_unique<Buffer>(
        device, indexBufferSize,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY
    );
    
    // Используем переданный staging pool
    stagingPool->UploadData(commandPool, vertexBuffer.get(), 
                            vertices.data(), vertexBufferSize);
    stagingPool->UploadData(commandPool, indexBuffer.get(), 
                            indices.data(), indexBufferSize);
}

// ResourceManager implementation
ResourceManager::ResourceManager(Device* device, CommandPool* transferPool) 
    : m_device(device), m_transferPool(transferPool) {
    
    // Создаем свой экземпляр staging pool
    m_stagingPool = std::make_unique<StagingBufferPool>(device);
    
    m_defaultSampler = std::make_unique<Sampler>(device);
    CreateDefaultTextures();
    CreatePrimitiveMeshes();
}

ResourceManager::~ResourceManager() = default;

void ResourceManager::CreatePrimitiveMeshes() {
    CreatePlaneMesh();
    CreateCubeMesh();
    CreateSphereMesh();
    CreateCylinderMesh();
    CreateConeMesh();
}

// ResourceManager::CreateMesh обновлен
Mesh* ResourceManager::CreateMesh(const std::string& name, 
                                  const std::vector<Vertex>& vertices, 
                                  const std::vector<uint32_t>& indices) {
    auto mesh = std::make_unique<Mesh>();
    mesh->vertices = vertices;
    mesh->indices = indices;
    mesh->CalculateBounds();
    
    // Используем наши пулы
    mesh->UploadToGPU(m_device, m_transferPool, m_stagingPool.get());
    
    Mesh* ptr = mesh.get();
    m_meshes[name] = std::move(mesh);
    return ptr;
}

Mesh* ResourceManager::GetMesh(const std::string& name) {
    auto it = m_meshes.find(name);
    return it != m_meshes.end() ? it->second.get() : nullptr;
}

Material* ResourceManager::CreateMaterial(const std::string& name) {
    auto material = std::make_unique<Material>();
    Material* ptr = material.get();
    m_materials[name] = std::move(material);
    return ptr;
}

Material* ResourceManager::GetMaterial(const std::string& name) {
    auto it = m_materials.find(name);
    return it != m_materials.end() ? it->second.get() : nullptr;
}

void ResourceManager::CreateDefaultTextures() {
    // White texture
    uint32_t white = 0xFFFFFFFF;
    CreateTexture("white", 1, 1, VK_FORMAT_R8G8B8A8_UNORM, &white);
    
    // Black texture
    uint32_t black = 0xFF000000;
    CreateTexture("black", 1, 1, VK_FORMAT_R8G8B8A8_UNORM, &black);
    
    // Default normal map (pointing up)
    uint32_t normal = 0xFFFF8080; // RGB = (0.5, 0.5, 1.0)
    CreateTexture("normal", 1, 1, VK_FORMAT_R8G8B8A8_UNORM, &normal);
}

Image* ResourceManager::CreateTexture(const std::string& name, uint32_t width, uint32_t height,
                                     VkFormat format, const void* data) {
    auto image = std::make_unique<Image>(
        m_device, width, height, format,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT
    );
    
    // Upload data if provided
    if (data) {
        // TODO: Implement texture upload with staging buffer
    }
    
    Image* ptr = image.get();
    m_textures[name] = std::move(image);
    return ptr;
}

Image* ResourceManager::GetTexture(const std::string& name) {
    auto it = m_textures.find(name);
    return it != m_textures.end() ? it->second.get() : nullptr;
}

Mesh* ResourceManager::CreatePlaneMesh() {
    std::vector<Vertex> vertices = {
        {{-1, 0, -1}, {0, 1, 0}, {0, 0}, {1, 0, 0}},
        {{ 1, 0, -1}, {0, 1, 0}, {1, 0}, {1, 0, 0}},
        {{ 1, 0,  1}, {0, 1, 0}, {1, 1}, {1, 0, 0}},
        {{-1, 0,  1}, {0, 1, 0}, {0, 1}, {1, 0, 0}}
    };
    
    std::vector<uint32_t> indices = {
        0, 1, 2,
        0, 2, 3
    };
    
    return CreateMesh("plane", vertices, indices);
}

Mesh* ResourceManager::CreateCubeMesh() {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    
    // Create cube vertices (24 vertices for proper normals)
    float positions[8][3] = {
        {-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f},
        {0.5f, 0.5f, -0.5f}, {-0.5f, 0.5f, -0.5f},
        {-0.5f, -0.5f, 0.5f}, {0.5f, -0.5f, 0.5f},
        {0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}
    };
    
    // Face definitions
    int faces[6][4] = {
        {0, 1, 2, 3}, // Front
        {5, 4, 7, 6}, // Back
        {4, 0, 3, 7}, // Left
        {1, 5, 6, 2}, // Right
        {3, 2, 6, 7}, // Top
        {4, 5, 1, 0}  // Bottom
    };
    
    glm::vec3 normals[6] = {
        {0, 0, -1}, {0, 0, 1}, {-1, 0, 0},
        {1, 0, 0}, {0, 1, 0}, {0, -1, 0}
    };
    
    for (int f = 0; f < 6; f++) {
        uint32_t baseIndex = vertices.size();
        
        for (int v = 0; v < 4; v++) {
            Vertex vertex;
            vertex.position = glm::vec3(
                positions[faces[f][v]][0],
                positions[faces[f][v]][1],
                positions[faces[f][v]][2]
            );
            vertex.normal = normals[f];
            vertex.texCoord = glm::vec2((v & 1) ? 1.0f : 0.0f, (v & 2) ? 1.0f : 0.0f);
            vertex.tangent = glm::vec3(1, 0, 0); // Simplified
            vertices.push_back(vertex);
        }
        
        indices.push_back(baseIndex + 0);
        indices.push_back(baseIndex + 1);
        indices.push_back(baseIndex + 2);
        indices.push_back(baseIndex + 0);
        indices.push_back(baseIndex + 2);
        indices.push_back(baseIndex + 3);
    }
    
    return CreateMesh("cube", vertices, indices);
}

Mesh* ResourceManager::CreateSphereMesh(uint32_t segments) {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    
    for (uint32_t lat = 0; lat <= segments; lat++) {
        float theta = lat * M_PI / segments;
        float sinTheta = sin(theta);
        float cosTheta = cos(theta);
        
        for (uint32_t lon = 0; lon <= segments; lon++) {
            float phi = lon * 2 * M_PI / segments;
            float sinPhi = sin(phi);
            float cosPhi = cos(phi);
            
            Vertex vertex;
            vertex.normal = glm::vec3(cosPhi * sinTheta, cosTheta, sinPhi * sinTheta);
            vertex.position = vertex.normal * 0.5f;
            vertex.texCoord = glm::vec2((float)lon / segments, (float)lat / segments);
            vertex.tangent = glm::vec3(-sinPhi, 0, cosPhi);
            
            vertices.push_back(vertex);
        }
    }
    
    for (uint32_t lat = 0; lat < segments; lat++) {
        for (uint32_t lon = 0; lon < segments; lon++) {
            uint32_t first = lat * (segments + 1) + lon;
            uint32_t second = first + segments + 1;
            
            indices.push_back(first);
            indices.push_back(second);
            indices.push_back(first + 1);
            
            indices.push_back(second);
            indices.push_back(second + 1);
            indices.push_back(first + 1);
        }
    }
    
    return CreateMesh("sphere", vertices, indices);
}

Mesh* ResourceManager::CreateCylinderMesh(uint32_t segments) {
    // Simplified cylinder creation
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    
    // TODO: Implement cylinder mesh generation
    
    return CreateMesh("cylinder", vertices, indices);
}

Mesh* ResourceManager::CreateConeMesh(uint32_t segments) {
    // Simplified cone creation
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    
    // TODO: Implement cone mesh generation
    
    return CreateMesh("cone", vertices, indices);
}

} // namespace RHI::Vulkan