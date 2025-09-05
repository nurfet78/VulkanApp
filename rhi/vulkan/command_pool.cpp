// engine/rhi/vulkan/command_pool.cpp
#include "command_pool.h"
#include "device.h"

namespace RHI::Vulkan {

CommandPool::CommandPool(Device* device, uint32_t queueFamily, VkCommandPoolCreateFlags flags)
    : m_device(device) {
    
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = flags;
    poolInfo.queueFamilyIndex = queueFamily;
    
    VK_CHECK(vkCreateCommandPool(m_device->GetDevice(), &poolInfo, nullptr, &m_pool));
}

CommandPool::~CommandPool() {
    if (m_pool) {
        vkDestroyCommandPool(m_device->GetDevice(), m_pool, nullptr);
    }
}

VkCommandBuffer CommandPool::AllocateCommandBuffer(VkCommandBufferLevel level) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_pool;
    allocInfo.level = level;
    allocInfo.commandBufferCount = 1;
    
    VkCommandBuffer buffer;
    VK_CHECK(vkAllocateCommandBuffers(m_device->GetDevice(), &allocInfo, &buffer));
    
    return buffer;
}

std::vector<VkCommandBuffer> CommandPool::AllocateCommandBuffers(uint32_t count, VkCommandBufferLevel level) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::vector<VkCommandBuffer> buffers(count);
    
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_pool;
    allocInfo.level = level;
    allocInfo.commandBufferCount = count;
    
    VK_CHECK(vkAllocateCommandBuffers(m_device->GetDevice(), &allocInfo, buffers.data()));
    
    return buffers;
}

void CommandPool::FreeCommandBuffer(VkCommandBuffer buffer) {
    std::lock_guard<std::mutex> lock(m_mutex);
    vkFreeCommandBuffers(m_device->GetDevice(), m_pool, 1, &buffer);
}

void CommandPool::FreeCommandBuffers(const std::vector<VkCommandBuffer>& buffers) {
    std::lock_guard<std::mutex> lock(m_mutex);
    vkFreeCommandBuffers(m_device->GetDevice(), m_pool, static_cast<uint32_t>(buffers.size()), buffers.data());
}

void CommandPool::Reset(VkCommandPoolResetFlags flags) {
    std::lock_guard<std::mutex> lock(m_mutex);
    VK_CHECK(vkResetCommandPool(m_device->GetDevice(), m_pool, flags));
}

// Thread-local storage
thread_local std::unique_ptr<CommandPool> CommandPoolManager::t_graphicsPool;
thread_local std::unique_ptr<CommandPool> CommandPoolManager::t_computePool;
thread_local std::unique_ptr<CommandPool> CommandPoolManager::t_transferPool;

CommandPoolManager::CommandPoolManager(Device* device) : m_device(device) {
    m_transferPool = std::make_unique<CommandPool>(
        device, 
        device->GetTransferQueueFamily(),
        VK_COMMAND_POOL_CREATE_TRANSIENT_BIT
    );
}

CommandPoolManager::~CommandPoolManager() = default;

CommandPool* CommandPoolManager::GetGraphicsPool() {
    if (!t_graphicsPool) {
        t_graphicsPool = std::make_unique<CommandPool>(
            m_device,
            m_device->GetGraphicsQueueFamily(),
            VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT
        );
    }
    return t_graphicsPool.get();
}

CommandPool* CommandPoolManager::GetComputePool() {
    if (!t_computePool) {
        t_computePool = std::make_unique<CommandPool>(
            m_device,
            m_device->GetComputeQueueFamily(),
            VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT
        );
    }
    return t_computePool.get();
}

CommandPool* CommandPoolManager::GetTransferPool() {
    if (!t_transferPool) {
        t_transferPool = std::make_unique<CommandPool>(
            m_device,
            m_device->GetTransferQueueFamily(),
            VK_COMMAND_POOL_CREATE_TRANSIENT_BIT
        );
    }
    return t_transferPool.get();
}

VkCommandBuffer CommandPoolManager::BeginSingleTimeCommands() {
    VkCommandBuffer commandBuffer = m_transferPool->AllocateCommandBuffer();
    
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    
    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    
    return commandBuffer;
}

void CommandPoolManager::EndSingleTimeCommands(VkCommandBuffer commandBuffer) {
    VK_CHECK(vkEndCommandBuffer(commandBuffer));
    
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    
    // Создаем fence для ожидания
    VkFence fence;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VK_CHECK(vkCreateFence(m_device->GetDevice(), &fenceInfo, nullptr, &fence));
    
    // Submit с fence вместо vkQueueWaitIdle
    VK_CHECK(m_device->SubmitTransfer(&submitInfo, fence));
    
    // Ждем конкретную операцию, а не весь queue
    VK_CHECK(vkWaitForFences(m_device->GetDevice(), 1, &fence, VK_TRUE, UINT64_MAX));
    
    vkDestroyFence(m_device->GetDevice(), fence, nullptr);
    m_transferPool->FreeCommandBuffer(commandBuffer);
}

} // namespace RHI::Vulkan