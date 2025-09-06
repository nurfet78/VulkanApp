// engine/rhi/vulkan/command_buffer_optimizer.cpp
#include "command_buffer_optimizer.h"
#include "device.h"
#include "command_pool.h"
#include <thread>
#include <execution>

namespace RHI::Vulkan {

// CommandBufferRecorder implementation
CommandBufferRecorder::CommandBufferRecorder(Device* device) : m_device(device) {
    // Create primary command pool
    m_primaryPool = new CommandPool(
        device,
        device->GetGraphicsQueueFamily(),
        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT
    );
    
    // Create secondary pools for each CPU thread
    uint32_t threadCount = std::thread::hardware_concurrency();
    m_secondaryPools.reserve(threadCount);
    
    for (uint32_t i = 0; i < threadCount; ++i) {
        m_secondaryPools.push_back(std::make_unique<CommandPool>(
            device,
            device->GetGraphicsQueueFamily(),
            VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT
        ));
    }
}

CommandBufferRecorder::~CommandBufferRecorder() {
    delete m_primaryPool;
}

void CommandBufferRecorder::BeginFrame(uint32_t frameIndex) {
    m_currentFrame = frameIndex;
    
    // Allocate or reuse primary command buffer
    if (!m_primaryCommandBuffer) {
        m_primaryCommandBuffer = m_primaryPool->AllocateCommandBuffer();
    }
    
    // Clean up old cached command buffers
    const uint32_t MAX_FRAME_LAG = 3;
    m_cachedCommandBuffers.erase(
        std::remove_if(m_cachedCommandBuffers.begin(), m_cachedCommandBuffers.end(),
            [this, MAX_FRAME_LAG](const CachedCommandBuffer& cached) {
                return (m_currentFrame - cached.lastUsedFrame) > MAX_FRAME_LAG;
            }),
        m_cachedCommandBuffers.end()
    );
}

void CommandBufferRecorder::RecordParallel(const std::vector<RecordFunc>& recordFunctions) {
    size_t numFunctions = recordFunctions.size();
    m_secondaryCommandBuffers.resize(numFunctions);
    
    // Record secondary command buffers in parallel
    std::for_each(std::execution::par_unseq,
        recordFunctions.begin(), recordFunctions.end(),
        [this, &recordFunctions](const RecordFunc& func) {
            size_t index = &func - &recordFunctions[0];
            
            // Get thread-local pool
            size_t poolIndex = index % m_secondaryPools.size();
            auto* pool = m_secondaryPools[poolIndex].get();
            
            // Allocate secondary command buffer
            VkCommandBuffer secondaryCmd = pool->AllocateCommandBuffer(
                VK_COMMAND_BUFFER_LEVEL_SECONDARY
            );
            
            // Begin secondary command buffer
            VkCommandBufferInheritanceInfo inheritanceInfo{};
            inheritanceInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
            
            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT |
                            VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
            beginInfo.pInheritanceInfo = &inheritanceInfo;
            
            vkBeginCommandBuffer(secondaryCmd, &beginInfo);
            
            // Record commands
            func(secondaryCmd);
            
            vkEndCommandBuffer(secondaryCmd);
            
            m_secondaryCommandBuffers[index] = secondaryCmd;
        }
    );
    
    // Execute secondary command buffers in primary
    vkCmdExecuteCommands(m_primaryCommandBuffer, 
                        static_cast<uint32_t>(m_secondaryCommandBuffers.size()),
                        m_secondaryCommandBuffers.data());
}

CommandBufferRecorder::CachedCommandBuffer* CommandBufferRecorder::GetCachedCommandBuffer(uint64_t hash) {
    auto it = std::find_if(m_cachedCommandBuffers.begin(), m_cachedCommandBuffers.end(),
        [hash](const CachedCommandBuffer& cached) {
            return cached.hash == hash && !cached.isDirty;
        });
    
    if (it != m_cachedCommandBuffers.end()) {
        it->lastUsedFrame = m_currentFrame;
        return &(*it);
    }
    
    return nullptr;
}

void CommandBufferRecorder::InvalidateCachedCommandBuffer(uint64_t hash) {
    auto it = std::find_if(m_cachedCommandBuffers.begin(), m_cachedCommandBuffers.end(),
        [hash](const CachedCommandBuffer& cached) {
            return cached.hash == hash;
        });
    
    if (it != m_cachedCommandBuffers.end()) {
        it->isDirty = true;
    }
}

// CommandBufferStateTracker implementation
CommandBufferStateTracker::CommandBufferStateTracker() {
    m_pushConstantData.reserve(128);
}

void CommandBufferStateTracker::BindPipeline(VkCommandBuffer cmd, VkPipeline pipeline) {
    if (m_currentPipeline != pipeline) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        m_currentPipeline = pipeline;
    }
}

void CommandBufferStateTracker::BindDescriptorSet(VkCommandBuffer cmd,
                                                  VkPipelineLayout layout,
                                                  uint32_t set,
                                                  VkDescriptorSet descriptorSet) {
    if (set < m_boundDescriptorSets.size() && m_boundDescriptorSets[set] != descriptorSet) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                               layout, set, 1, &descriptorSet, 0, nullptr);
        m_boundDescriptorSets[set] = descriptorSet;
    }
}

void CommandBufferStateTracker::BindVertexBuffer(VkCommandBuffer cmd,
                                                 uint32_t binding,
                                                 VkBuffer buffer,
                                                 VkDeviceSize offset) {
    if (binding < m_boundVertexBuffers.size()) {
        auto& current = m_boundVertexBuffers[binding];
        if (current.buffer != buffer || current.offset != offset) {
            vkCmdBindVertexBuffers(cmd, binding, 1, &buffer, &offset);
            current.buffer = buffer;
            current.offset = offset;
        }
    }
}

void CommandBufferStateTracker::BindIndexBuffer(VkCommandBuffer cmd,
                                               VkBuffer buffer,
                                               VkDeviceSize offset,
                                               VkIndexType type) {
    if (m_boundIndexBuffer.buffer != buffer ||
        m_boundIndexBuffer.offset != offset ||
        m_boundIndexBuffer.type != type) {
        vkCmdBindIndexBuffer(cmd, buffer, offset, type);
        m_boundIndexBuffer.buffer = buffer;
        m_boundIndexBuffer.offset = offset;
        m_boundIndexBuffer.type = type;
    }
}

void CommandBufferStateTracker::PushConstants(VkCommandBuffer cmd,
                                             VkPipelineLayout layout,
                                             VkShaderStageFlags stages,
                                             uint32_t offset,
                                             uint32_t size,
                                             const void* data) {
    // Simple hash of push constant data
    uint32_t hash = 0;
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    for (uint32_t i = 0; i < size; ++i) {
        hash = hash * 31 + bytes[i];
    }
    
    // Only push if data changed
    if (hash != m_pushConstantHash || m_pushConstantData.size() != size) {
        vkCmdPushConstants(cmd, layout, stages, offset, size, data);
        
        m_pushConstantData.resize(size);
        memcpy(m_pushConstantData.data(), data, size);
        m_pushConstantHash = hash;
    }
}

void CommandBufferStateTracker::SetViewport(VkCommandBuffer cmd, const VkViewport& viewport) {
    if (memcmp(&m_currentViewport, &viewport, sizeof(VkViewport)) != 0) {
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        m_currentViewport = viewport;
    }
}

void CommandBufferStateTracker::SetScissor(VkCommandBuffer cmd, const VkRect2D& scissor) {
    if (memcmp(&m_currentScissor, &scissor, sizeof(VkRect2D)) != 0) {
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        m_currentScissor = scissor;
    }
}

void CommandBufferStateTracker::Reset() {
    m_currentPipeline = VK_NULL_HANDLE;
    m_boundDescriptorSets.fill(VK_NULL_HANDLE);
    
    for (auto& binding : m_boundVertexBuffers) {
        binding.buffer = VK_NULL_HANDLE;
        binding.offset = 0;
    }
    
    m_boundIndexBuffer.buffer = VK_NULL_HANDLE;
    m_boundIndexBuffer.offset = 0;
    
    m_pushConstantData.clear();
    m_pushConstantHash = 0;
}

// MultiThreadedCommandBuilder implementation
MultiThreadedCommandBuilder::MultiThreadedCommandBuilder(Device* device, uint32_t threadCount)
    : m_device(device) {
    
    if (threadCount == 0) {
        threadCount = std::thread::hardware_concurrency();
    }
    
    // Create worker threads
    for (uint32_t i = 0; i < threadCount; ++i) {
        auto worker = std::make_unique<WorkerThread>();
        
        worker->commandPool = std::make_unique<CommandPool>(
            device,
            device->GetGraphicsQueueFamily(),
            VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT
        );
        
        worker->thread = std::thread([this, w = worker.get()]() {
            while (w->running) {
                RenderTask task;
                
                {
                    std::unique_lock lock(w->mutex);
                    w->cv.wait(lock, [w] {
                        return !w->running || !w->tasks.empty();
                    });
                    
                    if (!w->running) break;
                    
                    if (!w->tasks.empty()) {
                        task = std::move(w->tasks.front());
                        w->tasks.pop();
                    }
                }
                
                if (task.recordFunc) {
                    // Allocate command buffer
                    if (!w->currentBuffer) {
                        w->currentBuffer = w->commandPool->AllocateCommandBuffer(
                            VK_COMMAND_BUFFER_LEVEL_SECONDARY
                        );
                    }
                    
                    // Record commands
                    VkCommandBufferInheritanceInfo inheritanceInfo{};
                    inheritanceInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
                    
                    VkCommandBufferBeginInfo beginInfo{};
                    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
                    beginInfo.pInheritanceInfo = &inheritanceInfo;
                    
                    vkBeginCommandBuffer(w->currentBuffer, &beginInfo);
                    task.recordFunc(w->currentBuffer);
                    vkEndCommandBuffer(w->currentBuffer);
                    
                    // Add to completed list
                    {
                        std::lock_guard lock(m_completedMutex);
                        m_completedBuffers.push_back(w->currentBuffer);
                    }
                    
                    w->currentBuffer = VK_NULL_HANDLE;
                }
            }
        });
        
        m_workers.push_back(std::move(worker));
    }
}

MultiThreadedCommandBuilder::~MultiThreadedCommandBuilder() {
    // Stop all workers
    for (auto& worker : m_workers) {
        {
            std::lock_guard lock(worker->mutex);
            worker->running = false;
        }
        worker->cv.notify_all();
        
        if (worker->thread.joinable()) {
            worker->thread.join();
        }
    }
}

void MultiThreadedCommandBuilder::SubmitTask(const RenderTask& task) {
    // Find worker with least tasks
    WorkerThread* selectedWorker = nullptr;
    size_t minTasks = SIZE_MAX;
    
    for (auto& worker : m_workers) {
        std::lock_guard lock(worker->mutex);
        if (worker->tasks.size() < minTasks) {
            minTasks = worker->tasks.size();
            selectedWorker = worker.get();
        }
    }
    
    if (selectedWorker) {
        {
            std::lock_guard lock(selectedWorker->mutex);
            selectedWorker->tasks.push(task);
        }
        selectedWorker->cv.notify_one();
    }
}

void MultiThreadedCommandBuilder::ExecuteTasks(VkCommandBuffer primaryCmd) {
    // Wait for all tasks to complete
    bool allComplete = false;
    while (!allComplete) {
        allComplete = true;
        for (auto& worker : m_workers) {
            std::lock_guard lock(worker->mutex);
            if (!worker->tasks.empty()) {
                allComplete = false;
                break;
            }
        }
        
        if (!allComplete) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }
    
    // Execute all completed secondary buffers
    std::lock_guard lock(m_completedMutex);
    if (!m_completedBuffers.empty()) {
        vkCmdExecuteCommands(primaryCmd,
                           static_cast<uint32_t>(m_completedBuffers.size()),
                           m_completedBuffers.data());
        m_completedBuffers.clear();
    }
}

} // namespace RHI::Vulkan