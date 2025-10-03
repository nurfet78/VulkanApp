// engine/rhi/vulkan/resource.h
#pragma once

#include "vulkan_common.h"

namespace Core {
    class CoreContext;
}

namespace RHI::Vulkan {

class Device;
class CommandPool;
class StagingBufferPool;  // Добавлено

// Buffer wrapper
class Buffer {
public:
    Buffer(Device* device, VkDeviceSize size, VkBufferUsageFlags2 usage, VmaMemoryUsage memoryUsage);
    ~Buffer();
    
    void* Map();
    void Unmap();
    void Upload(const void* data, VkDeviceSize size, VkDeviceSize offset = 0);
    void Update(const void* data, VkDeviceSize size, VkDeviceSize offset = 0);
    
	// Изменено: Теперь принимает командный буфер извне
    void CopyFrom(VkCommandBuffer cmd, Buffer* srcBuffer, VkDeviceSize size, 
                  VkDeviceSize srcOffset = 0, VkDeviceSize dstOffset = 0);
    
    // Альтернатива для немедленного копирования с передачей пула
    void CopyFromImmediate(CommandPool* pool, Buffer* srcBuffer, VkDeviceSize size,
                          VkDeviceSize srcOffset = 0, VkDeviceSize dstOffset = 0);
    
    VkBuffer GetHandle() const { return m_buffer; }
    VkDeviceSize GetSize() const { return m_size; }
    VkDeviceAddress GetDeviceAddress() const;
    VkBuffer GetBuffer() const { return m_buffer; }
    
private:
    Device* m_device;
    VkBuffer m_buffer = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    VkDeviceSize m_size;
    void* m_mappedData = nullptr;
};

struct ImageDesc {
	uint32_t width = 1;
	uint32_t height = 1;
	uint32_t depth = 1;
	uint32_t arrayLayers = 1;
	uint32_t mipLevels = 1;
	VkFormat format = VK_FORMAT_UNDEFINED;
	VkImageUsageFlags usage = 0;
	VkImageType imageType = VK_IMAGE_TYPE_2D;
	VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;
	VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
	VkImageAspectFlags aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;
	VmaMemoryUsage memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY;
	VkSharingMode sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	uint32_t queueFamilyIndexCount = 0;
	const uint32_t* pQueueFamilyIndices = nullptr;
	VkImageCreateFlags flags = 0;
};

// Image wrapper
class Image {
public:
    Image(Device* device, const ImageDesc& desc);

    ~Image();
    
    void TransitionLayout(VkImageLayout oldLayout, VkImageLayout newLayout, Core::CoreContext* context);
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
    uint32_t m_arrayLayers;
	VkFormat m_format;

    void TransitionLayout(
        VkCommandBuffer cmd,
        VkImageLayout oldLayout,
        VkImageLayout newLayout,
        uint32_t baseMipLevel,
        uint32_t levelCount,
        uint32_t baseArrayLayer,
        uint32_t layerCount
    );

	static void GetStageAccessForLayout(VkImageLayout layout,
		VkPipelineStageFlags2& stageMask,
		VkAccessFlags2& accessMask) {

		switch (layout) {
		case VK_IMAGE_LAYOUT_UNDEFINED:
			stageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
			accessMask = 0;
			break;

		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
			stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
			accessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
			break;

		case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
			stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
			accessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
			break;

		case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
			stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
			accessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
			break;

		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
			stageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
			accessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			break;

		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
			stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT; // универсально, можно оптимизировать до FRAGMENT_SHADER
			accessMask = VK_ACCESS_2_SHADER_READ_BIT;
			break;

		case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
			stageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
			accessMask = 0;
			break;

		default:
			stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
			accessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
			break;
		}
    }
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

    static std::vector<VkVertexInputBindingDescription> GetVertexBindings() {
        return { {0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX} };
    }

    static std::vector<VkVertexInputAttributeDescription> GetVertexAttributes() {
        return {
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)},
            {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
            {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, texCoord)},
            {3, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, tangent)}
        };
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