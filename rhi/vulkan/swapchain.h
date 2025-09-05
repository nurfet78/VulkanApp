// engine/rhi/vulkan/swapchain.h
#pragma once

#include "vulkan_common.h"
#include <memory>
#include <functional>

namespace RHI::Vulkan {

class Device;

class Swapchain {
public:
    struct Frame {
        VkImage image = VK_NULL_HANDLE;
        VkImageView imageView = VK_NULL_HANDLE;
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        VkSemaphore renderFinished = VK_NULL_HANDLE;
        VkFence inFlight = VK_NULL_HANDLE;
    };
    
    Swapchain(Device* device, uint32_t width, uint32_t height, bool vsync = true);
    ~Swapchain();
    
    // Acquire next image
    VkResult AcquireNextImage(uint32_t* imageIndex, uint64_t timeout = UINT64_MAX);
    
    // Present current frame
    VkResult Present(uint32_t imageIndex);
    
    // Recreate swapchain (for resize)
    void Recreate(uint32_t width, uint32_t height);
    
    // Getters
    VkSwapchainKHR GetHandle() const { return m_swapchain; }
    VkFormat GetFormat() const { return m_surfaceFormat.format; }
    VkExtent2D GetExtent() const { return m_extent; }
    uint32_t GetImageCount() const { return static_cast<uint32_t>(m_frames.size()); }
    
    const Frame& GetFrame(uint32_t index) const { return m_frames[index]; }
    uint32_t GetCurrentFrameIndex() const { return m_currentFrame; }
    
    // Wait for current frame fence
    void WaitForFence(uint32_t frameIndex, uint64_t timeout = UINT64_MAX);
    void ResetFence(uint32_t frameIndex);

private:
    void CreateSwapchain(uint32_t width, uint32_t height);
    void CreateImageViews();
    void CreateSyncObjects();
    void CleanupSwapchain();
    
    VkSurfaceFormatKHR ChooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats);
    VkPresentModeKHR ChoosePresentMode(const std::vector<VkPresentModeKHR>& modes);
    VkExtent2D ChooseExtent(const VkSurfaceCapabilitiesKHR& capabilities, uint32_t width, uint32_t height);
    
    Device* m_device;
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    
    std::vector<Frame> m_frames;
    VkSurfaceFormatKHR m_surfaceFormat;
    VkPresentModeKHR m_presentMode;
    VkExtent2D m_extent;
    
    uint32_t m_currentFrame = 0;
    bool m_vsync;
};

} // namespace RHI::Vulkan