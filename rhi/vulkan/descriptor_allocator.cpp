// engine/rhi/vulkan/descriptor_allocator.cpp
#include "descriptor_allocator.h"
#include "device.h"

namespace RHI::Vulkan {

// DescriptorAllocator implementation
DescriptorAllocator::DescriptorAllocator(Device* device) : m_device(device) {
}

DescriptorAllocator::~DescriptorAllocator() {
    // Cleanup all pools
    for (auto pool : m_freePools) {
        vkDestroyDescriptorPool(m_device->GetDevice(), pool, nullptr);
    }
    for (auto pool : m_usedPools) {
        vkDestroyDescriptorPool(m_device->GetDevice(), pool, nullptr);
    }
}

void DescriptorAllocator::ResetPools() {
    for (auto pool : m_usedPools) {
        vkResetDescriptorPool(m_device->GetDevice(), pool, 0);
        m_freePools.push_back(pool);
    }
    
    m_usedPools.clear();
    m_currentPool = VK_NULL_HANDLE;
}

bool DescriptorAllocator::Allocate(VkDescriptorSet* set, VkDescriptorSetLayout layout) {
    if (!m_currentPool) {
        m_currentPool = GrabPool();
        m_usedPools.push_back(m_currentPool);
    }
    
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_currentPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;
    
    VkResult result = vkAllocateDescriptorSets(m_device->GetDevice(), &allocInfo, set);
    
    switch (result) {
        case VK_SUCCESS:
            return true;
            
        case VK_ERROR_FRAGMENTED_POOL:
        case VK_ERROR_OUT_OF_POOL_MEMORY:
            // Need new pool
            m_currentPool = GrabPool();
            m_usedPools.push_back(m_currentPool);
            
            allocInfo.descriptorPool = m_currentPool;
            result = vkAllocateDescriptorSets(m_device->GetDevice(), &allocInfo, set);
            
            if (result == VK_SUCCESS) {
                return true;
            }
            break;
            
        default:
            break;
    }
    
    return false;
}

VkDescriptorPool DescriptorAllocator::CreatePool() {
    std::vector<VkDescriptorPoolSize> sizes = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    };
    
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = 0;
    poolInfo.maxSets = 1000;
    poolInfo.poolSizeCount = static_cast<uint32_t>(sizes.size());
    poolInfo.pPoolSizes = sizes.data();
    
    VkDescriptorPool pool;
    VK_CHECK(vkCreateDescriptorPool(m_device->GetDevice(), &poolInfo, nullptr, &pool));
    
    return pool;
}

VkDescriptorPool DescriptorAllocator::GrabPool() {
    if (!m_freePools.empty()) {
        VkDescriptorPool pool = m_freePools.back();
        m_freePools.pop_back();
        return pool;
    } else {
        return CreatePool();
    }
}

// DescriptorLayoutCache implementation
DescriptorLayoutCache::DescriptorLayoutCache(Device* device) : m_device(device) {
}

DescriptorLayoutCache::~DescriptorLayoutCache() {
    for (auto& pair : m_layoutCache) {
        vkDestroyDescriptorSetLayout(m_device->GetDevice(), pair.second, nullptr);
    }
}

VkDescriptorSetLayout DescriptorLayoutCache::CreateDescriptorLayout(
    const VkDescriptorSetLayoutCreateInfo* info) {
    
    LayoutInfo layoutInfo;
    layoutInfo.bindings.reserve(info->bindingCount);
    
    bool isSorted = true;
    int32_t lastBinding = -1;
    
    for (uint32_t i = 0; i < info->bindingCount; i++) {
        layoutInfo.bindings.push_back(info->pBindings[i]);
        
        if (static_cast<int32_t>(info->pBindings[i].binding) > lastBinding) {
            lastBinding = info->pBindings[i].binding;
        } else {
            isSorted = false;
        }
    }
    
    if (!isSorted) {
        std::sort(layoutInfo.bindings.begin(), layoutInfo.bindings.end(),
            [](const VkDescriptorSetLayoutBinding& a, const VkDescriptorSetLayoutBinding& b) {
                return a.binding < b.binding;
            });
    }
    
    auto it = m_layoutCache.find(layoutInfo);
    if (it != m_layoutCache.end()) {
        return it->second;
    }
    
    VkDescriptorSetLayout layout;
    VK_CHECK(vkCreateDescriptorSetLayout(m_device->GetDevice(), info, nullptr, &layout));
    
    m_layoutCache[layoutInfo] = layout;
    return layout;
}

bool DescriptorLayoutCache::LayoutInfo::operator==(const LayoutInfo& other) const {
    if (bindings.size() != other.bindings.size()) {
        return false;
    }
    
    for (size_t i = 0; i < bindings.size(); i++) {
        if (bindings[i].binding != other.bindings[i].binding) return false;
        if (bindings[i].descriptorType != other.bindings[i].descriptorType) return false;
        if (bindings[i].descriptorCount != other.bindings[i].descriptorCount) return false;
        if (bindings[i].stageFlags != other.bindings[i].stageFlags) return false;
    }
    
    return true;
}

size_t DescriptorLayoutCache::LayoutInfo::hash() const {
    size_t result = std::hash<size_t>()(bindings.size());
    
    for (const auto& b : bindings) {
        size_t bindingHash = b.binding | (b.descriptorType << 8) | 
                            (b.descriptorCount << 16) | (b.stageFlags << 24);
        
        result ^= std::hash<size_t>()(bindingHash);
    }
    
    return result;
}

// DescriptorBuilder implementation
DescriptorBuilder DescriptorBuilder::Begin(DescriptorLayoutCache* layoutCache,
                                          DescriptorAllocator* allocator) {
    DescriptorBuilder builder;
    builder.m_cache = layoutCache;
    builder.m_alloc = allocator;
    return builder;
}

DescriptorBuilder& DescriptorBuilder::BindBuffer(uint32_t binding,
                                                 VkDescriptorBufferInfo* bufferInfo,
                                                 VkDescriptorType type,
                                                 VkShaderStageFlags stageFlags) {
    VkDescriptorSetLayoutBinding newBinding{};
    newBinding.binding = binding;
    newBinding.descriptorCount = 1;
    newBinding.descriptorType = type;
    newBinding.stageFlags = stageFlags;
    
    m_bindings.push_back(newBinding);
    
    VkWriteDescriptorSet newWrite{};
    newWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    newWrite.descriptorCount = 1;
    newWrite.descriptorType = type;
    newWrite.pBufferInfo = bufferInfo;
    newWrite.dstBinding = binding;
    
    m_writes.push_back(newWrite);
    
    return *this;
}

DescriptorBuilder& DescriptorBuilder::BindImage(uint32_t binding,
                                               VkDescriptorImageInfo* imageInfo,
                                               VkDescriptorType type,
                                               VkShaderStageFlags stageFlags) {
    VkDescriptorSetLayoutBinding newBinding{};
    newBinding.binding = binding;
    newBinding.descriptorCount = 1;
    newBinding.descriptorType = type;
    newBinding.stageFlags = stageFlags;
    
    m_bindings.push_back(newBinding);
    
    VkWriteDescriptorSet newWrite{};
    newWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    newWrite.descriptorCount = 1;
    newWrite.descriptorType = type;
    newWrite.pImageInfo = imageInfo;
    newWrite.dstBinding = binding;
    
    m_writes.push_back(newWrite);
    
    return *this;
}

bool DescriptorBuilder::Build(VkDescriptorSet& set, VkDescriptorSetLayout& layout) {
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(m_bindings.size());
    layoutInfo.pBindings = m_bindings.data();
    
    layout = m_cache->CreateDescriptorLayout(&layoutInfo);
    
    if (!m_alloc->Allocate(&set, layout)) {
        return false;
    }
    
    for (auto& w : m_writes) {
        w.dstSet = set;
    }
    
    vkUpdateDescriptorSets(m_cache->m_device->GetDevice(),
                          static_cast<uint32_t>(m_writes.size()),
                          m_writes.data(), 0, nullptr);
    
    return true;
}

bool DescriptorBuilder::Build(VkDescriptorSet& set) {
    VkDescriptorSetLayout layout;
    return Build(set, layout);
}

// BindlessDescriptorManager implementation
BindlessDescriptorManager::BindlessDescriptorManager(Device* device) : m_device(device) {
    m_textureSlots.resize(MAX_BINDLESS_TEXTURES);
    
    // Initialize free indices
    m_freeIndices.reserve(MAX_BINDLESS_TEXTURES);
    for (uint32_t i = 0; i < MAX_BINDLESS_TEXTURES; ++i) {
        m_freeIndices.push_back(MAX_BINDLESS_TEXTURES - 1 - i);
    }
    
    // Create descriptor pool
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = MAX_BINDLESS_TEXTURES;
    
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    
    VK_CHECK(vkCreateDescriptorPool(m_device->GetDevice(), &poolInfo, nullptr, &m_pool));
    
    // Create descriptor layout with bindless flag
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = BINDLESS_TEXTURE_BINDING;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = MAX_BINDLESS_TEXTURES;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    VkDescriptorBindingFlags bindingFlags = 
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
        VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;
    
    VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo{};
    bindingFlagsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    bindingFlagsInfo.bindingCount = 1;
    bindingFlagsInfo.pBindingFlags = &bindingFlags;
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.pNext = &bindingFlagsInfo;
    layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    
    VK_CHECK(vkCreateDescriptorSetLayout(m_device->GetDevice(), &layoutInfo, nullptr, &m_layout));
    
    // Allocate descriptor set
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_pool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_layout;
    
    VK_CHECK(vkAllocateDescriptorSets(m_device->GetDevice(), &allocInfo, &m_descriptorSet));
}

BindlessDescriptorManager::~BindlessDescriptorManager() {
    if (m_layout) {
        vkDestroyDescriptorSetLayout(m_device->GetDevice(), m_layout, nullptr);
    }
    if (m_pool) {
        vkDestroyDescriptorPool(m_device->GetDevice(), m_pool, nullptr);
    }
}

uint32_t BindlessDescriptorManager::RegisterTexture(VkImageView imageView, VkSampler sampler) {
    if (m_freeIndices.empty()) {
        return UINT32_MAX; // No free slots
    }
    
    uint32_t index = m_freeIndices.back();
    m_freeIndices.pop_back();
    
    m_textureSlots[index].imageView = imageView;
    m_textureSlots[index].sampler = sampler;
    m_textureSlots[index].inUse = true;
    
    m_dirtyIndices.push_back(index);
    
    return index;
}

void BindlessDescriptorManager::UnregisterTexture(uint32_t index) {
    if (index >= MAX_BINDLESS_TEXTURES || !m_textureSlots[index].inUse) {
        return;
    }
    
    m_textureSlots[index].inUse = false;
    m_freeIndices.push_back(index);
}

void BindlessDescriptorManager::UpdateDescriptorSet() {
    if (m_dirtyIndices.empty()) {
        return;
    }
    
    std::vector<VkWriteDescriptorSet> writes;
    std::vector<VkDescriptorImageInfo> imageInfos;
    
    writes.reserve(m_dirtyIndices.size());
    imageInfos.reserve(m_dirtyIndices.size());
    
    for (uint32_t index : m_dirtyIndices) {
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = m_textureSlots[index].imageView;
        imageInfo.sampler = m_textureSlots[index].sampler;
        imageInfos.push_back(imageInfo);
        
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_descriptorSet;
        write.dstBinding = BINDLESS_TEXTURE_BINDING;
        write.dstArrayElement = index;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfos.back();
        writes.push_back(write);
    }
    
    vkUpdateDescriptorSets(m_device->GetDevice(),
                          static_cast<uint32_t>(writes.size()),
                          writes.data(), 0, nullptr);
    
    m_dirtyIndices.clear();
}

} // namespace RHI::Vulkan