// meadow_app.cpp
#include "meadow_app.h"
#include "core/window.h"
#include "core/input.h"
#include "core/time.h"
#include "core/job_system.h"
#include "rhi/vulkan/device.h"
#include "rhi/vulkan/swapchain.h"
#include "rhi/vulkan/command_pool.h"
#include "rhi/vulkan/pipeline.h"
#include "rhi/vulkan/staging_buffer_pool.h"
#include <iostream>
#include <iomanip>
#include <thread>

MeadowApp::MeadowApp() 
    : Core::Application({
        .title = "Meadow World - Vulkan 1.3",
        .width = 1920,
        .height = 1080,
        .vsync = true,
        .fullscreen = false,
        .workerThreads = std::thread::hardware_concurrency() - 1
    }) {
}

MeadowApp::~MeadowApp() noexcept = default;

void MeadowApp::OnInitialize() {
    std::cout << "Initializing Meadow World..." << std::endl;
    
    // Initialize Vulkan
    m_device = std::make_unique<RHI::Vulkan::Device>(GetWindow(), true);
    
    // Initialize staging buffer pool
    RHI::Vulkan::StagingBufferPool::Get().Initialize(m_device.get());
    
    // Create swapchain
    m_swapchain = std::make_unique<RHI::Vulkan::Swapchain>(
        m_device.get(), 
        GetWindow()->GetWidth(), 
        GetWindow()->GetHeight(),
        true // vsync
    );
    
    // Store initial image count
    m_swapchainImageCount = m_swapchain->GetImageCount();
    
    // Create command pool manager
    m_commandPoolManager = std::make_unique<RHI::Vulkan::CommandPoolManager>(m_device.get());
    
    // Create triangle pipeline
    m_trianglePipeline = std::make_unique<RHI::Vulkan::TrianglePipeline>(
        m_device.get(),
        m_swapchain->GetFormat()
    );
    
    // Create sync objects and command buffers for each frame
    CreateSyncObjects();
    
    // Set resize callback
    GetWindow()->SetResizeCallback([this](uint32_t width, uint32_t height) {
        m_framebufferResized = true;
    });
    
    // Additional key bindings
    GetInput()->RegisterKeyCallback(GLFW_KEY_ESCAPE, [this](int action) {
        if (action == GLFW_PRESS) {
            RequestExit();
        }
    });
    
    // Show controls
    std::cout << "\n=== Controls ===" << std::endl;
    std::cout << "F11    - Toggle fullscreen" << std::endl;
    std::cout << "ESC    - Exit" << std::endl;
    std::cout << "================" << std::endl;
    
    std::cout << "\nVulkan Device: " << m_device->GetProperties().deviceName << std::endl;
    std::cout << "Swapchain Images: " << m_swapchain->GetImageCount() << " (Triple Buffering)" << std::endl;
    std::cout << "Frames in Flight: " << MAX_FRAMES_IN_FLIGHT << std::endl;
    std::cout << "Worker Threads: " << GetJobSystem()->GetThreadCount() << std::endl;
}

void MeadowApp::OnShutdown() {
    std::cout << "Shutting down..." << std::endl;
    
    // Wait for all frames to complete
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (m_frames[i].inFlightFence != VK_NULL_HANDLE) {
            vkWaitForFences(m_device->GetDevice(), 1, &m_frames[i].inFlightFence, VK_TRUE, UINT64_MAX);
        }
    }
    
    // Cleanup staging buffer pool
    RHI::Vulkan::StagingBufferPool::Get().Shutdown();
    
    // Cleanup in reverse order
    DestroySyncObjects();
    m_trianglePipeline.reset();
    m_commandPoolManager.reset();
    m_swapchain.reset();
    m_device.reset();
}

void MeadowApp::OnUpdate(float deltaTime) {
    m_totalTime += deltaTime;
    
    // FPS counter
    m_frameCounter++;
    m_fpsTimer += deltaTime;
    
    if (m_fpsTimer >= 1.0f) {
        std::cout << "\rFPS: " << std::setw(4) << GetTime()->GetFPS() 
                  << " | Frame Time: " << std::fixed << std::setprecision(2) 
                  << GetTime()->GetFrameTime() << "ms" 
                  << " | Fullscreen: " << (GetWindow()->IsFullscreen() ? "ON " : "OFF")
                  << std::flush;
        
        m_frameCounter = 0;
        m_fpsTimer = 0.0f;
        
        // Garbage collect staging buffers periodically
        RHI::Vulkan::StagingBufferPool::Get().GarbageCollect();
    }
    
    // Handle window resize
    if (m_framebufferResized) {
        RecreateSwapchain();
        m_framebufferResized = false;
    }
}

void MeadowApp::OnRender() {
    DrawFrame();
}

void MeadowApp::CreateSyncObjects() {
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    
    auto* graphicsPool = m_commandPoolManager->GetGraphicsPool();
    
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VK_CHECK(vkCreateSemaphore(m_device->GetDevice(), &semaphoreInfo, nullptr, 
                                  &m_frames[i].imageAvailableSemaphore));
        VK_CHECK(vkCreateSemaphore(m_device->GetDevice(), &semaphoreInfo, nullptr, 
                                  &m_frames[i].renderFinishedSemaphore));
        VK_CHECK(vkCreateFence(m_device->GetDevice(), &fenceInfo, nullptr, 
                              &m_frames[i].inFlightFence));
        
        m_frames[i].commandBuffer = graphicsPool->AllocateCommandBuffer();
    }
}

void MeadowApp::DestroySyncObjects() {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (m_frames[i].imageAvailableSemaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_device->GetDevice(), m_frames[i].imageAvailableSemaphore, nullptr);
        }
        if (m_frames[i].renderFinishedSemaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_device->GetDevice(), m_frames[i].renderFinishedSemaphore, nullptr);
        }
        if (m_frames[i].inFlightFence != VK_NULL_HANDLE) {
            vkDestroyFence(m_device->GetDevice(), m_frames[i].inFlightFence, nullptr);
        }
    }
}

void MeadowApp::RecreateSwapchain() {
    // Handle minimization
    int width = 0, height = 0;
    while (width == 0 || height == 0) {
        width = GetWindow()->GetWidth();
        height = GetWindow()->GetHeight();
        GetWindow()->PollEvents();
    }
    
    // Wait only for current frame to complete
    vkWaitForFences(m_device->GetDevice(), 1, &m_frames[m_currentFrame].inFlightFence, VK_TRUE, UINT64_MAX);
    
    uint32_t oldImageCount = m_swapchainImageCount;
    m_swapchain->Recreate(width, height);
    m_swapchainImageCount = m_swapchain->GetImageCount();
    
    // Check if image count changed and recreate command buffers if needed
    if (oldImageCount != m_swapchainImageCount) {
        DestroySyncObjects();
        CreateSyncObjects();
    }
}

void MeadowApp::RecordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = 0;
    
    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));
    
    // Transition swapchain image to color attachment
    VkImageMemoryBarrier2 imageBarrier{};
    imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    imageBarrier.srcAccessMask = 0;
    imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    imageBarrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    imageBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    imageBarrier.image = m_swapchain->GetFrame(imageIndex).image;
    imageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageBarrier.subresourceRange.baseMipLevel = 0;
    imageBarrier.subresourceRange.levelCount = 1;
    imageBarrier.subresourceRange.baseArrayLayer = 0;
    imageBarrier.subresourceRange.layerCount = 1;
    
    VkDependencyInfo depInfo{};
    depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    depInfo.imageMemoryBarrierCount = 1;
    depInfo.pImageMemoryBarriers = &imageBarrier;
    
    vkCmdPipelineBarrier2(cmd, &depInfo);
    
    // Debug marker
    m_device->BeginDebugLabel(cmd, "Triangle Pass", {1.0f, 0.0f, 0.0f, 1.0f});
    
    // Render triangle
    m_trianglePipeline->Render(
        cmd,
        m_swapchain->GetFrame(imageIndex).imageView,
        m_swapchain->GetExtent()
    );
    
    m_device->EndDebugLabel(cmd);
    
    // Transition to present
    imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    imageBarrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
    imageBarrier.dstAccessMask = 0;
    imageBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    imageBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    
    vkCmdPipelineBarrier2(cmd, &depInfo);
    
    VK_CHECK(vkEndCommandBuffer(cmd));
}

void MeadowApp::DrawFrame() {
    // Wait for previous frame with same index
    VkResult result = vkWaitForFences(m_device->GetDevice(), 1, 
                                      &m_frames[m_currentFrame].inFlightFence, 
                                      VK_TRUE, UINT64_MAX);
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to wait for fence: " << result << std::endl;
        return;
    }
    
    uint32_t imageIndex;
    result = vkAcquireNextImageKHR(
        m_device->GetDevice(),
        m_swapchain->GetHandle(),
        UINT64_MAX,
        m_frames[m_currentFrame].imageAvailableSemaphore,
        VK_NULL_HANDLE,
        &imageIndex
    );
    
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        RecreateSwapchain();
        return;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("Failed to acquire swapchain image");
    }
    
    VK_CHECK(vkResetFences(m_device->GetDevice(), 1, &m_frames[m_currentFrame].inFlightFence));
    
    // Record command buffer
    VK_CHECK(vkResetCommandBuffer(m_frames[m_currentFrame].commandBuffer, 0));
    RecordCommandBuffer(m_frames[m_currentFrame].commandBuffer, imageIndex);
    
    // Submit with thread-safe method
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    
    VkSemaphore waitSemaphores[] = {m_frames[m_currentFrame].imageAvailableSemaphore};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_frames[m_currentFrame].commandBuffer;
    
    VkSemaphore signalSemaphores[] = {m_frames[m_currentFrame].renderFinishedSemaphore};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;
    
    // Use thread-safe submit
    VK_CHECK(m_device->SubmitGraphics(&submitInfo, m_frames[m_currentFrame].inFlightFence));
    
    // Present
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    
    VkSwapchainKHR swapChains[] = {m_swapchain->GetHandle()};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;
    
    result = vkQueuePresentKHR(m_device->GetPresentQueue(), &presentInfo);
    
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        m_framebufferResized = true;
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to present swapchain image");
    }
    
    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}