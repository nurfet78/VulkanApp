// engine/rhi/vulkan/swapchain.cpp
#include "swapchain.h"
#include "device.h"


namespace RHI::Vulkan {

Swapchain::Swapchain(Device* device, uint32_t width, uint32_t height, bool vsync)
    : m_device(device), m_vsync(vsync) {
    CreateSwapchain(width, height);
    CreateImageViews();
    CreateDepthResources();
    CreateSyncObjects();
}

Swapchain::~Swapchain() {
    CleanupDepthResources();
    CleanupSwapchain();
}

VkResult Swapchain::AcquireNextImage(uint32_t* imageIndex, uint64_t timeout) {
    WaitForFence(m_currentFrame, timeout);
    
    VkResult result = vkAcquireNextImageKHR(
        m_device->GetDevice(),
        m_swapchain,
        timeout,
        m_frames[m_currentFrame].imageAvailable,
        VK_NULL_HANDLE,
        imageIndex
    );
    
    return result;
}

VkResult Swapchain::Present(uint32_t imageIndex) {
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &m_frames[m_currentFrame].renderFinished;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_swapchain;
    presentInfo.pImageIndices = &imageIndex;
    
    VkResult result = vkQueuePresentKHR(m_device->GetPresentQueue(), &presentInfo);
    
    m_currentFrame = (m_currentFrame + 1) % m_frames.size();
    
    return result;
}

void Swapchain::Recreate(uint32_t width, uint32_t height) {
    // Wait for current frame fence instead of device idle
    WaitForFence(m_currentFrame, UINT64_MAX);
    
    // Store old image count
    uint32_t oldImageCount = static_cast<uint32_t>(m_frames.size());
    
    CleanupDepthResources();
    CleanupSwapchain();
    CreateSwapchain(width, height);
    CreateImageViews();
    CreateDepthResources();
    CreateSyncObjects();
    
    // Check if image count changed
    uint32_t newImageCount = static_cast<uint32_t>(m_frames.size());
    if (oldImageCount != newImageCount) {
        // Notify that recreate changed image count
        // This should trigger command buffer recreation in app
        m_imageCountChanged = true;
    }
}

void Swapchain::CreateDepthResources() {
    // 1. Находим подходящий формат глубины
    m_depthFormat = m_device->FindSupportedFormat(
        { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
    );


    // 2. Создаем VkImage для буфера глубины
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = m_extent.width;
    imageInfo.extent.height = m_extent.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = m_depthFormat;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE; // VMA сама выберет лучшую память (device local)

    // Создаем образ и выделяем под него память одной командой
    vmaCreateImage(m_device->GetAllocator(), &imageInfo, &allocInfo, &m_depthImage, &m_depthImageAllocation, nullptr);

    // 3. Создаем VkImageView для буфера глубины
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_depthImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = m_depthFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VK_CHECK(vkCreateImageView(m_device->GetDevice(), &viewInfo, nullptr, &m_depthImageView));
}

void Swapchain::CleanupDepthResources() {
    if (m_depthImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device->GetDevice(), m_depthImageView, nullptr);
        m_depthImageView = VK_NULL_HANDLE;
    }
    if (m_depthImage != VK_NULL_HANDLE) {
        // Уничтожаем и образ, и память, выделенную под него
        vmaDestroyImage(m_device->GetAllocator(), m_depthImage, m_depthImageAllocation);
        m_depthImage = VK_NULL_HANDLE;
        m_depthImageAllocation = VK_NULL_HANDLE;
    }
}

void Swapchain::WaitForFence(uint32_t frameIndex, uint64_t timeout) {
    VkResult result = vkWaitForFences(m_device->GetDevice(), 1, 
                                      &m_frames[frameIndex].inFlight, 
                                      VK_TRUE, timeout);
    if (result == VK_TIMEOUT) {
        std::cerr << "Warning: Fence wait timeout" << std::endl;
    }
}

void Swapchain::ResetFence(uint32_t frameIndex) {
    vkResetFences(m_device->GetDevice(), 1, &m_frames[frameIndex].inFlight);
}

void Swapchain::CreateSwapchain(uint32_t width, uint32_t height) {
    SwapchainSupportDetails support = m_device->QuerySwapchainSupport();
    
    m_surfaceFormat = ChooseSurfaceFormat(support.formats);
    m_presentMode = ChoosePresentMode(support.presentModes);
    m_extent = ChooseExtent(support.capabilities, width, height);
    
    // Request triple buffering (3 images) if possible
    uint32_t imageCount = 3;
    if (support.capabilities.maxImageCount > 0 && imageCount > support.capabilities.maxImageCount) {
        imageCount = support.capabilities.maxImageCount;
    }
    if (imageCount < support.capabilities.minImageCount) {
        imageCount = support.capabilities.minImageCount;
    }
    
    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = m_device->GetSurface();
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = m_surfaceFormat.format;
    createInfo.imageColorSpace = m_surfaceFormat.colorSpace;
    createInfo.imageExtent = m_extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    
    uint32_t queueFamilyIndices[] = {
        m_device->GetGraphicsQueueFamily(),
        m_device->GetPresentQueueFamily()
    };
    
    if (queueFamilyIndices[0] != queueFamilyIndices[1]) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    
    createInfo.preTransform = support.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = m_presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;
    
    VK_CHECK(vkCreateSwapchainKHR(m_device->GetDevice(), &createInfo, nullptr, &m_swapchain));
    
    // Get swapchain images
    vkGetSwapchainImagesKHR(m_device->GetDevice(), m_swapchain, &imageCount, nullptr);
    m_frames.resize(imageCount);
    
    std::vector<VkImage> images(imageCount);
    vkGetSwapchainImagesKHR(m_device->GetDevice(), m_swapchain, &imageCount, images.data());
    
    for (size_t i = 0; i < images.size(); i++) {
        m_frames[i].image = images[i];
    }
}

void Swapchain::CreateImageViews() {
    for (auto& frame : m_frames) {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = frame.image;
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = m_surfaceFormat.format;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;
        
        VK_CHECK(vkCreateImageView(m_device->GetDevice(), &createInfo, nullptr, &frame.imageView));
    }
}

void Swapchain::CreateSyncObjects() {
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    
    for (auto& frame : m_frames) {
        VK_CHECK(vkCreateSemaphore(m_device->GetDevice(), &semaphoreInfo, nullptr, &frame.imageAvailable));
        VK_CHECK(vkCreateSemaphore(m_device->GetDevice(), &semaphoreInfo, nullptr, &frame.renderFinished));
        VK_CHECK(vkCreateFence(m_device->GetDevice(), &fenceInfo, nullptr, &frame.inFlight));
    }
}

void Swapchain::CleanupSwapchain() {
    VkDevice device = m_device->GetDevice();
    
    for (auto& frame : m_frames) {
        if (frame.imageView) {
            vkDestroyImageView(device, frame.imageView, nullptr);
        }
        if (frame.imageAvailable) {
            vkDestroySemaphore(device, frame.imageAvailable, nullptr);
        }
        if (frame.renderFinished) {
            vkDestroySemaphore(device, frame.renderFinished, nullptr);
        }
        if (frame.inFlight) {
            vkDestroyFence(device, frame.inFlight, nullptr);
        }
    }
    
    if (m_swapchain) {
        vkDestroySwapchainKHR(device, m_swapchain, nullptr);
    }
    
    m_frames.clear();
}

VkSurfaceFormatKHR Swapchain::ChooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) {
    // Prefer sRGB
    for (const auto& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB && 
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }
    
    // Fallback to first available
    return formats[0];
}

VkPresentModeKHR Swapchain::ChoosePresentMode(const std::vector<VkPresentModeKHR>& modes) {
    if (!m_vsync) {
        // Prefer mailbox (triple buffering) for no vsync
        for (const auto& mode : modes) {
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
                return mode;
            }
        }
        
        // Fallback to immediate
        for (const auto& mode : modes) {
            if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
                return mode;
            }
        }
    }
    
    // FIFO is guaranteed to be available (vsync)
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D Swapchain::ChooseExtent(const VkSurfaceCapabilitiesKHR& capabilities, 
                                   uint32_t width, uint32_t height) {
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    }
    
    VkExtent2D actualExtent = { width, height };
    
    actualExtent.width = std::clamp(actualExtent.width, 
                                   capabilities.minImageExtent.width,
                                   capabilities.maxImageExtent.width);
    actualExtent.height = std::clamp(actualExtent.height,
                                    capabilities.minImageExtent.height,
                                    capabilities.maxImageExtent.height);
    
    return actualExtent;
}

} // namespace RHI::Vulkan