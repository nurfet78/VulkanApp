// engine/rhi/vulkan/resource.cpp

// engine/rhi/vulkan/resource.cpp

#include "resource.h"
#include "device.h"
#include "command_pool.h"
#include "staging_buffer_pool.h"
#include "core/core_context.h"


namespace RHI::Vulkan {

// Buffer implementation
Buffer::Buffer(Device* device, VkDeviceSize size, VkBufferUsageFlags2 usage, VmaMemoryUsage memoryUsage)
    : m_device(device), m_size(size) {

    VkBufferUsageFlags2CreateInfo usageInfo{};
    usageInfo.sType = VK_STRUCTURE_TYPE_BUFFER_USAGE_FLAGS_2_CREATE_INFO;
    usageInfo.usage = usage;
    
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = 0;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    bufferInfo.pNext = &usageInfo;
    
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
    if (m_mappedData && m_allocation) {
        // Получаем флаги памяти
        VkMemoryPropertyFlags memFlags;
        vmaGetAllocationMemoryProperties(
            m_device->GetAllocator(),
            m_allocation,
            &memFlags  // Передаем указатель на переменную для результата
        );

        // Проверяем, если память не является HOST_COHERENT, нужно unmapить
        if (!(memFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            vmaUnmapMemory(m_device->GetAllocator(), m_allocation);
            m_mappedData = nullptr;
        }
    }
}

void Buffer::Upload(const void* data, VkDeviceSize size, VkDeviceSize offset) {
    // Получаем флаги памяти
    VkMemoryPropertyFlags memFlags;
    vmaGetAllocationMemoryProperties(m_device->GetAllocator(), m_allocation, &memFlags);

    if (memFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        // Мапим 
        void* mapped = Map();
        memcpy(static_cast<char*>(mapped) + offset, data, size);

        // Flush для non-coherent памяти
        if (!(memFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            VK_CHECK(vmaFlushAllocation(m_device->GetAllocator(), m_allocation, offset, size));
            Unmap(); // размапиваем non-coherent память
        }
        // Для host-coherent память остаётся замапленной
    }
    else {
        // Для device-local буферов без host-visible нельзя напрямую писать
        throw std::runtime_error("Buffer is not host-visible, use staging buffer for upload");
    }
}

void Buffer::Update(const void* data, VkDeviceSize size, VkDeviceSize offset) {
    Upload(data, size, offset);
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

void Buffer::CopyFromImmediate(CommandPool* pool, Buffer* srcBuffer, VkDeviceSize size,
                              VkDeviceSize srcOffset, VkDeviceSize dstOffset) {
    VkCommandBuffer cmd = pool->AllocateCommandBuffer();
    
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    
    vkBeginCommandBuffer(cmd, &beginInfo);
    CopyFrom(cmd, srcBuffer, size, srcOffset, dstOffset);
    vkEndCommandBuffer(cmd);
    
    // RAII fence wrapper
    struct FenceGuard {
        VkDevice device;
        VkFence fence;
        ~FenceGuard() { 
            if (fence) vkDestroyFence(device, fence, nullptr); 
        }
    };
    
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    
    VkFence fence;
    VK_CHECK(vkCreateFence(m_device->GetDevice(), &fenceInfo, nullptr, &fence));
    FenceGuard guard{m_device->GetDevice(), fence};
    
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    
    VK_CHECK(m_device->SubmitTransfer(&submitInfo, fence));
    VK_CHECK(vkWaitForFences(m_device->GetDevice(), 1, &fence, VK_TRUE, UINT64_MAX));
    
    pool->FreeCommandBuffer(cmd);
}

// Image implementation
Image::Image(Device* device,
    uint32_t width,
    uint32_t height,
    uint32_t depth,
    uint32_t arrayLayers,
    VkFormat format,
    VkImageUsageFlags usage,
    VkImageType imageType,
    VkImageTiling tiling,
    VkImageLayout initialLayout,
    VkSampleCountFlagBits samples,
    VkImageAspectFlags aspectFlags,
    VmaMemoryUsage memoryUsage,
    VkSharingMode sharingMode,
    uint32_t queueFamilyIndexCount,
    const uint32_t* pQueueFamilyIndices,
    VkImageCreateFlags flags)
    : m_device(device),
    m_format(format)
{
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = imageType;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = (imageType == VK_IMAGE_TYPE_3D ? depth : 1);
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = (imageType == VK_IMAGE_TYPE_3D ? 1 : arrayLayers);
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = initialLayout;
    imageInfo.usage = usage;
    imageInfo.samples = samples;
    imageInfo.sharingMode = sharingMode;
    imageInfo.queueFamilyIndexCount = queueFamilyIndexCount;
    imageInfo.pQueueFamilyIndices = pQueueFamilyIndices;
    imageInfo.flags = flags;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = memoryUsage;

    if (vmaCreateImage(m_device->GetAllocator(), &imageInfo, &allocInfo, &m_image, &m_allocation, nullptr) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan image");
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_image;
    viewInfo.viewType = (imageType == VK_IMAGE_TYPE_2D ? VK_IMAGE_VIEW_TYPE_2D :
        imageType == VK_IMAGE_TYPE_3D ? VK_IMAGE_VIEW_TYPE_3D :
        VK_IMAGE_VIEW_TYPE_2D);
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = (imageType == VK_IMAGE_TYPE_3D ? 1 : arrayLayers);

    if (vkCreateImageView(m_device->GetDevice(), &viewInfo, nullptr, &m_imageView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create image view");
    }
}

// Короткий конструктор для 2D изображений
Image::Image(Device* device,
    uint32_t width,
    uint32_t height,
    VkFormat format,
    VkImageUsageFlags usage,
    VkImageAspectFlags aspectFlags)
    : Image(device,
        width,
        height,
        1,                      // depth всегда 1 для 2D
        1,                      // arrayLayers по умолчанию 1
        format,
        usage,
        VK_IMAGE_TYPE_2D,       // imageType
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_SAMPLE_COUNT_1_BIT,
        aspectFlags,
        VMA_MEMORY_USAGE_GPU_ONLY,
        VK_SHARING_MODE_EXCLUSIVE,
        0,                      // queueFamilyIndexCount
        nullptr,                // pQueueFamilyIndices
        0)                      // flags
{
    // Это просто вызов полного конструктора с типовыми параметрами
}


Image::~Image() {
    if (m_imageView) {
        vkDestroyImageView(m_device->GetDevice(), m_imageView, nullptr);
    }
    if (m_image) {
        vmaDestroyImage(m_device->GetAllocator(), m_image, m_allocation);
    }
}

void Image::TransitionLayout(
    VkCommandBuffer cmd,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    uint32_t baseMipLevel,
    uint32_t levelCount,
    uint32_t baseArrayLayer,
    uint32_t layerCount
) {
    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_image;
    barrier.subresourceRange.baseMipLevel = baseMipLevel;
    barrier.subresourceRange.levelCount = levelCount;
    barrier.subresourceRange.baseArrayLayer = baseArrayLayer;
    barrier.subresourceRange.layerCount = layerCount;

    // --- Определение aspectMask ---
    VkFormat format = m_format;
    barrier.subresourceRange.aspectMask = 0;

    switch (format) {
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
    case VK_FORMAT_D24_UNORM_S8_UINT:
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        break;

    case VK_FORMAT_D16_UNORM:
    case VK_FORMAT_X8_D24_UNORM_PACK32:
    case VK_FORMAT_D32_SFLOAT:
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        break;

    default:
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        break;
    }

    // --- Хелпер для выбора стадий/доступа ---
    auto getStageAccess = [](VkImageLayout layout, VkPipelineStageFlags2& stageMask, VkAccessFlags2& accessMask) {
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
            stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            accessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            break;

        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            accessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
            break;

        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            // Универсальный вариант — безопасен и для graphics, и для transfer
            stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
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
    };

    // --- Установка src/dst масок ---
    getStageAccess(oldLayout, barrier.srcStageMask, barrier.srcAccessMask);
    getStageAccess(newLayout, barrier.dstStageMask, barrier.dstAccessMask);

    // --- Вызов ---
    VkDependencyInfo depInfo{};
    depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    depInfo.imageMemoryBarrierCount = 1;
    depInfo.pImageMemoryBarriers = &barrier;

    vkCmdPipelineBarrier2(cmd, &depInfo);
}

void Image::TransitionLayout(VkImageLayout oldLayout, VkImageLayout newLayout, Core::CoreContext* context) {

    VkCommandBuffer cmd = context->GetCommandPoolManager()->BeginSingleTimeCommands();

    TransitionLayout(cmd, oldLayout, newLayout, 0, 1, 0, 1);

    context->GetCommandPoolManager()->EndSingleTimeCommands(cmd);
}

void Image::GenerateMipmaps(VkCommandBuffer cmd) {
    // Проверим, поддерживает ли формат блиты
    VkFormatProperties formatProperties;
    vkGetPhysicalDeviceFormatProperties(m_device->GetPhysicalDevice(), m_format, &formatProperties);

    if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
        throw std::runtime_error("Image format does not support linear blitting for mipmaps!");
    }

    int32_t mipWidth = m_width;
    int32_t mipHeight = m_height;

    for (uint32_t i = 1; i < m_mipLevels; i++) {
        // mip[i - 1] → TRANSFER_SRC
        TransitionLayout(cmd,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            i - 1, 1, 0, 1);

        // mip[i] → TRANSFER_DST
        TransitionLayout(cmd,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            i, 1, 0, 1);

        // --- Blit ---
        VkImageBlit blit{};
        blit.srcOffsets[0] = { 0, 0, 0 };
        blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;

        blit.dstOffsets[0] = { 0, 0, 0 };
        blit.dstOffsets[1] = {
            std::max(1, mipWidth / 2),
            std::max(1, mipHeight / 2),
            1
        };
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;

        vkCmdBlitImage(cmd,
            m_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blit, VK_FILTER_LINEAR);

        // уменьшаем размеры для следующего уровня
        mipWidth = std::max(1, mipWidth / 2);
        mipHeight = std::max(1, mipHeight / 2);
    }

    // Все уровни переводим в SHADER_READ_ONLY_OPTIMAL
    TransitionLayout(cmd,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        0, m_mipLevels, 0, 1);
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
        VK_BUFFER_USAGE_2_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY
    );
    
    indexBuffer = std::make_unique<Buffer>(
        device, indexBufferSize,
        VK_BUFFER_USAGE_2_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT,
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
    //CreateCylinderMesh();
    //CreateConeMesh();
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
    // 24 вершины — 4 на каждую грань, с нормалями и UV
    Vertex cubeVertices[24] = {
        // Front (Z-)
        {{-0.5f,-0.5f,-0.5f}, {0,0,-1}, {0,0}, {}},
        {{ 0.5f,-0.5f,-0.5f}, {0,0,-1}, {1,0}, {}},
        {{ 0.5f, 0.5f,-0.5f}, {0,0,-1}, {1,1}, {}},
        {{-0.5f, 0.5f,-0.5f}, {0,0,-1}, {0,1}, {}},

        // Back (Z+)
        {{ 0.5f,-0.5f, 0.5f}, {0,0,1}, {0,0}, {}},
        {{-0.5f,-0.5f, 0.5f}, {0,0,1}, {1,0}, {}},
        {{-0.5f, 0.5f, 0.5f}, {0,0,1}, {1,1}, {}},
        {{ 0.5f, 0.5f, 0.5f}, {0,0,1}, {0,1}, {}},

        // Left (X-)
        {{-0.5f,-0.5f, 0.5f}, {-1,0,0}, {0,0}, {}},
        {{-0.5f,-0.5f,-0.5f}, {-1,0,0}, {1,0}, {}},
        {{-0.5f, 0.5f,-0.5f}, {-1,0,0}, {1,1}, {}},
        {{-0.5f, 0.5f, 0.5f}, {-1,0,0}, {0,1}, {}},

        // Right (X+)
        {{ 0.5f,-0.5f,-0.5f}, {1,0,0}, {0,0}, {}},
        {{ 0.5f,-0.5f, 0.5f}, {1,0,0}, {1,0}, {}},
        {{ 0.5f, 0.5f, 0.5f}, {1,0,0}, {1,1}, {}},
        {{ 0.5f, 0.5f,-0.5f}, {1,0,0}, {0,1}, {}},

        // Top (Y+)
        {{-0.5f, 0.5f,-0.5f}, {0,1,0}, {0,0}, {}},
        {{ 0.5f, 0.5f,-0.5f}, {0,1,0}, {1,0}, {}},
        {{ 0.5f, 0.5f, 0.5f}, {0,1,0}, {1,1}, {}},
        {{-0.5f, 0.5f, 0.5f}, {0,1,0}, {0,1}, {}},

        // Bottom (Y-)
        {{-0.5f,-0.5f, 0.5f}, {0,-1,0}, {0,0}, {}},
        {{ 0.5f,-0.5f, 0.5f}, {0,-1,0}, {1,0}, {}},
        {{ 0.5f,-0.5f,-0.5f}, {0,-1,0}, {1,1}, {}},
        {{-0.5f,-0.5f,-0.5f}, {0,-1,0}, {0,1}, {}},
    };

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    for (int i = 0; i < 24; i += 4) {
        // Вычисляем тангент для грани
        glm::vec3 edge1 = cubeVertices[i + 1].position - cubeVertices[i].position;
        glm::vec3 edge2 = cubeVertices[i + 3].position - cubeVertices[i].position;
        glm::vec2 deltaUV1 = cubeVertices[i + 1].texCoord - cubeVertices[i].texCoord;
        glm::vec2 deltaUV2 = cubeVertices[i + 3].texCoord - cubeVertices[i].texCoord;

        float r = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV1.y * deltaUV2.x);
        glm::vec3 tangent = (edge1 * deltaUV2.y - edge2 * deltaUV1.y) * r;
        tangent = glm::normalize(tangent);

        for (int v = 0; v < 4; ++v) {
            cubeVertices[i + v].tangent = tangent;
            vertices.push_back(cubeVertices[i + v]);
        }

        // Два треугольника на грань
        indices.push_back(i + 0);
        indices.push_back(i + 1);
        indices.push_back(i + 2);
        indices.push_back(i + 0);
        indices.push_back(i + 2);
        indices.push_back(i + 3);
    }

    return CreateMesh("cube", vertices, indices);
}

Mesh* ResourceManager::CreateSphereMesh(uint32_t segments) {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    
    for (uint32_t lat = 0; lat <= segments; lat++) {
        float theta = lat * static_cast<float>(std::numbers::pi) / segments;
        float sinTheta = sin(theta);
        float cosTheta = cos(theta);
        
        for (uint32_t lon = 0; lon <= segments; lon++) {
            float phi = lon * 2 * static_cast<float>(std::numbers::pi) / segments;
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