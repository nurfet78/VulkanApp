// meadow_app.h
#pragma once

#include "core/application.h"
#include <memory>
#include <array>

namespace RHI::Vulkan {
    class Device;
    class Swapchain;
    class CommandPoolManager;
    class TrianglePipeline;
	class ResourceManager;  // Добавлено
}

class MeadowApp : public Core::Application {
public:
    MeadowApp();
    ~MeadowApp() noexcept;

protected:
    void OnInitialize() override;
    void OnShutdown() override;
    void OnUpdate(float deltaTime) override;
    void OnRender() override;

private:
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
    
    struct FrameData {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
        VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;
        VkFence inFlightFence = VK_NULL_HANDLE;
    };
    
    void RecreateSwapchain();
    void DrawFrame();
    void CreateSyncObjects();
    void DestroySyncObjects();
    void RecordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex);
    
    std::unique_ptr<RHI::Vulkan::Device> m_device;
    std::unique_ptr<RHI::Vulkan::Swapchain> m_swapchain;
    std::unique_ptr<RHI::Vulkan::CommandPoolManager> m_commandPoolManager;
	std::unique_ptr<RHI::Vulkan::ResourceManager> m_resourceManager;  // Добавлено
    std::unique_ptr<RHI::Vulkan::TrianglePipeline> m_trianglePipeline;
    
    std::array<FrameData, MAX_FRAMES_IN_FLIGHT> m_frames;
    uint32_t m_currentFrame = 0;
    uint32_t m_swapchainImageCount = 0;
    
    bool m_framebufferResized = false;
    float m_totalTime = 0.0f;
    uint32_t m_frameCounter = 0;
    float m_fpsTimer = 0.0f;
};