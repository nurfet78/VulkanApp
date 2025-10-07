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
#include "renderer/triangle_renderer.h"
#include "core/core_context.h"
#include "renderer/CubeRenderer.h"
#include "renderer/material_system.h"
#include "renderer/sky_renderer.h"
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

    // 5. Create triangle pipeline (требует Device и format от swapchain)
    m_trianglePipeline = std::make_unique<Renderer::TriangleRenderer>(
        m_device.get(),
        m_shaderManager.get(),
        m_swapchain->GetFormat()
    ); 

	m_cameraRotationX = 0.3f;  // ~17 градусов вверх
	m_cameraRotationY = 0.5f;  // ~29 градусов вбок

    // 8. Initialize scene
    InitializeScene();

    // 9. Create depth buffer
    CreateDepthBuffer();

    m_cubeRenderer = std::make_unique<Renderer::CubeRenderer>(
        m_device.get(),
        m_shaderManager.get(),
        m_materialSystem.get(),
        m_resourceManager.get(),
        m_swapchain->GetFormat(),
        VK_FORMAT_D32_SFLOAT
    );

    m_skyRenderer = std::make_unique<Renderer::SkyRenderer>(
        m_device.get(),
        m_coreContext.get(),
        m_shaderManager.get(),
        m_swapchain->GetExtent(),
        m_swapchain->GetFormat(),
        m_swapchain->GetDepthFormat()
    );

	Renderer::SkyParams params;
	params.timeOfDay = 12.0f;  // 2 PM
	params.cloudCoverage = 0.5f;
	params.turbidity = 2.0f;
	m_skyRenderer->SetSkyParams(params);

	// Set quality profile based on GPU
	//m_skyRenderer->SetQualityProfile(Renderer::SkyQualityProfile::High);

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
    std::cout << "ESC    - Exit" << std::endl;
    std::cout << "================" << std::endl;

    std::cout << "\nVulkan Device: " << m_device->GetProperties().deviceName << std::endl;
    std::cout << "Swapchain Images: " << m_swapchain->GetImageCount() << " (Triple Buffering)" << std::endl;
    std::cout << "Frames in Flight: " << MAX_FRAMES_IN_FLIGHT << std::endl;
    std::cout << "Worker Threads: " << GetJobSystem()->GetThreadCount() << std::endl;
}

void MeadowApp::DrowScene(VkCommandBuffer cmd, uint32_t imageIndex) {
	// === RENDER SKY FIRST ===
	m_device->BeginDebugLabel(cmd, "Sky Pass", { 0.5f, 0.8f, 1.0f, 1.0f });

	// Create view matrix without translation (for skybox effect)
	glm::mat4 viewRotationOnly = glm::mat4(glm::mat3(m_camera->GetViewMatrix()));

	m_skyRenderer->Render(
		cmd,
		m_swapchain->GetFrame(imageIndex).imageView,
		m_swapchain->GetFrame(imageIndex).image,
		m_swapchain->GetExtent(),
		m_camera->GetProjectionMatrix(),
		viewRotationOnly,
		m_cameraPos
	);

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

    HandleSkyControls();
    UpdateCamera();

	// Update sky renderer
	if (m_skyRenderer) {
		m_skyRenderer->Update(m_deltaTime);
	}

    // Update cube rotation with time
    static float rotation = 0.0f;
    rotation += m_deltaTime * glm::radians(45.0f); // 45 degrees per second
    m_cubeTransform->SetRotationEuler(glm::vec3(rotation * 0.5f, rotation, rotation * 0.3f));
    // Update camera transform
    Scene::Transform cameraTransform;
    cameraTransform.SetPosition(m_cameraPos);
    cameraTransform.LookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    m_camera->UpdateViewMatrix(cameraTransform);

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
        m_cameraPos,
        time,  // pass time for shader animation
        lights,
        glm::vec3(0.05f, 0.05f, 0.05f) // ambient
    );
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

    // Cube shader 
    auto* cubeProgram = m_shaderManager->CreateProgram("Cube");
    cubeProgram->AddStage(VK_SHADER_STAGE_VERTEX_BIT, "shaders/cube.vert.spv");
    cubeProgram->AddStage(VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/cube.frag.spv");
    if (!cubeProgram->Compile()) {
        throw std::runtime_error("Failed to compile Cube shader program!");
    }

	// === SKY SHADERS ===
	auto* atmosphereProgram = m_shaderManager->CreateProgram("Atmosphere");
	atmosphereProgram->AddStage(VK_SHADER_STAGE_VERTEX_BIT, "shaders/atmosphere.vert.spv");
	atmosphereProgram->AddStage(VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/atmosphere.frag.spv");
	if (!atmosphereProgram->Compile()) {
		throw std::runtime_error("Failed to compile Atmosphere shader!");
	}

	auto* cloudsProgram = m_shaderManager->CreateProgram("Clouds");
    cloudsProgram->AddStage(VK_SHADER_STAGE_VERTEX_BIT, "shaders/clouds.vert.spv");
    cloudsProgram->AddStage(VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/clouds.frag.spv");
	if (!cloudsProgram->Compile()) {
		throw std::runtime_error("Failed to compile Atmosphere shader!");
	}

	auto* starsProgram = m_shaderManager->CreateProgram("Stars");
	starsProgram->AddStage(VK_SHADER_STAGE_VERTEX_BIT, "shaders/stars.vert.spv");
	starsProgram->AddStage(VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/stars.frag.spv");
	if (!starsProgram->Compile()) {
		throw std::runtime_error("Failed to compile Stars shader!");
	}

	auto* postProcessProgram = m_shaderManager->CreateProgram("PostProcess");
	postProcessProgram->AddStage(VK_SHADER_STAGE_VERTEX_BIT, "shaders/postprocess.vert.spv");
	postProcessProgram->AddStage(VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/postprocess.frag.spv");
	if (!postProcessProgram->Compile()) {
		throw std::runtime_error("Failed to compile PostProcess shader!");
	}

	auto* sunBillboardProgram = m_shaderManager->CreateProgram("SunBillboard");
    sunBillboardProgram->AddStage(VK_SHADER_STAGE_VERTEX_BIT, "shaders/sun_billboard.vert.spv");
    sunBillboardProgram->AddStage(VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/sun_billboard.frag.spv");
	if (!sunBillboardProgram->Compile()) {
		throw std::runtime_error("Failed to compile SunBillboard shader!");
	}

	// Compute shaders

	auto* sunTextureProgram = m_shaderManager->CreateProgram("SunTexture");
    sunTextureProgram->AddStage(VK_SHADER_STAGE_COMPUTE_BIT, "shaders/sun_texture.comp.spv");
	if (!sunTextureProgram->Compile()) {
		throw std::runtime_error("Failed to compile SunTexture compute shader!");
	}


	auto* lutProgram = m_shaderManager->CreateProgram("GenerateLUT");
	lutProgram->AddStage(VK_SHADER_STAGE_COMPUTE_BIT, "shaders/generate_lut.comp.spv");
	if (!lutProgram->Compile()) {
		throw std::runtime_error("Failed to compile GenerateLUT compute shader!");
	}

	auto* cloudNoiseProgram = m_shaderManager->CreateProgram("CloudNoise");
	cloudNoiseProgram->AddStage(VK_SHADER_STAGE_COMPUTE_BIT, "shaders/cloud_noise.comp.spv");
	if (!cloudNoiseProgram->Compile()) {
		throw std::runtime_error("Failed to compile CloudNoise compute shader!");
	}

	auto* starGenProgram = m_shaderManager->CreateProgram("GenerateStars");
	starGenProgram->AddStage(VK_SHADER_STAGE_COMPUTE_BIT, "shaders/generate_stars.comp.spv");
	if (!starGenProgram->Compile()) {
		throw std::runtime_error("Failed to compile GenerateStars compute shader!");
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

    if (m_skyRenderer) m_skyRenderer.reset();

    // 2. Уничтожаем высокоуровневые объекты рендера
    if (m_cubeRenderer) m_cubeRenderer.reset();
    if (m_trianglePipeline) m_trianglePipeline.reset();

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
    m_skyRenderer.reset();
    m_cubeRenderer.reset();
    m_trianglePipeline.reset();

    // Recreate swapchain
    m_swapchain->Recreate(width, height);

    // Update camera aspect ratio
    float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
    m_camera->SetAspectRatio(aspectRatio);

    // Recreate depth buffer
    CreateDepthBuffer();

	m_skyRenderer = std::make_unique<Renderer::SkyRenderer>(
		m_device.get(),
		m_coreContext.get(),
		m_shaderManager.get(),
        m_swapchain->GetExtent(),
		m_swapchain->GetFormat(),
		m_swapchain->GetDepthFormat()
	);

	Renderer::SkyParams params;
	params.timeOfDay = 14.0f;
	params.cloudCoverage = 0.6f;
	params.turbidity = 2.5f;
	m_skyRenderer->SetSkyParams(params);
	m_skyRenderer->SetQualityProfile(Renderer::SkyQualityProfile::High);

    // Recreate renderers
    m_cubeRenderer = std::make_unique<Renderer::CubeRenderer>(
        m_device.get(),
        m_shaderManager.get(),
        m_materialSystem.get(),
        m_resourceManager.get(),
        m_swapchain->GetFormat(),
        VK_FORMAT_D32_SFLOAT
    );

    m_trianglePipeline = std::make_unique<Renderer::TriangleRenderer>(
        m_device.get(),
        m_shaderManager.get(),
        m_swapchain->GetFormat()
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
    m_camera = std::make_unique<Scene::Camera>();
    float aspectRatio = static_cast<float>(GetWindow()->GetWidth()) / static_cast<float>(GetWindow()->GetHeight());
    m_camera->SetPerspective(60.0f, aspectRatio, 0.1f, 1000.0f);

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

void MeadowApp::UpdateCamera() {
    // Rotation with mouse (hold left button)
    if (glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
        double xpos, ypos;
        glfwGetCursorPos(m_window, &xpos, &ypos);

        static double lastX = xpos, lastY = ypos;
        static bool firstMouse = true;

        if (firstMouse) {
            lastX = xpos;
            lastY = ypos;
            firstMouse = false;
        }

        double deltaX = xpos - lastX;
        double deltaY = ypos - lastY;

        m_cameraRotationY += static_cast<float>(deltaX * 0.01f);
        m_cameraRotationX += static_cast<float>(deltaY * 0.01f);
        m_cameraRotationX = glm::clamp(m_cameraRotationX, -1.5f, 1.5f);

        lastX = xpos;
        lastY = ypos;
    }

    // Update camera position based on rotation
    float distance = 5.0f;
    m_cameraPos.x = distance * sin(m_cameraRotationY) * cos(m_cameraRotationX);
    m_cameraPos.y = distance * sin(m_cameraRotationX);
    m_cameraPos.z = distance * cos(m_cameraRotationY) * cos(m_cameraRotationX);
}

void MeadowApp::HandleSkyControls() {
    //using namespace Core;
    //// Example: keyboard controls for sky parameters
    //if (Core::Input::IsKeyPressed(GLFW_KEY_1)) {
    //    m_skyRenderer->SetTimeOfDay(6.0f);  // Sunrise
    //}
    //if (Input::IsKeyPressed(GLFW_KEY_2)) {
    //    m_skyRenderer->SetTimeOfDay(12.0f); // Noon
    //}
    //if (Input::IsKeyPressed(GLFW_KEY_3)) {
    //    m_skyRenderer->SetTimeOfDay(18.0f); // Sunset
    //}
    //if (Input::IsKeyPressed(GLFW_KEY_4)) {
    //    m_skyRenderer->SetTimeOfDay(0.0f);  // Midnight
    //}

    //// Cloud control
    //if (Input::IsKeyPressed(GLFW_KEY_C)) {
    //    auto params = m_skyRenderer->GetSkyParams();
    //    params.cloudCoverage = std::min(1.0f, params.cloudCoverage + 0.1f);
    //    m_skyRenderer->SetSkyParams(params);
    //}
    //if (Input::IsKeyPressed(GLFW_KEY_V)) {
    //    auto params = m_skyRenderer->GetSkyParams();
    //    params.cloudCoverage = std::max(0.0f, params.cloudCoverage - 0.1f);
    //    m_skyRenderer->SetSkyParams(params);
    //}

    //// Quality presets
    //if (Input::IsKeyPressed(GLFW_KEY_F1)) {
    //    m_skyRenderer->SetQualityProfile(Renderer::SkyQualityProfile::Low);
    //}
    //if (Input::IsKeyPressed(GLFW_KEY_F2)) {
    //    m_skyRenderer->SetQualityProfile(Renderer::SkyQualityProfile::Medium);
    //}
    //if (Input::IsKeyPressed(GLFW_KEY_F3)) {
    //    m_skyRenderer->SetQualityProfile(Renderer::SkyQualityProfile::High);
    //}
    //if (Input::IsKeyPressed(GLFW_KEY_F4)) {
    //    m_skyRenderer->SetQualityProfile(Renderer::SkyQualityProfile::Ultra);
    //}

    //// Animation control
    //if (Input::IsKeyPressed(GLFW_KEY_SPACE)) {
    //    m_skyRenderer->SetAutoAnimate(!m_skyRenderer->GetAutoAnimate());
    //}
}