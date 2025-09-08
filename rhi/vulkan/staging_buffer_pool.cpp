// engine/rhi/vulkan/staging_buffer_pool.cpp - С ПУЛОМ FENCE'ОВ
#include "staging_buffer_pool.h"
#include "device.h"
#include "resource.h"
#include "command_pool.h"


namespace RHI::Vulkan {

// FencePool implementation
FencePool::FencePool(Device* device) : m_device(device) {
    // Pre-allocate some fences
    const size_t INITIAL_FENCE_COUNT = 8;
    m_availableFences.reserve(INITIAL_FENCE_COUNT);
    
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    
    for (size_t i = 0; i < INITIAL_FENCE_COUNT; ++i) {
        VkFence fence;
        VK_CHECK(vkCreateFence(m_device->GetDevice(), &fenceInfo, nullptr, &fence));
        m_availableFences.push_back(fence);
    }
}

FencePool::~FencePool() noexcept {
    VkDevice device = m_device->GetDevice();
    
    for (VkFence fence : m_availableFences) {
        vkDestroyFence(device, fence, nullptr);
    }
    
    for (VkFence fence : m_inUseFences) {
        // Wait for any pending operations
        vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
        vkDestroyFence(device, fence, nullptr);
    }
}

VkFence FencePool::Acquire() {
    std::lock_guard lock(m_mutex);
    
    VkFence fence;
    
    if (m_availableFences.empty()) {
        // Create new fence
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        VK_CHECK(vkCreateFence(m_device->GetDevice(), &fenceInfo, nullptr, &fence));
    } else {
        // Reuse existing fence
        fence = m_availableFences.back();
        m_availableFences.pop_back();
        
        // Reset the fence
        VK_CHECK(vkResetFences(m_device->GetDevice(), 1, &fence));
    }
    
    m_inUseFences.push_back(fence);
    return fence;
}

void FencePool::Release(VkFence fence) {
    std::lock_guard lock(m_mutex);
    
    auto it = std::find(m_inUseFences.begin(), m_inUseFences.end(), fence);
    if (it != m_inUseFences.end()) {
        m_inUseFences.erase(it);
        m_availableFences.push_back(fence);
    }
}

void FencePool::Reset() {
    std::lock_guard lock(m_mutex);
    
    // Move all in-use fences back to available after waiting
    VkDevice device = m_device->GetDevice();
    
    for (VkFence fence : m_inUseFences) {
        vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
        m_availableFences.push_back(fence);
    }
    
    m_inUseFences.clear();
}

// StagingBufferPool implementation
StagingBufferPool::StagingBufferPool(Device* device) 
    : m_device(device) {
    if (!m_device) {
        throw std::runtime_error("Device cannot be null for StagingBufferPool");
    }
    
    m_fencePool = std::make_unique<FencePool>(device);
}

StagingBufferPool::~StagingBufferPool() noexcept {
    // Process any remaining uploads
    ProcessCompletedUploads();
    
    std::lock_guard lock(m_mutex);
    m_pools.clear();
}

void StagingBufferPool::UploadData(CommandPool* commandPool, Buffer* dstBuffer,
                                   const void* data, size_t size, size_t dstOffset) {
    // Get staging buffer
    Buffer* stagingBuffer = AcquireBuffer(size);
    
    // Upload data to staging
    stagingBuffer->Upload(data, size);
    
    // Copy to destination buffer using provided CommandPool
    VkCommandBuffer cmd = commandPool->AllocateCommandBuffer();
    
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    
    vkBeginCommandBuffer(cmd, &beginInfo);
    dstBuffer->CopyFrom(cmd, stagingBuffer, size, 0, dstOffset);
    vkEndCommandBuffer(cmd);
    
    // Submit and wait with fence from pool
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    
    VkFence fence = m_fencePool->Acquire();
    m_device->SubmitTransfer(&submitInfo, fence);
    vkWaitForFences(m_device->GetDevice(), 1, &fence, VK_TRUE, UINT64_MAX);
    m_fencePool->Release(fence);
    
    commandPool->FreeCommandBuffer(cmd);
    
    // Release staging buffer
    ReleaseBuffer(stagingBuffer);
}

VkFence StagingBufferPool::UploadDataAsync(CommandPool* commandPool, Buffer* dstBuffer,
                                           const void* data, size_t size, size_t dstOffset) {
    // Get staging buffer
    Buffer* stagingBuffer = AcquireBuffer(size);
    
    // Upload data to staging
    stagingBuffer->Upload(data, size);
    
    // Copy to destination buffer
    VkCommandBuffer cmd = commandPool->AllocateCommandBuffer();
    
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    
    vkBeginCommandBuffer(cmd, &beginInfo);
    dstBuffer->CopyFrom(cmd, stagingBuffer, size, 0, dstOffset);
    vkEndCommandBuffer(cmd);
    
    // Submit with fence from pool
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    
    VkFence fence = m_fencePool->Acquire();
    m_device->SubmitTransfer(&submitInfo, fence);
    
    // Track pending upload
    {
        std::lock_guard lock(m_uploadMutex);
        m_pendingUploads.push({
            stagingBuffer,
            dstBuffer,
            size,
            dstOffset,
            fence,
            std::chrono::steady_clock::now()
        });
    }
    
    commandPool->FreeCommandBuffer(cmd);
    
    return fence;
}

void StagingBufferPool::ProcessCompletedUploads() {
    std::lock_guard lock(m_uploadMutex);
    
    VkDevice device = m_device->GetDevice();
    
    while (!m_pendingUploads.empty()) {
        auto& upload = m_pendingUploads.front();
        
        // Check if upload is complete
        VkResult result = vkGetFenceStatus(device, upload.fence);
        
        if (result == VK_SUCCESS) {
            // Upload complete - release resources
            ReleaseBuffer(upload.stagingBuffer);
            m_fencePool->Release(upload.fence);
            m_pendingUploads.pop();
        } else if (result == VK_NOT_READY) {
            // Still pending - stop processing
            break;
        } else {
            // Error occurred
            throw std::runtime_error("Fence check failed");
        }
    }
}

Buffer* StagingBufferPool::AcquireBuffer(size_t size) {
    std::lock_guard lock(m_mutex);
    
    if (!m_device) {
        throw std::runtime_error("StagingBufferPool not initialized");
    }
    
    size = std::max(size, MIN_BUFFER_SIZE);
    size = NextPowerOfTwo(size);
    
    // Find suitable free buffer
    auto it = m_pools.lower_bound(size);
    if (it != m_pools.end()) {
        for (auto& entry : it->second) {
            if (!entry.inUse) {
                entry.inUse = true;  // Под защитой m_mutex
                entry.lastUsed = std::chrono::steady_clock::now();
                return entry.buffer.get();
            }
        }
    }
    
    // Create new buffer
    auto& pool = m_pools[size];
    pool.emplace_back();
    auto& entry = pool.back();
    entry.buffer = std::make_unique<Buffer>(
        m_device, size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU
    );
    entry.inUse = true;
    entry.lastUsed = std::chrono::steady_clock::now();
    
    return entry.buffer.get();
}

void StagingBufferPool::ReleaseBuffer(Buffer* buffer) {
    std::lock_guard lock(m_mutex);
    
    for (auto& [size, buffers] : m_pools) {
        for (auto& entry : buffers) {
            if (entry.buffer.get() == buffer) {
                entry.inUse = false;
                return;
            }
        }
    }
}

void StagingBufferPool::GarbageCollect() {
    // First process completed uploads
    ProcessCompletedUploads();

    std::lock_guard lock(m_mutex);
    auto now = std::chrono::steady_clock::now();
    const auto maxAge = std::chrono::seconds(30);

    for (auto& [size, buffers] : m_pools) {
        // Используем итераторы для удаления без копирования
        for (auto it = buffers.begin(); it != buffers.end();) {
            if (!it->inUse && (now - it->lastUsed) > maxAge) {
                it = buffers.erase(it);  // erase возвращает следующий итератор
            }
            else {
                ++it;
            }
        }
    }
}

size_t StagingBufferPool::NextPowerOfTwo(size_t n) {
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    n |= n >> 32;
    n++;
    return n;
}

} // namespace RHI::Vulkan