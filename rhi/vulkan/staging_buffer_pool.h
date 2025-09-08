// engine/rhi/vulkan/staging_buffer_pool.h - С ПУЛОМ FENCE'ОВ
#pragma once

#include "vulkan_common.h"

namespace RHI::Vulkan {

class Device;
class Buffer;
class CommandPool;

// Fence pool for efficient synchronization
class FencePool {
public:
    explicit FencePool(Device* device);
    ~FencePool() noexcept;
    
    VkFence Acquire();
    void Release(VkFence fence);
    void Reset();
    
private:
    Device* m_device;
    std::vector<VkFence> m_availableFences;
    std::vector<VkFence> m_inUseFences;
    mutable std::mutex m_mutex;
};

// Async upload manager
struct UploadRequest {
    Buffer* stagingBuffer;
    Buffer* dstBuffer;
    size_t size;
    size_t dstOffset;
    VkFence fence;
    std::chrono::steady_clock::time_point submitTime;
};

class StagingBufferPool {
public:
    static constexpr size_t MIN_BUFFER_SIZE = 64 * 1024;        // 64KB
    static constexpr size_t MAX_BUFFER_SIZE = 256 * 1024 * 1024; // 256MB
    
    explicit StagingBufferPool(Device* device);
    ~StagingBufferPool() noexcept;
    
    StagingBufferPool(const StagingBufferPool&) = delete;
    StagingBufferPool& operator=(const StagingBufferPool&) = delete;
    
    // Synchronous upload (immediate)
    void UploadData(CommandPool* commandPool, Buffer* dstBuffer, 
                    const void* data, size_t size, size_t dstOffset = 0);
    
    // Asynchronous upload (returns fence for tracking)
    VkFence UploadDataAsync(CommandPool* commandPool, Buffer* dstBuffer,
                           const void* data, size_t size, size_t dstOffset = 0);
    
    // Process completed uploads
    void ProcessCompletedUploads();
    
    // Garbage collection
    void GarbageCollect();
    
    Buffer* AcquireBuffer(size_t size);
    void ReleaseBuffer(Buffer* buffer);

private:
    struct BufferEntry {
        std::unique_ptr<Buffer> buffer;
        std::atomic<bool> inUse{false};
        std::chrono::steady_clock::time_point lastUsed;
    };
    
    Device* m_device;
    std::unique_ptr<FencePool> m_fencePool;
    std::map<size_t, std::vector<BufferEntry>> m_pools;
    std::queue<UploadRequest> m_pendingUploads;
    mutable std::mutex m_mutex;
    mutable std::mutex m_uploadMutex;
    
    size_t NextPowerOfTwo(size_t n);
};