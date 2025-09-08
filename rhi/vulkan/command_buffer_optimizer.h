// engine/rhi/vulkan/command_buffer_optimizer.h
#pragma once

#include "vulkan_common.h"


namespace RHI::Vulkan {

class Device;
class CommandPool;

// Secondary command buffer for parallel recording
class SecondaryCommandBuffer {
public:
    SecondaryCommandBuffer(CommandPool* pool, VkCommandBufferInheritanceInfo* inheritanceInfo);
    ~SecondaryCommandBuffer();
    
    void Begin();
    void End();
    
    VkCommandBuffer GetHandle() const { return m_commandBuffer; }
    
private:
    VkCommandBuffer m_commandBuffer;
    VkCommandBufferInheritanceInfo m_inheritanceInfo;
    CommandPool* m_pool;
};

// Command buffer recording optimizer
class CommandBufferRecorder {
public:
    CommandBufferRecorder(Device* device);
    ~CommandBufferRecorder();
    
    // Pre-record static command buffers
    void PreRecordStaticCommands();
    
    // Begin frame recording
    void BeginFrame(uint32_t frameIndex);
    void EndFrame();
    
    // Record commands in parallel
    using RecordFunc = std::function<void(VkCommandBuffer)>;
    
    void RecordParallel(const std::vector<RecordFunc>& recordFunctions);
    
    // Get optimized command buffer
    VkCommandBuffer GetPrimaryCommandBuffer() const { return m_primaryCommandBuffer; }
    
    // Command buffer caching
    struct CachedCommandBuffer {
        VkCommandBuffer buffer;
        uint64_t hash;
        uint32_t lastUsedFrame;
        bool isDirty;
    };
    
    CachedCommandBuffer* GetCachedCommandBuffer(uint64_t hash);
    void InvalidateCachedCommandBuffer(uint64_t hash);
    void InvalidateAllCachedCommandBuffers();
    
private:
    Device* m_device;
    CommandPool* m_primaryPool;
    std::vector<std::unique_ptr<CommandPool>> m_secondaryPools;
    
    VkCommandBuffer m_primaryCommandBuffer = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> m_secondaryCommandBuffers;
    
    // Command buffer cache
    std::vector<CachedCommandBuffer> m_cachedCommandBuffers;
    uint32_t m_currentFrame = 0;
    
    // Thread pool for parallel recording
    class ThreadPool;
    std::unique_ptr<ThreadPool> m_threadPool;
};

// Multi-threaded command buffer builder
class MultiThreadedCommandBuilder {
public:
    MultiThreadedCommandBuilder(Device* device, uint32_t threadCount = 0);
    ~MultiThreadedCommandBuilder();
    
    // Task submission
    struct RenderTask {
        std::function<void(VkCommandBuffer)> recordFunc;
        uint32_t priority = 0;
        bool canRunParallel = true;
    };
    
    void SubmitTask(const RenderTask& task);
    void ExecuteTasks(VkCommandBuffer primaryCmd);
    
private:
    struct WorkerThread {
        std::thread thread;
        std::unique_ptr<CommandPool> commandPool;
        std::queue<RenderTask> tasks;
        std::mutex mutex;
        std::condition_variable cv;
        std::atomic<bool> running{true};
        VkCommandBuffer currentBuffer = VK_NULL_HANDLE;
    };
    
    Device* m_device;
    std::vector<std::unique_ptr<WorkerThread>> m_workers;
    std::vector<VkCommandBuffer> m_completedBuffers;
    std::mutex m_completedMutex;
};

// Command buffer state tracker for reducing redundant state changes
class CommandBufferStateTracker {
public:
    CommandBufferStateTracker();
    
    // Pipeline state
    void BindPipeline(VkCommandBuffer cmd, VkPipeline pipeline);
    bool IsPipelineBound(VkPipeline pipeline) const { return m_currentPipeline == pipeline; }
    
    // Descriptor sets
    void BindDescriptorSet(VkCommandBuffer cmd, VkPipelineLayout layout,
                          uint32_t set, VkDescriptorSet descriptorSet);
    bool IsDescriptorSetBound(uint32_t set, VkDescriptorSet descriptorSet) const;
    
    // Vertex/Index buffers
    void BindVertexBuffer(VkCommandBuffer cmd, uint32_t binding, VkBuffer buffer, VkDeviceSize offset);
    void BindIndexBuffer(VkCommandBuffer cmd, VkBuffer buffer, VkDeviceSize offset, VkIndexType type);
    
    bool IsVertexBufferBound(uint32_t binding, VkBuffer buffer, VkDeviceSize offset) const;
    bool IsIndexBufferBound(VkBuffer buffer, VkDeviceSize offset, VkIndexType type) const;
    
    // Push constants
    void PushConstants(VkCommandBuffer cmd, VkPipelineLayout layout,
                       VkShaderStageFlags stages, uint32_t offset,
                       uint32_t size, const void* data);
    
    // Viewport/Scissor
    void SetViewport(VkCommandBuffer cmd, const VkViewport& viewport);
    void SetScissor(VkCommandBuffer cmd, const VkRect2D& scissor);
    
    // Reset state
    void Reset();
    
private:
    // Current state
    VkPipeline m_currentPipeline = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, 4> m_boundDescriptorSets{};
    
    struct VertexBufferBinding {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceSize offset = 0;
    };
    std::array<VertexBufferBinding, 8> m_boundVertexBuffers;
    
    struct IndexBufferBinding {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceSize offset = 0;
        VkIndexType type = VK_INDEX_TYPE_UINT32;
    } m_boundIndexBuffer;
    
    VkViewport m_currentViewport{};
    VkRect2D m_currentScissor{};
    
    // Push constant cache
    std::vector<uint8_t> m_pushConstantData;
    uint32_t m_pushConstantHash = 0;
};