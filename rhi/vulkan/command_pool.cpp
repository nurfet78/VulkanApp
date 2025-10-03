// engine/rhi/vulkan/command_pool.cpp - ИСПРАВЛЕННАЯ ВЕРСИЯ
#include "command_pool.h"
#include "device.h"

namespace RHI::Vulkan {

// Anonymous namespace for thread-local storage
namespace {
    thread_local std::unique_ptr<CommandPool> t_graphicsPool;
    thread_local std::unique_ptr<CommandPool> t_computePool;
    thread_local std::unique_ptr<CommandPool> t_transferPool;
    thread_local Device* t_device = nullptr;
}

CommandPool::CommandPool(Device* device, uint32_t queueFamily, VkCommandPoolCreateFlags flags)
    : m_device(device), m_queueFamilyIndex(queueFamily) {
    
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = flags;
    poolInfo.queueFamilyIndex = queueFamily;
    
    VK_CHECK(vkCreateCommandPool(m_device->GetDevice(), &poolInfo, nullptr, &m_pool));

    std::cout << "[CommandPool] Created pool for queue family " << queueFamily << std::endl;
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

CommandPoolManager::CommandPoolManager(Device* device) : m_device(device) {
    m_transferPool = std::make_unique<CommandPool>(
        device, 
        device->GetTransferQueueFamily(),
        VK_COMMAND_POOL_CREATE_TRANSIENT_BIT
    );

    m_graphicsPool = std::make_unique<CommandPool>(
        device,
        device->GetGraphicsQueueFamily(),
        VK_COMMAND_POOL_CREATE_TRANSIENT_BIT
    );

    m_computePool = std::make_unique<CommandPool>(
        device,
        device->GetComputeQueueFamily(),
        VK_COMMAND_POOL_CREATE_TRANSIENT_BIT
    );
}

std::mutex CommandPoolManager::s_cleanupMutex;

CommandPoolManager::~CommandPoolManager() {
    std::lock_guard lock(s_cleanupMutex);
    
    // Clean up thread-local pools if this thread created any
    if (t_device == m_device) {
        t_graphicsPool.reset();
        t_computePool.reset();
        t_transferPool.reset();
        t_device = nullptr;
    }
}

CommandPool* CommandPoolManager::GetGraphicsPool() {
    if (!t_graphicsPool || t_device != m_device) {
        t_graphicsPool = std::make_unique<CommandPool>(
            m_device,
            m_device->GetGraphicsQueueFamily(),
            VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT
        );
        t_device = m_device;
    }
    return t_graphicsPool.get();
}

CommandPool* CommandPoolManager::GetComputePool() {
    if (!t_computePool || t_device != m_device) {
        t_computePool = std::make_unique<CommandPool>(
            m_device,
            m_device->GetComputeQueueFamily(),
            VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT
        );
        t_device = m_device;
    }
    return t_computePool.get();
}

CommandPool* CommandPoolManager::GetTransferPool() {
    // Use the shared transfer pool for single-time commands
    return m_transferPool.get();
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
    
    // Create fence for waiting
    VkFence fence;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VK_CHECK(vkCreateFence(m_device->GetDevice(), &fenceInfo, nullptr, &fence));
    
    // Submit with fence instead of vkQueueWaitIdle
    VK_CHECK(m_device->SubmitTransfer(&submitInfo, fence));
    
    // Wait for specific operation, not entire queue
    VK_CHECK(vkWaitForFences(m_device->GetDevice(), 1, &fence, VK_TRUE, UINT64_MAX));
    
    vkDestroyFence(m_device->GetDevice(), fence, nullptr);
    m_transferPool->FreeCommandBuffer(commandBuffer);
}

VkCommandBuffer CommandPoolManager::BeginSingleTimeCommandsGraphics() {
    VkCommandBuffer cmd = m_graphicsPool->AllocateCommandBuffer();

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(cmd, &beginInfo);
    return cmd;
}

void CommandPoolManager::EndSingleTimeCommandsGraphics(VkCommandBuffer cmd) {
    VK_CHECK(vkEndCommandBuffer(cmd));

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence;
    VK_CHECK(vkCreateFence(m_device->GetDevice(), &fenceInfo, nullptr, &fence));

    // ВАЖНО: отправляем именно в graphics queue
    VK_CHECK(vkQueueSubmit(m_device->GetGraphicsQueue(), 1, &submitInfo, fence));
    VK_CHECK(vkWaitForFences(m_device->GetDevice(), 1, &fence, VK_TRUE, UINT64_MAX));

    vkDestroyFence(m_device->GetDevice(), fence, nullptr);
    m_graphicsPool->FreeCommandBuffer(cmd);
}

VkCommandBuffer CommandPoolManager::BeginSingleTimeCommandsCompute() {
    VkCommandBuffer cmd = m_computePool->AllocateCommandBuffer();

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(cmd, &beginInfo);
    return cmd;
}

void CommandPoolManager::EndSingleTimeCommandsCompute(VkCommandBuffer cmd) {
	VK_CHECK(vkEndCommandBuffer(cmd));

	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &cmd;

	VkFenceCreateInfo fenceInfo{};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	VkFence fence;
	VK_CHECK(vkCreateFence(m_device->GetDevice(), &fenceInfo, nullptr, &fence));

	std::cout << "[Compute] Submitting command buffer from pool family "
		<< m_computePool->GetQueueFamilyIndex()
		<< " to compute queue family "
		<< m_device->GetComputeQueueFamily()
		<< std::endl;

	VkResult res = vkQueueSubmit(m_device->GetComputeQueue(), 1, &submitInfo, fence);

	if (res != VK_SUCCESS) {
		vkDestroyFence(m_device->GetDevice(), fence, nullptr);
		throw std::runtime_error("Vulkan error: vkQueueSubmit failed");
	}

	VkResult waitRes = vkWaitForFences(m_device->GetDevice(), 1, &fence, VK_TRUE, UINT64_MAX);
	std::cout << "vkWaitForFences result: " << waitRes << std::endl;

	vkDestroyFence(m_device->GetDevice(), fence, nullptr);
	m_computePool->FreeCommandBuffer(cmd);
}



} // namespace RHI::Vulkan