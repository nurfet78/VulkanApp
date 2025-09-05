// engine/rhi/vulkan/staging_buffer_pool.cpp
#include "staging_buffer_pool.h"
#include "device.h"
#include "resource.h"
#include <algorithm>

namespace RHI::Vulkan {

void StagingBufferPool::Shutdown() {
    std::lock_guard lock(m_mutex);
    m_pools.clear();
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
            if (!entry.inUse.exchange(true)) {
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
    std::lock_guard lock(m_mutex);
    auto now = std::chrono::steady_clock::now();
    const auto maxAge = std::chrono::seconds(30);
    
    for (auto& [size, buffers] : m_pools) {
        buffers.erase(
            std::remove_if(buffers.begin(), buffers.end(),
                [&](const BufferEntry& entry) {
                    return !entry.inUse && 
                           (now - entry.lastUsed) > maxAge;
                }),
            buffers.end()
        );
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