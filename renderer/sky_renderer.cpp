// engine/renderer/sky_renderer.cpp
#include "sky_renderer.h"
#include "rhi/vulkan/device.h"
#include "rhi/vulkan/shader_manager.h"
#include "rhi/vulkan/resource.h"
#include "rhi/vulkan/descriptor_allocator.h"

namespace Renderer {

    SkyRenderer::SkyRenderer(RHI::Vulkan::Device* device,
        RHI::Vulkan::ShaderManager* shaderManager,
        VkFormat colorFormat,
        VkFormat depthFormat)
        : m_device(device)
        , m_shaderManager(shaderManager)
    {
        CreateUniformBuffer();      // Создаём uniform buffer
        CreateDescriptorSet();      // Создаём descriptor set
        CreatePipeline(colorFormat, depthFormat);
        CreateIBLResources();
        UpdateSunPosition();
        UpdateUniformBuffer();      // Заполняем uniform buffer начальными данными
    }

    SkyRenderer::~SkyRenderer() {
        // 1. Сброс pipeline
        m_pipeline.reset(); // умный указатель, вызовет Destroy() ReloadablePipeline

        // 2. Сброс uniform buffer
        m_uniformBuffer.reset();

        // 3. Дестрой дескрипт-сета и layout
        // Descriptor set освобождается пулом, его уничтожать не нужно
        if (m_descriptorSetLayout) {
            vkDestroyDescriptorSetLayout(m_device->GetDevice(), m_descriptorSetLayout, nullptr);
            m_descriptorSetLayout = VK_NULL_HANDLE;
        }
    }

    void SkyRenderer::CreateUniformBuffer() {
        // Создаём uniform buffer для хранения цветовых параметров
        VkDeviceSize bufferSize = sizeof(SkyUniformData);

        m_uniformBuffer = std::make_unique<RHI::Vulkan::Buffer>(
            m_device,
            bufferSize,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU
        );
    }

    void SkyRenderer::CreateDescriptorSet() {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m_uniformBuffer->GetHandle();
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(SkyUniformData);

        // Используем DescriptorBuilder
        bool ok = RHI::Vulkan::DescriptorBuilder::Begin(
            m_device->GetDescriptorLayoutCache(),
            m_device->GetDescriptorAllocator())
            .BindBuffer(0, &bufferInfo,
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                VK_SHADER_STAGE_FRAGMENT_BIT)
            .Build(m_descriptorSet, m_descriptorSetLayout);

        if (!ok) {
            throw std::runtime_error("SkyRenderer: failed to build descriptor set!");
        }
    }

    void SkyRenderer::CreatePipeline(VkFormat colorFormat, VkFormat depthFormat) {
        RHI::Vulkan::ReloadablePipeline::CreateInfo pipelineInfo{};

        pipelineInfo.shaderProgram = "Sky";

        // Push constants - теперь меньшего размера!
        pipelineInfo.pushConstantSize = sizeof(SkyPushConstants);
        pipelineInfo.pushConstantStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        pipelineInfo.descriptorLayouts = &m_descriptorSetLayout;  // указатель на один layout
        pipelineInfo.descriptorLayoutCount = 1;                   // обязательно количество


        pipelineInfo.colorFormat = colorFormat;
        pipelineInfo.depthFormat = depthFormat;

        pipelineInfo.depthTestEnable = true;
        pipelineInfo.depthWriteEnable = false;
        pipelineInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        pipelineInfo.cullMode = VK_CULL_MODE_NONE;

        pipelineInfo.vertexBindings = {};
        pipelineInfo.vertexAttributes = {};

        m_pipeline = std::make_unique<RHI::Vulkan::ReloadablePipeline>(m_device, m_shaderManager);
        if (!m_pipeline->Create(pipelineInfo)) {
            throw std::runtime_error("Failed to create sky pipeline!");
        }
    }

    void SkyRenderer::UpdateUniformBuffer() {

        SkyUniformData ubo{};
        ubo.dayHorizonColor = glm::vec4(m_skyParams.dayHorizonColor, 1.0f);
        ubo.dayZenithColor = glm::vec4(m_skyParams.dayZenithColor, 1.0f);
        ubo.sunsetHorizonColor = glm::vec4(m_skyParams.sunsetHorizonColor, 1.0f);
        ubo.sunsetZenithColor = glm::vec4(m_skyParams.sunsetZenithColor, 1.0f);
        ubo.nightHorizonColor = glm::vec4(m_skyParams.nightHorizonColor, 1.0f);
        ubo.nightZenithColor = glm::vec4(m_skyParams.nightZenithColor, 1.0f);
        ubo.groundColor = glm::vec4(m_skyParams.groundColor, 1.0f);
        ubo.sunColor = glm::vec4(m_skyParams.sunColor, 1.0f);

        m_uniformBuffer->Upload(&ubo, sizeof(ubo));
    }

    void SkyRenderer::CreateIBLResources() {
        // Environment map (cube map) для отражений
        m_environmentMap = std::make_unique<RHI::Vulkan::Image>(
            m_device, 512, 512, VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT
        );

        // Irradiance map для диффузного IBL
        m_irradianceMap = std::make_unique<RHI::Vulkan::Image>(
            m_device, 32, 32, VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT
        );

        // Prefilter map для зеркального IBL
        m_prefilterMap = std::make_unique<RHI::Vulkan::Image>(
            m_device, 128, 128, VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT
        );

        // BRDF LUT
        m_brdfLUT = std::make_unique<RHI::Vulkan::Image>(
            m_device, 512, 512, VK_FORMAT_R16G16_SFLOAT,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT
        );
    }

    void SkyRenderer::UpdateSunPosition() {
        // Вычисляем положение солнца на основе времени дня
        float sunAngle = (m_timeOfDay - 6.0f) * glm::pi<float>() / 12.0f;
        m_skyParams.sunDirection = glm::normalize(glm::vec3(
            std::cos(sunAngle),
            std::sin(sunAngle),
            -0.3f
        ));
    }

    void SkyRenderer::Update(float deltaTime) {
        // Обновляем время анимации облаков
        m_cloudAnimationTime += deltaTime;

        // Автоматическая анимация времени дня
        if (m_autoAnimate) {
            m_timeOfDay += deltaTime * m_animationSpeed;
            if (m_timeOfDay >= 24.0f) {
                m_timeOfDay -= 24.0f;
            }
            UpdateSunPosition();
        }
    }

    void SkyRenderer::Render(VkCommandBuffer cmd,
        VkImageView targetImageView,
        VkImageView depthImageView,
        VkExtent2D extent,
        const glm::mat4& projection,
        const glm::mat4& viewRotationOnly,
        const glm::vec3& cameraPos) {
        // Настройка рендеринга
        VkRenderingAttachmentInfo colorAttachment{};
        colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachment.imageView = targetImageView;
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        VkRenderingAttachmentInfo depthAttachment{};
        depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAttachment.imageView = depthImageView;
        depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        VkRenderingInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderingInfo.renderArea.extent = extent;
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &colorAttachment;
        renderingInfo.pDepthAttachment = &depthAttachment;

        vkCmdBeginRendering(cmd, &renderingInfo);

        // Viewport и scissor
        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(extent.width);
        viewport.height = static_cast<float>(extent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.extent = extent;
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        // Заполняем УМЕНЬШЕННЫЕ push constants
        SkyPushConstants pc{};
        pc.invProjection = glm::inverse(projection);
        pc.invView = glm::inverse(viewRotationOnly);
        pc.cameraPosAndTime = glm::vec4(cameraPos, m_cloudAnimationTime);
        pc.sunDirAndIntensity = glm::vec4(m_skyParams.sunDirection, m_skyParams.sunIntensity);
        pc.skyParams1 = glm::vec4(m_timeOfDay, m_skyParams.cloudCoverage, m_skyParams.cloudSpeed, m_skyParams.cloudScale);
        pc.skyParams2 = glm::vec4(m_skyParams.atmosphereDensity, m_skyParams.sunSize, m_skyParams.starIntensity, m_skyParams.horizonSharpness);
        pc.resolution = glm::vec2(extent.width, extent.height);

        vkCmdPushConstants(cmd, m_pipeline->GetLayout(),
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(SkyPushConstants), &pc);

        // Привязываем descriptor set с uniform buffer
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_pipeline->GetLayout(), 0, 1, &m_descriptorSet, 0, nullptr);

        // Рисуем
        if (m_pipeline) {
            m_pipeline->Bind(cmd);
            vkCmdDraw(cmd, 3, 1, 0, 0);
        }

        vkCmdEndRendering(cmd);
    }

    void SkyRenderer::ConfigureSky() {
        SkyParams skyParams;

        // Настройка времени дня (например, полдень)
        SetTimeOfDay(12.0f);

        // Или включаем автоматическую анимацию времени
        // m_skyRenderer->SetAutoAnimate(true);
        // m_skyRenderer->SetAnimationSpeed(0.5f); // 0.5 часа в секунду

        // Настройка параметров неба
        skyParams.sunIntensity = 3.0f;
        skyParams.sunSize = 64.0f;

        // Цвета дня
        skyParams.dayHorizonColor = glm::vec3(0.6f, 0.75f, 0.9f);
        skyParams.dayZenithColor = glm::vec3(0.1f, 0.3f, 0.7f);

        // Цвета заката
        skyParams.sunsetHorizonColor = glm::vec3(0.9f, 0.5f, 0.3f);
        skyParams.sunsetZenithColor = glm::vec3(0.3f, 0.2f, 0.5f);

        // Цвета ночи
        skyParams.nightHorizonColor = glm::vec3(0.02f, 0.02f, 0.05f);
        skyParams.nightZenithColor = glm::vec3(0.0f, 0.0f, 0.02f);

        // Облака
        skyParams.cloudCoverage = 0.4f;     // 40% покрытие облаками
        skyParams.cloudSpeed = 0.02f;       // Скорость движения
        skyParams.cloudScale = 3.0f;        // Масштаб
        skyParams.cloudHeight = 0.3f;       // Высота слоя облаков

        // Атмосфера
        skyParams.atmosphereDensity = 1.5f; // Плотность атмосферы
        skyParams.horizonSharpness = 3.0f;  // Резкость горизонта
        skyParams.starIntensity = 0.5f;     // Яркость звёзд

        SetSkyParams(skyParams);
    }

    void SkyRenderer::GenerateEnvironmentMaps(VkCommandBuffer cmd) {
        // TODO: Реализация генерации IBL карт
        // 1. Рендерим небо в кубмап
        // 2. Генерируем irradiance map (свёртка для диффузного освещения)
        // 3. Генерируем prefilter map (свёртка для зеркального освещения)
        // 4. Генерируем BRDF LUT
    }

    VkImageView SkyRenderer::GetIrradianceMap() const {
        return m_irradianceMap ? m_irradianceMap->GetView() : VK_NULL_HANDLE;
    }

    VkImageView SkyRenderer::GetPrefilterMap() const {
        return m_prefilterMap ? m_prefilterMap->GetView() : VK_NULL_HANDLE;
    }

    VkImageView SkyRenderer::GetBRDFLUT() const {
        return m_brdfLUT ? m_brdfLUT->GetView() : VK_NULL_HANDLE;
    }

    glm::vec3 SkyRenderer::GetCurrentSkyColor(const glm::vec3& direction) const {
        return CalculateSkyColor(direction);
    }

    glm::vec3 SkyRenderer::GetAmbientLight() const {
        // Возвращаем усреднённый цвет неба для амбиентного освещения
        glm::vec3 upColor = CalculateSkyColor(glm::vec3(0, 1, 0));
        glm::vec3 horizonColor = CalculateSkyColor(glm::vec3(1, 0, 0));
        return glm::mix(horizonColor, upColor, 0.5f) * 0.3f;
    }

    glm::vec3 SkyRenderer::CalculateSkyColor(const glm::vec3& rayDir) const {
        float y = rayDir.y * 0.5f + 0.5f;

        // Интерполяция цветов в зависимости от времени
        float dayFactor = glm::smoothstep(6.0f, 9.0f, m_timeOfDay) *
            glm::smoothstep(21.0f, 18.0f, m_timeOfDay);
        float sunsetFactor = glm::max(
            glm::smoothstep(5.0f, 7.0f, m_timeOfDay) * glm::smoothstep(9.0f, 7.0f, m_timeOfDay),
            glm::smoothstep(17.0f, 19.0f, m_timeOfDay) * glm::smoothstep(21.0f, 19.0f, m_timeOfDay)
        );
        float nightFactor = 1.0f - glm::max(dayFactor, sunsetFactor);

        glm::vec3 horizonColor = m_skyParams.dayHorizonColor * dayFactor +
            m_skyParams.sunsetHorizonColor * sunsetFactor +
            m_skyParams.nightHorizonColor * nightFactor;
        glm::vec3 zenithColor = m_skyParams.dayZenithColor * dayFactor +
            m_skyParams.sunsetZenithColor * sunsetFactor +
            m_skyParams.nightZenithColor * nightFactor;

        glm::vec3 skyColor;
        if (rayDir.y > 0.0f) {
            skyColor = glm::mix(horizonColor, zenithColor, glm::pow(y, m_skyParams.atmosphereDensity));
        }
        else {
            skyColor = glm::mix(horizonColor, m_skyParams.groundColor, glm::pow(-rayDir.y, 2.0f));
        }

        // Солнце
        float sun = glm::pow(glm::max(glm::dot(rayDir, m_skyParams.sunDirection), 0.0f), m_skyParams.sunSize);
        skyColor += m_skyParams.sunColor * sun * m_skyParams.sunIntensity * glm::max(dayFactor, sunsetFactor * 0.7f);

        return skyColor;
    }

    void SkyRenderer::RecreateSwapchainResources() {
        // 1. Ждем окончания работы GPU
        vkDeviceWaitIdle(m_device->GetDevice());

        // 2. Сбрасываем allocator (освобождаем старые descriptor sets)
        m_device->GetDescriptorAllocator()->ResetPools(); 

        // 3. Создаем descriptor set заново
        CreateDescriptorSet();
    }


} // namespace Renderer