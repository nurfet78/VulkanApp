// engine/rhi/vulkan/resource.h
#pragma once

#include "vulkan_common.h"

namespace RHI::Vulkan {

class Device;
class CommandPool;
class StagingBufferPool;  // Добавлено

// Buffer wrapper
class Buffer {
public:
    Buffer(Device* device, VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
    ~Buffer();
    
    void* Map();
    void Unmap();
    void Upload(const void* data, VkDeviceSize size, VkDeviceSize offset = 0);
    
	// Изменено: Теперь принимает командный буфер извне
    void CopyFrom(VkCommandBuffer cmd, Buffer* srcBuffer, VkDeviceSize size, 
                  VkDeviceSize srcOffset = 0, VkDeviceSize dstOffset = 0);
    
    // Альтернатива для немедленного копирования с передачей пула
    void CopyFromImmediate(CommandPool* pool, Buffer* srcBuffer, VkDeviceSize size,
                          VkDeviceSize srcOffset = 0, VkDeviceSize dstOffset = 0);
    
    VkBuffer GetHandle() const { return m_buffer; }
    VkDeviceSize GetSize() const { return m_size; }
    VkDeviceAddress GetDeviceAddress() const;
    
private:
    Device* m_device;
    VkBuffer m_buffer = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    VkDeviceSize m_size;
    void* m_mappedData = nullptr;
};

// Image wrapper
class Image {
public:
    Image(Device* device, uint32_t width, uint32_t height, VkFormat format,
          VkImageUsageFlags usage, VkImageAspectFlags aspectFlags);
    ~Image();
    
    void TransitionLayout(VkCommandBuffer cmd, VkImageLayout oldLayout, VkImageLayout newLayout);
    void CopyFromBuffer(VkCommandBuffer cmd, Buffer* buffer);
    void GenerateMipmaps(VkCommandBuffer cmd);
    
    VkImage GetHandle() const { return m_image; }
    VkImageView GetView() const { return m_imageView; }
    VkFormat GetFormat() const { return m_format; }
    uint32_t GetWidth() const { return m_width; }
    uint32_t GetHeight() const { return m_height; }
    uint32_t GetMipLevels() const { return m_mipLevels; }
    
private:
    Device* m_device;
    VkImage m_image = VK_NULL_HANDLE;
    VkImageView m_imageView = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    
    uint32_t m_width;
    uint32_t m_height;
    uint32_t m_mipLevels;
    VkFormat m_format;
};

// Sampler wrapper
class Sampler {
public:
    Sampler(Device* device, VkFilter filter = VK_FILTER_LINEAR,
            VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            bool enableAnisotropy = true, float maxAnisotropy = 16.0f);
    ~Sampler();
    
    VkSampler GetHandle() const { return m_sampler; }
    
private:
    Device* m_device;
    VkSampler m_sampler = VK_NULL_HANDLE;
};

// Vertex structure
struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;
    glm::vec3 tangent;
    
    static VkVertexInputBindingDescription GetBindingDescription() {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(Vertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return bindingDescription;
    }
    
    static std::vector<VkVertexInputAttributeDescription> GetAttributeDescriptions() {
        std::vector<VkVertexInputAttributeDescription> attributeDescriptions(4);
        
        // Position
        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(Vertex, position);
        
        // Normal
        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(Vertex, normal);
        
        // TexCoord
        attributeDescriptions[2].binding = 0;
        attributeDescriptions[2].location = 2;
        attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[2].offset = offsetof(Vertex, texCoord);
        
        // Tangent
        attributeDescriptions[3].binding = 0;
        attributeDescriptions[3].location = 3;
        attributeDescriptions[3].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[3].offset = offsetof(Vertex, tangent);
        
        return attributeDescriptions;
    }
};

// Mesh data
struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    
    std::unique_ptr<Buffer> vertexBuffer;
    std::unique_ptr<Buffer> indexBuffer;
    
    glm::vec3 boundingMin{0.0f};
    glm::vec3 boundingMax{0.0f};
    float boundingRadius = 0.0f;
    
    void CalculateBounds() {
        if (vertices.empty()) return;
        
        boundingMin = boundingMax = vertices[0].position;
        
        for (const auto& v : vertices) {
            boundingMin = glm::min(boundingMin, v.position);
            boundingMax = glm::max(boundingMax, v.position);
        }
        
        glm::vec3 center = (boundingMin + boundingMax) * 0.5f;
        boundingRadius = 0.0f;
        
        for (const auto& v : vertices) {
            float dist = glm::length(v.position - center);
            boundingRadius = std::max(boundingRadius, dist);
        }
    }
    
    // Изменено: теперь принимает пулы явно
    void UploadToGPU(Device* device, CommandPool* commandPool, 
                     StagingBufferPool* stagingPool);
};

// Material data
struct Material {
    glm::vec4 baseColor{1.0f};
    float metallic = 0.0f;
    float roughness = 0.5f;
    float ao = 1.0f;
    float emissive = 0.0f;
    
    uint32_t albedoTexture = 0;
    uint32_t normalTexture = 0;
    uint32_t metallicRoughnessTexture = 0;
    uint32_t aoTexture = 0;
    uint32_t emissiveTexture = 0;
};

// Resource Manager
class ResourceManager {
public:
    // Изменено: теперь принимает CommandPool
    ResourceManager(Device* device, CommandPool* transferPool);
    ~ResourceManager();
    
    // Mesh management - обновлено
    void CreatePrimitiveMeshes();
    Mesh* CreateMesh(const std::string& name, const std::vector<Vertex>& vertices, 
                    const std::vector<uint32_t>& indices);
    Mesh* GetMesh(const std::string& name);
    
    // Texture management
    Image* LoadTexture(const std::string& path);
    Image* CreateTexture(const std::string& name, uint32_t width, uint32_t height, 
                        VkFormat format, const void* data);
    Image* GetTexture(const std::string& name);
    
    // Material management
    Material* CreateMaterial(const std::string& name);
    Material* GetMaterial(const std::string& name);
    
    // Default resources
    Sampler* GetDefaultSampler() { return m_defaultSampler.get(); }
    Image* GetWhiteTexture() { return GetTexture("white"); }
    Image* GetBlackTexture() { return GetTexture("black"); }
    Image* GetNormalTexture() { return GetTexture("normal"); }
    
    // Доступ к staging pool
    StagingBufferPool* GetStagingPool() { return m_stagingPool.get(); }

private:
    void CreateDefaultTextures();
    Mesh* CreatePlaneMesh();
    Mesh* CreateCubeMesh();
    Mesh* CreateSphereMesh(uint32_t segments = 32);
    Mesh* CreateCylinderMesh(uint32_t segments = 32);
    Mesh* CreateConeMesh(uint32_t segments = 32);
    
    Device* m_device;
    CommandPool* m_transferPool;  // Добавлено
    std::unique_ptr<StagingBufferPool> m_stagingPool;  // Добавлено
    
    std::unordered_map<std::string, std::unique_ptr<Mesh>> m_meshes;
    std::unordered_map<std::string, std::unique_ptr<Image>> m_textures;
    std::unordered_map<std::string, std::unique_ptr<Material>> m_materials;
    
    std::unique_ptr<Sampler> m_defaultSampler;
};

} // namespace RHI::Vulkan