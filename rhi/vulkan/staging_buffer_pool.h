// engine/rhi/vulkan/staging_buffer_pool.h
#pragma once

#include "vulkan_common.h"
#include "resource.h"
#include <memory>
#include <map>
#include <vector>
#include <mutex>
#include <chrono>
#include <atomic>

namespace RHI::Vulkan {

class Device;
class Buffer;

class StagingBufferPool {
public:
    static constexpr size_t MIN_BUFFER_SIZE = 64 * 1024;        // 64KB
    static constexpr size_t MAX_BUFFER_SIZE = 256 * 1024 * 1024; // 256MB
    
    static StagingBufferPool& Get() {
        static StagingBufferPool instance;
        return instance;
    }
    
    void Initialize(Device* device) { m_device = device; }
    void Shutdown();
    
    Buffer* AcquireBuffer(size_t size);
    void ReleaseBuffer(Buffer* buffer);
    void GarbageCollect(); // Cleanup unused buffers

private:
    StagingBufferPool() = default;
    ~StagingBufferPool() noexcept { Shutdown(); }
    
    StagingBufferPool(const StagingBufferPool&) = delete;
    StagingBufferPool& operator=(const StagingBufferPool&) = delete;
    
    struct BufferEntry {
        std::unique_ptr<Buffer> buffer;
        std::atomic<bool> inUse{false};
        std::chrono::steady_clock::time_point lastUsed;
    };
    
    Device* m_device = nullptr;
    std::map<size_t, std::vector<BufferEntry>> m_pools;
    mutable std::mutex m_mutex;
    
    size_t NextPowerOfTwo(size_t n);
};

} // namespace RHI::Vulkan