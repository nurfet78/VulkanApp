// engine/renderer/render_context.h
#pragma once

#include "rhi/vulkan/vulkan_common.h"


namespace RHI::Vulkan {
    class Device;
    class Swapchain;
    class CommandPoolManager;
    class ResourceManager;
}

namespace Renderer {

// Manages all rendering resources and state
class RenderContext {
public:
    RenderContext(Core::Window* window, bool enableValidation = true);
    ~RenderContext();
    
    // Getters
    RHI::Vulkan::Device* GetDevice() const { return m_device.get(); }
    RHI::Vulkan::Swapchain* GetSwapchain() const { return m_swapchain.get(); }
    RHI::Vulkan::CommandPoolManager* GetCommandPoolManager() const { return m_commandPoolManager.get(); }
    RHI::Vulkan::ResourceManager* GetResourceManager() const { return m_resourceManager.get(); }
    
    // Swapchain management
    void RecreateSwapchain(uint32_t width, uint32_t height);
    
    // Resource management
    void GarbageCollect();
    
private:
    std::unique_ptr<RHI::Vulkan::Device> m_device;
    std::unique_ptr<RHI::Vulkan::Swapchain> m_swapchain;
    std::unique_ptr<RHI::Vulkan::CommandPoolManager> m_commandPoolManager;
    std::unique_ptr<RHI::Vulkan::ResourceManager> m_resourceManager;
};