// meadow_app.cpp - ИСПРАВЛЕННАЯ ВЕРСИЯ

#include "rhi/vulkan/staging_buffer_pool.h"
#include "meadow_app.h"
#include "core/window.h"
#include "core/input.h"
#include "core/time_utils.h"
#include "core/job_system.h"
#include "rhi/vulkan/device.h"
#include "rhi/vulkan/swapchain.h"
#include "rhi/vulkan/command_pool.h"
#include "rhi/vulkan/resource.h"
#include "rhi/vulkan/shader_manager.h"
#include "renderer/triangle_renderer.h"
#include "renderer/sky_renderer.h"


MeadowApp::MeadowApp() 
    : Core::Application({
        .title = "Meadow World - Vulkan 1.4",
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

    // 1. Initialize Vulkan Device first (это создает instance, surface, physical device, logical device)
    m_device = std::make_unique<RHI::Vulkan::Device>(GetWindow(), true);

    // Теперь Device полностью инициализирован и знает семейства очередей

    m_shaderManager = std::make_unique<RHI::Vulkan::ShaderManager>(m_device.get());

    // 3. Load all shader programs needed for the application
    LoadShaders(); // Выносим загрузку шейдеров в отдельную функцию для чистоты

    // 2. Create swapchain (требует полностью инициализированный Device)
    m_swapchain = std::make_unique<RHI::Vulkan::Swapchain>(
        m_device.get(),
        GetWindow()->GetWidth(),
        GetWindow()->GetHeight(),
        true // vsync
    );

    // Store initial image count
    m_swapchainImageCount = m_swapchain->GetImageCount();

    // 3. Create command pool manager (теперь Device знает семейства очередей)
    m_commandPoolManager = std::make_unique<RHI::Vulkan::CommandPoolManager>(m_device.get());

    // 4. Create resource manager с valid command pool
    m_resourceManager = std::make_unique<RHI::Vulkan::ResourceManager>(
        m_device.get(),
        m_commandPoolManager->GetTransferPool()
    );

    // 5. Create triangle pipeline (требует Device и format от swapchain)
    m_trianglePipeline = std::make_unique<RHI::Vulkan::TriangleRenderer>(
        m_device.get(),
        m_shaderManager.get(),
        m_swapchain->GetFormat()
    );

    // Создаем рендер неба
    m_skyRenderer = std::make_unique<Renderer::SkyRenderer>(
        m_device.get(),
        m_shaderManager.get(),
        m_swapchain->GetFormat(),
        m_swapchain->GetDepthFormat() 
    );

    // 6. Create sync objects and command buffers for each frame
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

void MeadowApp::LoadShaders() {
    std::cout << "Compiling shaders..." << std::endl;

    // Шейдерная программа для тестового треугольника
    auto* triangleProgram = m_shaderManager->CreateProgram("Triangle");
    triangleProgram->AddStage(VK_SHADER_STAGE_VERTEX_BIT, "shaders/triangle.vert.spv");
    triangleProgram->AddStage(VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/triangle.frag.spv");
    if (!triangleProgram->Compile()) {
        throw std::runtime_error("Failed to compile Triangle shader program!");
    }

    // Шейдерная программа для скайбокса (если есть)
     auto* skyProgram = m_shaderManager->CreateProgram("Sky");
     skyProgram->AddStage(VK_SHADER_STAGE_VERTEX_BIT, "shaders/sky.vert.spv");
     skyProgram->AddStage(VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/sky.frag.spv");
     if (!skyProgram->Compile()) {
         throw std::runtime_error("Failed to compile Sky shader program!");
     }

    // ... Добавьте здесь все остальные шейдерные программы вашего приложения
}

void MeadowApp::OnShutdown() {
    std::cout << "Shutting down..." << std::endl;

    // Проверяем, что m_device вообще существует, прежде чем его использовать
    if (!m_device) {
        return;
    }

    // Ждем, пока GPU закончит все свои дела.
    // vkDeviceWaitIdle делает то же самое, что и ожидание всех fence'ов, и даже больше.
    // Поэтому можно оставить только его, это надежнее.
    vkDeviceWaitIdle(m_device->GetDevice());

    // Теперь безопасно уничтожать объекты в порядке, ОБРАТНОМ их созданию.

    // 1. Уничтожаем объекты синхронизации
    DestroySyncObjects();

    // 2. Уничтожаем высокоуровневые объекты рендера
    //if (m_skyRenderer) m_skyRenderer.reset(); // Не забудьте, если добавили
    if (m_skyRenderer) m_skyRenderer.reset();
    if (m_trianglePipeline) m_trianglePipeline.reset();

    // 3. Уничтожаем менеджеры
    if (m_resourceManager) m_resourceManager.reset();

    // === КЛЮЧЕВОЕ ИСПРАВЛЕНИЕ ===
    // 4. Уничтожаем ShaderManager ПЕРЕД уничтожением устройства.
    //    Это вызовет деструкторы всех ShaderProgram и уничтожит все VkShaderModule.
    if (m_shaderManager) m_shaderManager.reset();
    // =============================

    // 5. Уничтожаем "системные" объекты Vulkan
    if (m_commandPoolManager) m_commandPoolManager.reset();
    if (m_swapchain) m_swapchain.reset();

    // 6. В САМОМ КОНЦЕ уничтожаем логическое устройство
    if (m_device) m_device.reset();

    std::cout << "Shutdown complete." << std::endl;
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
        
        // Garbage collect staging buffers periodically - FIXED: use member variable
        if (m_resourceManager) {
            m_resourceManager->GetStagingPool()->GarbageCollect();
        }
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
    // Еще раз убедимся, что GPU не использует эти объекты
    vkDeviceWaitIdle(m_device->GetDevice());

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        // Сначала fence'ы
        if (m_frames[i].inFlightFence != VK_NULL_HANDLE) {
            vkDestroyFence(m_device->GetDevice(), m_frames[i].inFlightFence, nullptr);
            m_frames[i].inFlightFence = VK_NULL_HANDLE;
        }

        // Потом семафоры
        if (m_frames[i].imageAvailableSemaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_device->GetDevice(), m_frames[i].imageAvailableSemaphore, nullptr);
            m_frames[i].imageAvailableSemaphore = VK_NULL_HANDLE;
        }
        if (m_frames[i].renderFinishedSemaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_device->GetDevice(), m_frames[i].renderFinishedSemaphore, nullptr);
            m_frames[i].renderFinishedSemaphore = VK_NULL_HANDLE;
        }
    }
}

void MeadowApp::RecreateSwapchain() {
    // 1. Обработка минимизации окна.
    // Если окно свернуто, его размер 0x0. Ждем, пока пользователь его развернет.
    int width = 0, height = 0;
    while (width == 0 || height == 0) {
        width = GetWindow()->GetWidth();
        height = GetWindow()->GetHeight();
        GetWindow()->PollEvents();
    }

    // 2. ЖДЕМ ПОЛНОГО ЗАВЕРШЕНИЯ РАБОТЫ GPU.
    // Это самый важный и надежный шаг. Он гарантирует, что ни один из старых
    // ресурсов swapchain'а (картинки, буфер глубины) или пайплайнов не используется.
    // Использование vkWaitForFences здесь недостаточно и приводит к ошибкам.
    vkDeviceWaitIdle(m_device->GetDevice());

    // 3. УНИЧТОЖАЕМ ВСЕ ОБЪЕКТЫ, КОТОРЫЕ ЗАВИСЯТ ОТ SWAPCHAIN.
    // Сюда входят все пайплайны и рендеры, так как они создавались
    // с использованием форматов и размеров старого swapchain'а.
    m_skyRenderer.reset();
    m_trianglePipeline.reset();

    // Также сюда могут входить фреймбуферы, если вы их используете.

    // 4. ПЕРЕСОЗДАЕМ БАЗОВЫЕ РЕСУРСЫ.
    // Эта функция уничтожит старый swapchain, его image views, старый depth buffer
    // и создаст новые с актуальными размерами.
    m_swapchain->Recreate(width, height);

    // 5. ПЕРЕСОЗДАЕМ ЗАВИСИМЫЕ ОБЪЕКТЫ.
    // Теперь, когда у нас есть новый swapchain, мы можем заново создать
    // рендеры. Они автоматически подхватят новые форматы и размеры.
    m_trianglePipeline = std::make_unique<RHI::Vulkan::TriangleRenderer>(
        m_device.get(),
        m_shaderManager.get(),
        m_swapchain->GetFormat()
    );

    m_skyRenderer = std::make_unique<Renderer::SkyRenderer>(
        m_device.get(),
        m_shaderManager.get(),
        m_swapchain->GetFormat(),
        m_swapchain->GetDepthFormat()
    );

    // 6. СБРАСЫВАЕМ ФЛАГ.
    m_framebufferResized = false;

    // ПРИМЕЧАНИЕ: Логика с oldImageCount/DestroySyncObjects/CreateSyncObjects
    // больше не нужна в таком виде, так как объекты синхронизации (m_frames)
    // не зависят от swapchain'а напрямую и должны жить в течение всего
    // времени работы приложения. Их количество определяется MAX_FRAMES_IN_FLIGHT,
    // а не количеством картинок в swapchain.
    // Если вам все же нужно пересоздавать командные буферы, это нужно делать
    // отдельно, но уничтожать семафоры и фенсы здесь не следует.
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

    // --- РЕНДЕР НЕБА (ПЕРВЫМ!) ---
    m_device->BeginDebugLabel(cmd, "Sky Pass", { 0.0f, 0.5f, 1.0f, 1.0f });

    // Создаем ПРАВИЛЬНЫЕ матрицы для теста
    float aspectRatio = static_cast<float>(m_swapchain->GetExtent().width) / static_cast<float>(m_swapchain->GetExtent().height);
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspectRatio, 0.1f, 100.0f);
    // Vulkan использует другую систему координат для clip space, Y инвертирована
    proj[1][1] *= -1;
    glm::mat4 view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 invViewProj = glm::inverse(proj * view);
    glm::vec3 cameraPos = glm::vec3(2.0f, 2.0f, 2.0f);

    m_skyRenderer->Render(
        cmd,
        m_swapchain->GetFrame(imageIndex).imageView,
        m_swapchain->GetDepthImageView(), // SkyRenderer'ю нужен и Depth ImageView
        m_swapchain->GetExtent(),
        invViewProj,
        cameraPos
    );

    m_device->EndDebugLabel(cmd);
    
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
    // Wait for previous frame with same index - WITH TIMEOUT HANDLING
    constexpr uint64_t FENCE_TIMEOUT = 1000000000; // 1 second in nanoseconds
    VkResult result = vkWaitForFences(m_device->GetDevice(), 1, 
                                      &m_frames[m_currentFrame].inFlightFence, 
                                      VK_TRUE, FENCE_TIMEOUT);
    if (result == VK_TIMEOUT) {
        std::cerr << "Warning: Fence wait timeout for frame " << m_currentFrame << std::endl;
        // Could implement recovery logic here
        return;
    } else if (result != VK_SUCCESS) {
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