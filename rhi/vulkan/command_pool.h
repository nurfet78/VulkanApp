// engine/rhi/vulkan/command_pool.h
#pragma once

#include "vulkan_common.h"
#include <vector>
#include <mutex>
#include <memory>

namespace RHI::Vulkan {

class Device;

class CommandPool {
public:
    CommandPool(Device* device, uint32_t queueFamily, VkCommandPoolCreateFlags flags = 0);
    ~CommandPool() noexcept;
    
    CommandPool(const CommandPool&) = delete;
    CommandPool& operator=(const CommandPool&) = delete;
    
    VkCommandBuffer AllocateCommandBuffer(VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    std::vector<VkCommandBuffer> AllocateCommandBuffers(uint32_t count, VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    
    void FreeCommandBuffer(VkCommandBuffer buffer);
    void FreeCommandBuffers(const std::vector<VkCommandBuffer>& buffers);
    
    void Reset(VkCommandPoolResetFlags flags = 0);
    
    VkCommandPool GetHandle() const { return m_pool; }

private:
    Device* m_device;
    VkCommandPool m_pool = VK_NULL_HANDLE;
    mutable std::mutex m_mutex;
};

// Thread-safe command pool manager
class CommandPoolManager {
public:
    explicit CommandPoolManager(Device* device);
    ~CommandPoolManager() noexcept;
    
    CommandPoolManager(const CommandPoolManager&) = delete;
    CommandPoolManager& operator=(const CommandPoolManager&) = delete;
    
    CommandPool* GetGraphicsPool();
    CommandPool* GetComputePool();
    CommandPool* GetTransferPool();
    
    VkCommandBuffer BeginSingleTimeCommands();
    void EndSingleTimeCommands(VkCommandBuffer commandBuffer);

private:
    // Thread-safe pools wrapper
    struct ThreadPools {
        std::unique_ptr<CommandPool> graphics;
        std::unique_ptr<CommandPool> compute;
        std::unique_ptr<CommandPool> transfer;
        
        ~ThreadPools() noexcept = default;
    };
    
    Device* m_device;
    std::unique_ptr<CommandPool> m_transferPool;
    
    static thread_local ThreadPools t_pools;
};

} // namespace RHI::Vulkan