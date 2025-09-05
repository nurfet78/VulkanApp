// engine/rhi/vulkan/staging_buffer_pool.h
#pragma once

#include "vulkan_common.h"
#include <memory>
#include <map>
#include <vector>
#include <mutex>
#include <chrono>
#include <atomic>

namespace RHI::Vulkan {

class Device;
class Buffer;
class CommandPool;

// Теперь НЕ синглтон - управляется явно через Device или ResourceManager
class StagingBufferPool {
public:
    static constexpr size_t MIN_BUFFER_SIZE = 64 * 1024;        // 64KB
    static constexpr size_t MAX_BUFFER_SIZE = 256 * 1024 * 1024; // 256MB
    
    // Явный конструктор/деструктор вместо синглтона
    explicit StagingBufferPool(Device* device);
    ~StagingBufferPool() noexcept;
    
    // Запрещаем копирование
    StagingBufferPool(const StagingBufferPool&) = delete;
    StagingBufferPool& operator=(const StagingBufferPool&) = delete;
    
    // Методы остаются те же
    Buffer* AcquireBuffer(size_t size);
    void ReleaseBuffer(Buffer* buffer);
    void GarbageCollect();
    
    // Новый метод для загрузки с использованием внешнего CommandPool
    void UploadData(CommandPool* commandPool, Buffer* dstBuffer, 
                    const void* data, size_t size, size_t dstOffset = 0);

private:
    struct BufferEntry {
        std::unique_ptr<Buffer> buffer;
        std::atomic<bool> inUse{false};
        std::chrono::steady_clock::time_point lastUsed;
    };
    
    Device* m_device;
    std::map<size_t, std::vector<BufferEntry>> m_pools;
    mutable std::mutex m_mutex;
    
    size_t NextPowerOfTwo(size_t n);
};

} // namespace RHI::Vulkan