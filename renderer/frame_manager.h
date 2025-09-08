// engine/renderer/frame_manager.h
#pragma once

#include "rhi/vulkan/vulkan_common.h"


namespace Renderer {

class RenderContext;

// Manages frame synchronization and command buffers
class FrameManager {
public:
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
    
    struct FrameData {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
        VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;
        VkFence inFlightFence = VK_NULL_HANDLE;
    };
    
    FrameManager(RenderContext* context);
    ~FrameManager();
    
    // Frame lifecycle
    bool BeginFrame(uint32_t& imageIndex);
    void EndFrame(uint32_t imageIndex);
    
    // Get current frame data
    FrameData& GetCurrentFrame() { return m_frames[m_currentFrame]; }
    uint32_t GetCurrentFrameIndex() const { return m_currentFrame; }
    
    // Wait for all frames to complete
    void WaitForAllFrames();
    
private:
    void CreateSyncObjects();
    void DestroySyncObjects();
    
    RenderContext* m_context;
    std::array<FrameData, MAX_FRAMES_IN_FLIGHT> m_frames;
    uint32_t m_currentFrame = 0;
    bool m_framebufferResized = false;
};