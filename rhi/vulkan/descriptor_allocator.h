// engine/rhi/vulkan/descriptor_allocator.h
#pragma once

#include "vulkan_common.h"

namespace RHI::Vulkan {

class Device;

// Descriptor pool allocator
class DescriptorAllocator {
public:
    DescriptorAllocator(Device* device);
    ~DescriptorAllocator();
    
    // Reset all pools for new frame
    void ResetPools();
    
    // Allocate descriptor set
    bool Allocate(VkDescriptorSet* set, VkDescriptorSetLayout layout);
    
private:
    Device* m_device;
    
    VkDescriptorPool m_currentPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorPool> m_usedPools;
    std::vector<VkDescriptorPool> m_freePools;
    
    VkDescriptorPool CreatePool();
    VkDescriptorPool GrabPool();
};

// Descriptor layout cache
class DescriptorLayoutCache {
public:
    DescriptorLayoutCache(Device* device);
    ~DescriptorLayoutCache();
    
    VkDescriptorSetLayout CreateDescriptorLayout(const VkDescriptorSetLayoutCreateInfo* info);
    
    struct LayoutInfo {
        std::vector<VkDescriptorSetLayoutBinding> bindings;
        
        bool operator==(const LayoutInfo& other) const;
        size_t hash() const;
    };
    
private:
    struct LayoutHash {
        size_t operator()(const LayoutInfo& k) const {
            return k.hash();
        }
    };
    
    Device* m_device;
    std::unordered_map<LayoutInfo, VkDescriptorSetLayout, LayoutHash> m_layoutCache;
};

// Descriptor builder for easy descriptor set creation
class DescriptorBuilder {
public:
    static DescriptorBuilder Begin(DescriptorLayoutCache* layoutCache, DescriptorAllocator* allocator);
    
    DescriptorBuilder& BindBuffer(uint32_t binding, VkDescriptorBufferInfo* bufferInfo,
                                  VkDescriptorType type, VkShaderStageFlags stageFlags);
    DescriptorBuilder& BindImage(uint32_t binding, VkDescriptorImageInfo* imageInfo,
                                 VkDescriptorType type, VkShaderStageFlags stageFlags);
    DescriptorBuilder& BindBufferArray(uint32_t binding, uint32_t count,
                                       VkDescriptorBufferInfo* bufferInfo,
                                       VkDescriptorType type, VkShaderStageFlags stageFlags);
    DescriptorBuilder& BindImageArray(uint32_t binding, uint32_t count,
                                      VkDescriptorImageInfo* imageInfo,
                                      VkDescriptorType type, VkShaderStageFlags stageFlags);
    
    bool Build(VkDescriptorSet& set, VkDescriptorSetLayout& layout);
    bool Build(VkDescriptorSet& set);
    
private:
    std::vector<VkWriteDescriptorSet> m_writes;
    std::vector<VkDescriptorSetLayoutBinding> m_bindings;
    
    DescriptorLayoutCache* m_cache;
    DescriptorAllocator* m_alloc;
};

// Bindless descriptor manager for textures
class BindlessDescriptorManager {
public:
    static constexpr uint32_t MAX_BINDLESS_TEXTURES = 16384;
    static constexpr uint32_t BINDLESS_TEXTURE_BINDING = 0;
    
    BindlessDescriptorManager(Device* device);
    ~BindlessDescriptorManager();
    
    // Register texture and get its index
    uint32_t RegisterTexture(VkImageView imageView, VkSampler sampler);
    void UnregisterTexture(uint32_t index);
    
    // Update descriptor set with new textures
    void UpdateDescriptorSet();
    
    VkDescriptorSet GetDescriptorSet() const { return m_descriptorSet; }
    VkDescriptorSetLayout GetLayout() const { return m_layout; }
    
private:
    Device* m_device;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_layout = VK_NULL_HANDLE;
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;
    
    struct TextureSlot {
        VkImageView imageView = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        bool inUse = false;
    };
    
    std::vector<TextureSlot> m_textureSlots;
    std::vector<uint32_t> m_freeIndices;
    std::vector<uint32_t> m_dirtyIndices;
};

}