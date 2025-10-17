// meadow_app.cpp 

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
#include "core/core_context.h"
#include "renderer/CubeRenderer.h"
#include "renderer/material_system.h"
#include "renderer/sky_renderer.h"
#include "renderer/skybox_renderer.h"
#include "scene/components.h"


MeadowApp::MeadowApp() 
    : Core::Application({
        .title = "Meadow World - Vulkan 1.4",
        .width = 1400,
        .height = 950,
        .vsync = true,
        .fullscreen = false,
        .workerThreads = std::thread::hardware_concurrency() - 1
    }) {
}

MeadowApp::~MeadowApp() noexcept = default;

void MeadowApp::OnInitialize() {
    std::cout << "Initializing Meadow World..." << std::endl;

    m_window = GetWindow()->GetHandle();

    if (!m_window) {
        throw std::runtime_error("Failed to get window handle!");
    }

    // 1. Initialize Vulkan Device first (это создает instance, surface, physical device, logical device)
    m_device = std::make_unique<RHI::Vulkan::Device>(GetWindow(), true);

    m_coreContext = std::make_unique<Core::CoreContext>(m_device.get());

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

    m_materialSystem = std::make_unique<Renderer::MaterialSystem>(m_device.get());

    // 8. Initialize scene
    InitializeScene();

    // 9. Create depth buffer
    CreateDepthBuffer();

	m_skyboxRenderer = std::make_unique<Renderer::SkyboxRenderer>(
        m_device.get(), m_coreContext.get(), m_shaderManager.get(),
		m_swapchain->GetFormat(), VK_FORMAT_D32_SFLOAT
	);
  
    Renderer::SkyboxRenderer::Quality quality = Renderer::SkyboxRenderer::Quality::High;

	// Загружаем из HDRI файла (ОДИН файл!)
	m_skyboxRenderer->LoadFromHDRI("assets/textures/kloofendal_48d_partly_cloudy_puresky_4k.hdr", static_cast<uint32_t>(quality));

	m_cubeRenderer = std::make_unique<Renderer::CubeRenderer>(
		m_device.get(),
		m_shaderManager.get(),
		m_materialSystem.get(),
		m_resourceManager.get(),
		m_swapchain->GetFormat(),
		VK_FORMAT_D32_SFLOAT
	);

	m_cubeRenderer->SetSkyboxForIBL(
		m_skyboxRenderer->GetSkyboxView(),
		m_skyboxRenderer->GetSkyboxSampler()
	);

    // 6. Create sync objects and command buffers for each frame
    CreateSyncObjects();

    // Set resize callback
    GetWindow()->SetResizeCallback([this](uint32_t, uint32_t) {
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
	std::cout << "TAB    - Switch camera mode (FreeFly / Orbit)" << std::endl;
	std::cout << "ESC    - Exit" << std::endl;
	std::cout << "================" << std::endl;

    std::cout << "\nVulkan Device: " << m_device->GetProperties().deviceName << std::endl;
    std::cout << "Swapchain Images: " << m_swapchain->GetImageCount() << " (Triple Buffering)" << std::endl;
    std::cout << "Frames in Flight: " << MAX_FRAMES_IN_FLIGHT << std::endl;
    std::cout << "Worker Threads: " << GetJobSystem()->GetThreadCount() << std::endl;
}

void MeadowApp::DrowScene(VkCommandBuffer cmd, uint32_t imageIndex) {

	m_device->BeginDebugLabel(cmd, "Sky Pass", { 0.5f, 0.8f, 1.0f, 1.0f });

    m_skyboxRenderer->Render(
        cmd, 
        m_swapchain->GetFrame(imageIndex).imageView,
        m_camera->GetProjectionMatrix(), 
        m_camera->GetViewMatrix(),
        m_swapchain->GetExtent());

	m_device->EndDebugLabel(cmd);


	m_device->BeginDebugLabel(cmd, "Cube Pass", { 0.0f, 1.0f, 0.0f, 1.0f });
	m_cubeRenderer->Render(
		cmd,
		m_swapchain->GetFrame(imageIndex).imageView,
		m_depthBuffer->GetView(),
		m_swapchain->GetExtent(),
		m_cubeTransform->GetMatrix()
	);
	m_device->EndDebugLabel(cmd);
}

void MeadowApp::Update() {
    // Вычисляем delta time
    float currentTime = static_cast<float>(glfwGetTime());
    m_deltaTime = currentTime - m_lastFrameTime;
    m_lastFrameTime = currentTime;

    if (m_deltaTime > 0.05f) {
        m_deltaTime = 0.05f; // clamp
    }


    // Update cube rotation with time
    static float rotation = 0.0f;
    rotation += m_deltaTime * glm::radians(45.0f); // 45 degrees per second
    m_cubeTransform->SetRotationEuler(glm::vec3(rotation * 0.5f, rotation, rotation * 0.3f));
    // Update camera transform

    m_cameraController->Update(m_deltaTime, m_window);

    m_camera->UpdateViewMatrix(*m_cameraTransform);

    std::vector<Renderer::LightData> lights;

    CollectLightData(currentTime);
}

// Сбор данных для передачи в шейдер:
void MeadowApp::CollectLightData(float time) {

    // Анимация точечного света (вращение вокруг куба)
    float radius = 3.0f;
    glm::vec3 lightPos = glm::vec3(
        cos(time * 0.5f) * radius,
        1.5f + sin(time * 0.3f) * 0.5f,  // Плавание вверх-вниз
        sin(time * 0.5f) * radius
    );
    m_pointLightTransform->SetPosition(lightPos);

    std::vector<Renderer::LightData> lights;

    // Sun light
    Renderer::LightData sun;
    sun.direction = m_sunLight->GetDirection();
    sun.color = m_sunLight->GetColor();
    sun.intensity = m_sunLight->GetIntensity();
    sun.type = 0; // Directional
    //sun.shadingModel = static_cast<int>(m_sunLight->GetShadingModel());
    //sun.wrapFactor = m_sunLight->GetWrapFactor();
    //sun.toonSteps = m_sunLight->GetToonSteps();
    //sun.softness = m_sunLight->GetSoftness();
    lights.push_back(sun);

    // Point light
    Renderer::LightData point;
    point.position = m_pointLightTransform->GetPosition();
    point.color = m_pointLight->GetColor();
    point.intensity = m_pointLight->GetIntensity();
    point.range = m_pointLight->GetRange();
    point.type = 1; // Point
    //point.shadingModel = static_cast<int>(m_pointLight->GetShadingModel());
    //point.wrapFactor = m_pointLight->GetWrapFactor();
    //point.toonSteps = m_pointLight->GetToonSteps();
    //point.softness = m_pointLight->GetSoftness();
    lights.push_back(point);

    // Передать в рендерер
    m_cubeRenderer->UpdateUniforms(
        m_camera->GetViewMatrix(),
        m_camera->GetProjectionMatrix(),
        m_cameraTransform->GetPosition(),
        time,  // pass time for shader animation
        lights,
        glm::vec3(0.05f, 0.05f, 0.05f) // ambient
    );
}

void MeadowApp::LoadShaders() {
    std::cout << "Compiling shaders..." << std::endl;

    // Cube shader 
    auto* cubeProgram = m_shaderManager->CreateProgram("Cube");
    cubeProgram->AddStage(VK_SHADER_STAGE_VERTEX_BIT, "assets/shaders/cube.vert.spv");
    cubeProgram->AddStage(VK_SHADER_STAGE_FRAGMENT_BIT, "assets/shaders/cube.frag.spv");
    if (!cubeProgram->Compile()) {
        throw std::runtime_error("Failed to compile Cube shader program!");
    }

    ////////////
	auto* skyBoxProgram = m_shaderManager->CreateProgram("Skybox");
    skyBoxProgram->AddStage(VK_SHADER_STAGE_VERTEX_BIT, "assets/shaders/skybox.vert.spv");
    skyBoxProgram->AddStage(VK_SHADER_STAGE_FRAGMENT_BIT, "assets/shaders/skybox.frag.spv");
	if (!skyBoxProgram->Compile()) {
		throw std::runtime_error("Failed to compile Skybox shader!");
	}

    // Compute shaders

	auto* equirectTocubemapProgram = m_shaderManager->CreateProgram("EquirectToCubemap");
    equirectTocubemapProgram->AddStage(VK_SHADER_STAGE_COMPUTE_BIT, "assets/shaders/equirect_to_cubemap.comp.spv");
	if (!equirectTocubemapProgram->Compile()) {
		throw std::runtime_error("Failed to compile SunTexture compute shader!");
	}
}

void MeadowApp::OnShutdown() {
    std::cout << "Shutting down..." << std::endl;

    if (!m_device) {
        return;
    }

    vkDeviceWaitIdle(m_device->GetDevice());

    // 1. Уничтожаем объекты синхронизации
    DestroySyncObjects();

    if (m_skyboxRenderer) m_skyboxRenderer.reset();

    // 2. Уничтожаем высокоуровневые объекты рендера
    if (m_cubeRenderer) m_cubeRenderer.reset();

    // 3. Уничтожаем сцену
    if (m_cubeTransform) m_cubeTransform.reset();
    if (m_camera) m_camera.reset();

    // 4. Уничтожаем буферы
    if (m_depthBuffer) m_depthBuffer.reset();

    // 5. Уничтожаем менеджеры
    if (m_materialSystem) m_materialSystem.reset();
    if (m_resourceManager) m_resourceManager.reset();

    // 6. ShaderManager перед устройством
    if (m_shaderManager) m_shaderManager.reset();

    if (m_coreContext) m_coreContext.reset();

    // 7. Системные объекты Vulkan
    if (m_commandPoolManager) m_commandPoolManager.reset();
    if (m_swapchain) m_swapchain.reset();

    // 8. В самом конце уничтожаем логическое устройство
    if (m_device) m_device.reset();

    std::cout << "Shutdown complete." << std::endl;
}

void MeadowApp::OnUpdate(float deltaTime) {
    m_totalTime += deltaTime;

    Update();
    
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

    // Destroy old depth buffer
    m_depthBuffer.reset();

    // Destroy renderers
    m_cubeRenderer.reset();

    // Recreate swapchain
    m_swapchain->Recreate(width, height);

    // Update camera aspect ratio
    float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
    m_camera->SetAspectRatio(aspectRatio);

    // Recreate depth buffer
    CreateDepthBuffer();

    // Recreate renderers
    m_cubeRenderer = std::make_unique<Renderer::CubeRenderer>(
        m_device.get(),
        m_shaderManager.get(),
        m_materialSystem.get(),
        m_resourceManager.get(),
        m_swapchain->GetFormat(),
        VK_FORMAT_D32_SFLOAT
    );

	m_cubeRenderer->SetSkyboxForIBL(
		m_skyboxRenderer->GetSkyboxView(),
		m_skyboxRenderer->GetSkyboxSampler()
	);

    // 6. СБРАСЫВАЕМ ФЛАГ.
    m_framebufferResized = false;

    // ПРИМЕЧАНИЕ: Логика с oldImageCount/DestroySyncObjects/CreateSyncObjects
    // больше не нужна в таком виде, так как объекты синхронизации (m_frames)
    // не зависят от swapchain'а напрямую и должны жить в течение всего
    // времени работы приложения. Их количество определяется MAX_FRAMES_IN_FLIGHT,
    // а не количеством картинок в swapchain.
    // Если нужно пересоздавать командные буферы, это нужно делать
    // отдельно, но уничтожать семафоры и фенсы здесь не следует.
}

void MeadowApp::RecordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = 0;

    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));

    // Только transition swapchain image для color attachment
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

    // depth barrier не нужен - dynamic rendering сам переводит depth buffer в нужный layout!

    DrowScene(cmd, imageIndex);


    // Transition to present
    imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    imageBarrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
    imageBarrier.dstAccessMask = 0;
    imageBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    imageBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkDependencyInfo presentDep{};
    presentDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    presentDep.imageMemoryBarrierCount = 1;
    presentDep.pImageMemoryBarriers = &imageBarrier;

    vkCmdPipelineBarrier2(cmd, &presentDep);

    VK_CHECK(vkEndCommandBuffer(cmd));
}

void MeadowApp::CreateDepthBuffer() {
    VkExtent2D extent = m_swapchain->GetExtent();

	RHI::Vulkan::ImageDesc depthDesc{};
	depthDesc.width = extent.width;
	depthDesc.height = extent.height;
	depthDesc.depth = 1;
	depthDesc.arrayLayers = 1;
	depthDesc.mipLevels = 1;
	depthDesc.format = VK_FORMAT_D32_SFLOAT;
	depthDesc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	depthDesc.imageType = VK_IMAGE_TYPE_2D;
	depthDesc.tiling = VK_IMAGE_TILING_OPTIMAL;
	depthDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	depthDesc.samples = VK_SAMPLE_COUNT_1_BIT;
	depthDesc.aspectFlags = VK_IMAGE_ASPECT_DEPTH_BIT;
	depthDesc.memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY;
	depthDesc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	m_depthBuffer = std::make_unique<RHI::Vulkan::Image>(m_device.get(), depthDesc);

    // НЕ НУЖНО делать transition - dynamic rendering сделает это автоматически
    // при первом использовании в vkCmdBeginRendering()

    // Если все же нужно сделать transition (например, для очистки при создании):
    m_depthBuffer->TransitionLayout(
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        m_coreContext.get()
    ); 
}

void MeadowApp::InitializeScene() {
    // Create camera
	m_cameraTransform = std::make_unique<Scene::Transform>();
	m_cameraTransform->SetPosition(glm::vec3(0.0f, 0.0f, 5.0f));
    m_cameraTransform->LookAt(glm::vec3(0.0f, 0.0f, 0.0f));

    m_camera = std::make_unique<Scene::Camera>();
    float aspectRatio = static_cast<float>(GetWindow()->GetWidth()) / static_cast<float>(GetWindow()->GetHeight());
    m_camera->SetPerspective(60.0f, aspectRatio, 0.1f, 1000.0f);

	m_cameraController = std::make_unique<Scene::CameraController>(m_cameraTransform.get());
	m_cameraController->SetMode(Scene::CameraController::Mode::Orbit);
	m_cameraController->SetOrbitTarget(glm::vec3(0.0f, 0.0f, 0.0f));
	m_cameraController->SetOrbitDistance(5.0f);

    // Create cube transform
    m_cubeTransform = std::make_unique<Scene::Transform>();
    m_cubeTransform->SetPosition(glm::vec3(0.0f, 0.0f, 0.0f));
    m_cubeTransform->SetScale(2.0f);

    // Создать направленный свет
	m_sunLight = std::make_unique<Scene::Light>(Scene::Light::Type::Directional);
	m_sunLight->SetColor(glm::vec3(1.0f, 0.95f, 0.8f));
	m_sunLight->SetIntensity(5.0f); 
	m_sunLight->SetDirection(glm::vec3(-0.5f, -1.0f, -0.2f));

    //// === НАСТРОЙКА Half-Lambert ===
    //m_sunLight->SetShadingModel(Scene::Light::ShadingModel::HalfLambert);
    //m_sunLight->SetWrapFactor(0.5f);  // Стандартный Half-Lambert
    //m_sunLight->SetSoftness(1.0f);

    //m_sunTransform = std::make_unique<Scene::Transform>();

    // Точечный свет с Toon shading
    m_pointLight = std::make_unique<Scene::Light>(Scene::Light::Type::Point);
    m_pointLight->SetColor(glm::vec3(1.0f, 1.0f, 1.0f));
    m_pointLight->SetIntensity(5.0f);
    m_pointLight->SetRange(10.0f);

    //// === НАСТРОЙКА Toon shading ===
    //m_pointLight->SetShadingModel(Scene::Light::ShadingModel::Toon);
    //m_pointLight->SetToonSteps(4);    // 4 уровня освещения
    //m_pointLight->SetSoftness(0.1f);  // Резкие переходы

    m_pointLightTransform = std::make_unique<Scene::Transform>();
    m_pointLightTransform->SetPosition(glm::vec3(3.0f, 3.0f, 3.0f));  // Позиция точечного света
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
