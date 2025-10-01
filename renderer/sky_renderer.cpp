// SkyRenderer.cpp - Implementation
#include "sky_renderer.h"
#include "rhi/vulkan/device.h"
#include "rhi/vulkan/resource.h"
#include "rhi/vulkan/descriptor_allocator.h"
#include "rhi/vulkan/command_pool.h"
#include "rhi/vulkan/compute_pipeline.h"
#include "core/core_context.h"

namespace Renderer {

    // Quality presets
    struct QualitySettings {
        uint32_t skyResolution;
        uint32_t cloudResolution;
        uint32_t starResolution;
        int atmosphereSamples;
        int cloudSamples;
        bool volumetricClouds;
        bool temporalAccumulation;
        int cloudOctaves;
    };

    static const QualitySettings g_qualityPresets[] = {
        {512,  256,  512,  8,  16, false, false, 2}, // Low
        {1024, 512,  1024, 16, 32, true,  false, 3}, // Medium
        {2048, 1024, 2048, 32, 64, true,  true,  4}, // High
        {4096, 2048, 4096, 64, 128, true, true,  5}  // Ultra
    };

    SkyRenderer::SkyRenderer(RHI::Vulkan::Device* device,
        Core::CoreContext* context,
        RHI::Vulkan::ShaderManager* shaderManager,
        VkFormat colorFormat,
        VkFormat depthFormat)
        : m_device(device)
        , m_context(context)
        , m_shaderManager(shaderManager)
        , m_colorFormat(colorFormat) 
        , m_depthFormat(depthFormat) {

        // Initialize default cloud layers
        m_cloudLayers.push_back({ 2000.0f, 500.0f, 0.5f, 0.1f, 1.0f, 0 }); // Cumulus
        m_cloudLayers.push_back({ 8000.0f, 200.0f, 0.3f, 0.2f, 2.0f, 2 }); // Cirrus

        // Create resources
        CreateUniformBuffers();
        CreateTextures();
        CreateRenderTargets();
        CreateSamplers();
        CreateLUTs();

        // Create pipelines
        CreateAtmospherePipeline();
        CreateCloudPipeline();
        CreateStarsPipeline();
        CreatePostProcessPipeline();
        CreateComputePipelines();

        // Create descriptor sets
        CreateDescriptorSets();

        // Initial setup
        SetTimeOfDay(12.0f);
        UpdateAtmosphere();

        m_needsCloudGeneration = true;
        m_needsLUTGeneration = true;
        m_needsStarGeneration = true;

        // Generate initial procedural content
		VkCommandBuffer cmd = context->GetCommandPoolManager()->BeginSingleTimeCommandsCompute();
		GenerateAtmosphereLUT(cmd);
		GenerateCloudNoise(cmd);
		GenerateStarTexture(cmd);
		context->GetCommandPoolManager()->EndSingleTimeCommandsCompute(cmd);
    }

    SkyRenderer::~SkyRenderer() {
       
        vkDeviceWaitIdle(m_device->GetDevice());


        // Cleanup samplers
        if (m_linearSampler) vkDestroySampler(m_device->GetDevice(), m_linearSampler, nullptr);
        if (m_nearestSampler) vkDestroySampler(m_device->GetDevice(), m_nearestSampler, nullptr);
        if (m_cloudSampler) vkDestroySampler(m_device->GetDevice(), m_cloudSampler, nullptr);

        // Cleanup descriptor resources
        if (m_descriptorPool) vkDestroyDescriptorPool(m_device->GetDevice(), m_descriptorPool, nullptr);
        if (m_atmosphereDescSetLayout) vkDestroyDescriptorSetLayout(m_device->GetDevice(), m_atmosphereDescSetLayout, nullptr);
        if (m_cloudDescSetLayout) vkDestroyDescriptorSetLayout(m_device->GetDevice(), m_cloudDescSetLayout, nullptr);
        if (m_starsDescSetLayout) vkDestroyDescriptorSetLayout(m_device->GetDevice(), m_starsDescSetLayout, nullptr);
        if (m_postProcessDescSetLayout) vkDestroyDescriptorSetLayout(m_device->GetDevice(), m_postProcessDescSetLayout, nullptr);
    }

    void SkyRenderer::Render(VkCommandBuffer cmd,
        VkImageView targetImageView,
        VkImage targetImage,
        VkExtent2D extent,
        const glm::mat4& projection,
        const glm::mat4& viewRotationOnly,
        const glm::vec3& cameraPos) {

        // Update render state
        m_currentExtent = extent;
        m_currentProjection = projection;
        m_currentView = viewRotationOnly;
        m_currentCameraPos = cameraPos;

        VkImageMemoryBarrier resetBarriers[2] = {};
        VkImageSubresourceRange subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

        // Сброс Sky Buffer
        resetBarriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        resetBarriers[0].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        resetBarriers[0].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        resetBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        resetBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        resetBarriers[0].image = m_skyBuffer->GetHandle();
        resetBarriers[0].subresourceRange = subresourceRange;
        resetBarriers[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT; // Завершили чтение в шейдере (в прошлом кадре)
        resetBarriers[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; // Начинаем запись (в этом кадре)

        // Сброс Cloud Buffer
        resetBarriers[1] = resetBarriers[0];
        resetBarriers[1].image = m_cloudBuffer->GetHandle();

        vkCmdPipelineBarrier(cmd,
            // Ждем завершения чтения в фрагментном шейдере (прошлого кадра)
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            // Перед началом этапа вывода в цветовое вложение (этого кадра)
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0, 0, nullptr, 0, nullptr, 2, resetBarriers);

        // Update uniform buffers
        UpdateUniformBuffers();

        //if (m_needsLUTGeneration || m_needsCloudGeneration || m_needsStarGeneration) {
        //    VkCommandBuffer cmd = m_context->GetCommandPoolManager()->BeginSingleTimeCommandsCompute();
        //    if (m_needsLUTGeneration) { GenerateAtmosphereLUT(cmd); m_needsLUTGeneration = false; }
        //     if (m_needsStarGeneration) { GenerateStarTexture(cmd); m_needsStarGeneration = false; }
        //                // Облака генерируем в последнюю очередь (самые медленные)
        //        if (m_needsCloudGeneration) { GenerateCloudNoise(cmd); m_needsCloudGeneration = false; }
        //     m_context->GetCommandPoolManager()->EndSingleTimeCommandsCompute(cmd);
        //    
        //       return; // Пропускаем рендеринг в этом кадре
        //}

        // Begin timing
        auto startTime = std::chrono::high_resolution_clock::now();

        RenderAtmosphere(cmd);

        auto atmosphereTime = std::chrono::high_resolution_clock::now();
        m_stats.atmospherePassMs = std::chrono::duration<float, std::milli>(atmosphereTime - startTime).count();

        // Render stars (only at night)
        float dayNightBlend = glm::smoothstep(0.3f, 0.7f,
            glm::dot(m_skyParams.sunDirection, glm::vec3(0, 1, 0)));
        if (dayNightBlend < 0.9f) {
            RenderStars(cmd);
            RenderMoon(cmd);
        }

        auto starsTime = std::chrono::high_resolution_clock::now();
        m_stats.starPassMs = std::chrono::duration<float, std::milli>(starsTime - atmosphereTime).count();

        // Render volumetric clouds
        if (m_skyParams.cloudCoverage > 0.01f) {
            //RenderClouds(cmd);
        }

        auto cloudsTime = std::chrono::high_resolution_clock::now();
        m_stats.cloudPassMs = std::chrono::duration<float, std::milli>(cloudsTime - starsTime).count();

        // Post-processing and composite to target
        PostProcess(cmd, targetImageView, targetImage);

        auto endTime = std::chrono::high_resolution_clock::now();
        m_stats.postProcessMs = std::chrono::duration<float, std::milli>(endTime - cloudsTime).count();
        m_stats.totalMs = std::chrono::duration<float, std::milli>(endTime - startTime).count();

        // Update temporal accumulation state
        m_previousViewProj = projection * viewRotationOnly;
        m_frameIndex++;
    }

    void SkyRenderer::RenderAtmosphere(VkCommandBuffer cmd) {

        // Clear sky buffer
        VkClearValue clearValue = {};
        clearValue.color = { {0.0f, 0.0f, 0.0f, 0.0f} };

        // Render atmosphere pass
        {
            VkRenderingAttachmentInfo colorAttachment = {};
            colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            colorAttachment.imageView = m_skyBuffer->GetView();
            colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            colorAttachment.clearValue = clearValue;

            VkRenderingInfo renderInfo = {};
            renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            renderInfo.renderArea = { 0, 0, m_currentExtent.width, m_currentExtent.height };
            renderInfo.layerCount = 1;
            renderInfo.colorAttachmentCount = 1;
            renderInfo.pColorAttachments = &colorAttachment;

            vkCmdBeginRendering(cmd, &renderInfo);

            // Bind atmosphere pipeline
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_atmospherePipeline->GetPipeline());
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                m_atmospherePipeline->GetLayout(),
                0, 1, &m_atmosphereDescSet, 0, nullptr);

            VkViewport viewport{};
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = (float)m_currentExtent.width;
            viewport.height = (float)m_currentExtent.height;
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(cmd, 0, 1, &viewport);

            VkRect2D scissor{};
            scissor.offset = { 0, 0 };
            scissor.extent = m_currentExtent;
            vkCmdSetScissor(cmd, 0, 1, &scissor);

            // Push constants for camera matrices
            struct PushConstants {
                glm::mat4 invViewProj;
                glm::vec3 cameraPos;
                float time;
            } pushConstants;

            pushConstants.invViewProj = glm::inverse(m_currentProjection * m_currentView);
            pushConstants.cameraPos = m_currentCameraPos;
            pushConstants.time = m_currentTime;

            vkCmdPushConstants(cmd, m_atmospherePipeline->GetLayout(),
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(pushConstants), &pushConstants);

            // Draw fullscreen quad
            vkCmdDraw(cmd, 3, 1, 0, 0);

            vkCmdEndRendering(cmd);
        }
    }

    void SkyRenderer::RenderClouds(VkCommandBuffer cmd) {
        // Transition cloud buffer for rendering
        VkImageMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = m_cloudBuffer->GetHandle();
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);

        // Setup rendering
        VkRenderingAttachmentInfo colorAttachment = {};
        colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachment.imageView = m_cloudBuffer->GetView();
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.clearValue.color = { {0.0f, 0.0f, 0.0f, 0.0f} };

        VkRenderingInfo renderInfo = {};
        renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderInfo.renderArea = { 0, 0, m_currentExtent.width, m_currentExtent.height };
        renderInfo.layerCount = 1;
        renderInfo.colorAttachmentCount = 1;
        renderInfo.pColorAttachments = &colorAttachment;

        vkCmdBeginRendering(cmd, &renderInfo);

        // Bind cloud pipeline
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_cloudPipeline->GetPipeline());
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_cloudPipeline->GetLayout(),
            0, 1, &m_cloudDescSet, 0, nullptr);

        // Cloud push constants
        CloudPushConstants cloudPush{};

        cloudPush.invViewProj = glm::inverse(m_currentProjection * m_currentView);
        cloudPush.cameraPos = m_currentCameraPos;
        cloudPush.time = m_cloudAnimationTime;
        cloudPush.sunDirection = m_skyParams.sunDirection;
        cloudPush.coverage = m_skyParams.cloudCoverage;
        cloudPush.windDirection = glm::vec3(1, 0, 0.3f) * m_skyParams.cloudSpeed;
        cloudPush.cloudScale = m_skyParams.cloudScale;

        vkCmdPushConstants(cmd, m_cloudPipeline->GetLayout(),
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(cloudPush), &cloudPush);

        // Draw fullscreen quad
        vkCmdDraw(cmd, 3, 1, 0, 0);

        vkCmdEndRendering(cmd);
    }

    void SkyRenderer::RenderStars(VkCommandBuffer cmd) {
        // Additive blending for stars over sky buffer
        VkRenderingAttachmentInfo colorAttachment = {};
        colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachment.imageView = m_skyBuffer->GetView();
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        VkRenderingInfo renderInfo = {};
        renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderInfo.renderArea = { 0, 0, m_currentExtent.width, m_currentExtent.height };
        renderInfo.layerCount = 1;
        renderInfo.colorAttachmentCount = 1;
        renderInfo.pColorAttachments = &colorAttachment;

        vkCmdBeginRendering(cmd, &renderInfo);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_starsPipeline->GetPipeline());
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_starsPipeline->GetLayout(),
            0, 1, &m_starsDescSet, 0, nullptr);

        StarsPushConstants starsPush{};

        float dayNightBlend = glm::smoothstep(0.3f, 0.7f,
            glm::dot(m_skyParams.sunDirection, glm::vec3(0, 1, 0)));

        starsPush.invViewProj = glm::inverse(m_currentProjection * m_currentView);
        starsPush.intensity = m_skyParams.starIntensity * (1.0f - dayNightBlend);
        starsPush.twinkle = 0.3f;
        starsPush.time = m_currentTime;
        starsPush.nightBlend = 1.0f - dayNightBlend;

        vkCmdPushConstants(cmd, m_starsPipeline->GetLayout(),
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(starsPush), &starsPush);

        vkCmdDraw(cmd, 3, 1, 0, 0);

        vkCmdEndRendering(cmd);
    }

    void SkyRenderer::RenderMoon(VkCommandBuffer cmd) {

    }

    void SkyRenderer::PostProcess(VkCommandBuffer cmd, VkImageView targetView, VkImage targetImage) {

        // Нам нужно 2 барьера: для sky-буфера и для cloud-буфера.
        VkImageMemoryBarrier barriers[2] = {};

        // Создадим один subresource range для всех наших цветных изображений
        // (1 мип-уровень, 1 слой массива).
        VkImageSubresourceRange subresourceRange = {};
        subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresourceRange.baseMipLevel = 0;
        subresourceRange.levelCount = 1;
        subresourceRange.baseArrayLayer = 0;
        subresourceRange.layerCount = 1;

        // Барьер 0: Sky buffer как input.
        // Переводим его из состояния "цель для рендера" в состояние "текстура для чтения".
        barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barriers[0].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[0].image = m_skyBuffer->GetHandle();
        barriers[0].subresourceRange = subresourceRange;
        barriers[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; // Завершили запись
        barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;            // Начинаем чтение

        // Барьер 1: Cloud buffer как input.
        barriers[1] = barriers[0]; // Копируем настройки
        barriers[1].image = m_cloudBuffer->GetHandle();

        // Выполняем барьер для подготовки наших 2-х текстур к чтению
        vkCmdPipelineBarrier(cmd,
            // Ждем завершения записи в цветовое вложение
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            // Перед тем как фрагментный шейдер начнет из них читать
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            2, barriers); // <<-- Управляем только 2 барьерами

        VkRenderingAttachmentInfo colorAttachment = {};
        colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachment.imageView = targetView;
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        VkRenderingInfo renderInfo = {};
        renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderInfo.renderArea = { 0, 0, m_currentExtent.width, m_currentExtent.height };
        renderInfo.layerCount = 1;
        renderInfo.colorAttachmentCount = 1;
        renderInfo.pColorAttachments = &colorAttachment;

        vkCmdBeginRendering(cmd, &renderInfo);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_postProcessPipeline->GetPipeline());
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_postProcessPipeline->GetLayout(),
            0, 1, &m_postProcessDescSet, 0, nullptr);

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = (float)m_currentExtent.width;
        viewport.height = (float)m_currentExtent.height;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = { 0, 0 };
        scissor.extent = m_currentExtent;
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        PostProcessPushConstants postPush{};

        postPush.exposure = m_skyParams.exposure;
        postPush.bloomThreshold = m_skyParams.bloomThreshold;
        postPush.bloomIntensity = m_skyParams.bloomIntensity;
        postPush.time = m_currentTime;

        vkCmdPushConstants(cmd, m_postProcessPipeline->GetLayout(),
            VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(postPush), &postPush);

        vkCmdDraw(cmd, 3, 1, 0, 0);

        vkCmdEndRendering(cmd);
    }

    void SkyRenderer::Update(float deltaTime) {
        if (m_autoAnimate) {
            m_currentTime += deltaTime * m_animationSpeed;

            // Update time of day
            float hoursPerSecond = 0.5f * m_animationSpeed;
            SetTimeOfDay(fmod(m_skyParams.timeOfDay + deltaTime * hoursPerSecond, 24.0f));
        }

        // Update cloud animation
        m_cloudAnimationTime += deltaTime * m_skyParams.cloudSpeed;

        // Update uniform buffers
        UpdateUniformBuffers();
    }

    void SkyRenderer::SetTimeOfDay(float hours) {
        m_skyParams.timeOfDay = fmod(hours, 24.0f);
        UpdateSunMoonPositions();
        UpdateAtmosphere();
    }

    void SkyRenderer::UpdateSunMoonPositions() {
        // Simple sun position based on time of day
        float sunAngle = static_cast<float>((m_skyParams.timeOfDay / 24.0f) * 2.0f * std::numbers::pi - std::numbers::pi / 2.0f);

        m_skyParams.sunDirection = glm::normalize(glm::vec3(
            0.0f,
            sin(sunAngle),
            cos(sunAngle)
        ));

        // Adjust sun intensity based on position
        float sunElevation = m_skyParams.sunDirection.y;
        m_skyParams.sunIntensity = glm::max(0.0f, sunElevation) * 10.0f;
    }

    void SkyRenderer::UpdateAtmosphere() {
        // Update atmospheric parameters based on time of day
        float sunElevation = m_skyParams.sunDirection.y;

        // Adjust turbidity for sunset/sunrise
        if (abs(sunElevation) < 0.3f) {
            m_skyParams.turbidity = glm::mix(2.0f, 5.0f, 1.0f - abs(sunElevation) / 0.3f);
        }
        else {
            m_skyParams.turbidity = 2.0f;
        }

        // Adjust Mie coefficient for sun glare
        m_skyParams.mieCoeff = glm::mix(1.0f, 2.0f, glm::max(0.0f, 1.0f - abs(sunElevation)));
    }

    void SkyRenderer::SetQualityProfile(SkyQualityProfile profile) {
        m_qualityProfile = profile;
        const auto& settings = g_qualityPresets[static_cast<int>(profile)];

        // Recreate textures with new resolutions
        //CreateTextures();
        //CreateRenderTargets();

        // Update cloud parameters
        m_skyParams.cloudOctaves = settings.cloudOctaves;
        m_useTemporalAccumulation = settings.temporalAccumulation;

        //UpdateDescriptorSets(); // только vkUpdateDescriptorSets, без нового пула

        //// Regenerate procedural content
        //VkCommandBuffer cmd = m_context->GetCommandPoolManager()->BeginSingleTimeCommandsCompute();
        //GenerateAtmosphereLUT(cmd);
        //GenerateCloudNoise(cmd);
        //GenerateStarTexture(cmd);
        //m_context->GetCommandPoolManager()->EndSingleTimeCommandsCompute(cmd);
    }

    glm::vec3 SkyRenderer::GetCurrentSkyColor(const glm::vec3& direction) const {
        // Compute sky color for a given direction
        glm::vec3 color = CalculateRayleighScattering(direction);
        color += CalculateMieScattering(direction);

        // Add stars if night
        float dayNightBlend = glm::smoothstep(0.3f, 0.7f,
            glm::dot(m_skyParams.sunDirection, glm::vec3(0, 1, 0)));
        if (dayNightBlend < 0.9f) {
            // Simple star contribution
            float starNoise = glm::fract(sin(glm::dot(direction, glm::vec3(12.9898f, 78.233f, 45.164f))) * 43758.5453f);
            if (starNoise > 0.99f) {
                color += glm::vec3(1.0f) * m_skyParams.starIntensity * (1.0f - dayNightBlend);
            }
        }

        return color;
    }

    glm::vec3 SkyRenderer::GetAmbientLight() const {
        // Compute ambient light from sky
        glm::vec3 ambient = glm::vec3(0.0f);

        // Sample sky in multiple directions
        const int samples = 16;
        for (int i = 0; i < samples; ++i) {
            float theta = (float(i) / float(samples)) * 2.0f * static_cast<float>(std::numbers::pi);
            for (int j = 0; j < samples / 2; ++j) {
                float phi = (float(j) / float(samples / 2)) * static_cast<float>(std::numbers::pi);

                glm::vec3 dir(
                    sin(phi) * cos(theta),
                    cos(phi),
                    sin(phi) * sin(theta)
                );

                ambient += GetCurrentSkyColor(dir);
            }
        }

        ambient /= float(samples * samples / 2);
        return ambient * m_skyParams.ambientLightMultiplier;
    }

    glm::vec3 SkyRenderer::CalculateRayleighScattering(const glm::vec3& rayDir) const {
        return AtmosphereHelper::ComputeRayleighScattering(
            glm::vec3(0, EARTH_RADIUS + 1000.0f, 0),  // Camera at 1km altitude
            rayDir,
            ATMOSPHERE_HEIGHT,
            m_skyParams.sunDirection,
            m_skyParams.rayleighBeta * m_skyParams.rayleighCoeff,
            16
        );
    }

    glm::vec3 SkyRenderer::CalculateMieScattering(const glm::vec3& rayDir) const {
        return AtmosphereHelper::ComputeMieScattering(
            glm::vec3(0, EARTH_RADIUS + 1000.0f, 0),
            rayDir,
            ATMOSPHERE_HEIGHT,
            m_skyParams.sunDirection,
            m_skyParams.mieBeta * m_skyParams.mieCoeff,
            m_skyParams.mieG,
            8
        );
    }

    // AtmosphereHelper implementation
    glm::vec3 AtmosphereHelper::ComputeRayleighScattering(
        const glm::vec3& rayOrigin,
        const glm::vec3& rayDir,
        float rayLength,
        const glm::vec3& sunDir,
        const glm::vec3& rayleighBeta,
        int numSamples) {

        float stepSize = rayLength / float(numSamples);
        glm::vec3 scatter = glm::vec3(0.0f);
        float opticalDepth = 0.0f;

        for (int i = 0; i < numSamples; ++i) {
            float t = (float(i) + 0.5f) * stepSize;
            glm::vec3 samplePos = rayOrigin + rayDir * t;

            float height = glm::length(samplePos) - EARTH_RADIUS;
            float density = exp(-height / 8000.0f); // Scale height of 8km

            opticalDepth += density * stepSize;

            // Compute sun optical depth
            float sunOpticalDepth = ComputeOpticalDepth(
                samplePos, sunDir, ATMOSPHERE_HEIGHT,
                EARTH_RADIUS, ATMOSPHERE_RADIUS, 8
            );

            glm::vec3 transmission = exp(-(rayleighBeta * (opticalDepth + sunOpticalDepth)));
            scatter += transmission * density * stepSize;
        }

        // Rayleigh phase function
        float cosTheta = glm::dot(rayDir, sunDir);
        float phase = 3.0f / (16.0f * static_cast<float>(std::numbers::pi)) * (1.0f + cosTheta * cosTheta);

        return scatter * rayleighBeta * phase * 20.0f; // Intensity scale
    }

    glm::vec3 AtmosphereHelper::ComputeMieScattering(
        const glm::vec3& rayOrigin,
        const glm::vec3& rayDir,
        float rayLength,
        const glm::vec3& sunDir,
        const glm::vec3& mieBeta,
        float g,
        int numSamples) {

        float stepSize = rayLength / float(numSamples);
        glm::vec3 scatter = glm::vec3(0.0f);
        float opticalDepth = 0.0f;

        for (int i = 0; i < numSamples; ++i) {
            float t = (float(i) + 0.5f) * stepSize;
            glm::vec3 samplePos = rayOrigin + rayDir * t;

            float height = glm::length(samplePos) - EARTH_RADIUS;
            float density = exp(-height / 1200.0f); // Scale height of 1.2km for Mie

            opticalDepth += density * stepSize;

            float sunOpticalDepth = ComputeOpticalDepth(
                samplePos, sunDir, ATMOSPHERE_HEIGHT,
                EARTH_RADIUS, ATMOSPHERE_RADIUS, 8
            );

            glm::vec3 transmission = exp(-(mieBeta * (opticalDepth + sunOpticalDepth)));
            scatter += transmission * density * stepSize;
        }

        // Henyey-Greenstein phase function
        float cosTheta = glm::dot(rayDir, sunDir);
        float phase = (1.0f - g * g) / (4.0f * static_cast<float>(std::numbers::pi) * pow(1.0f + g * g - 2.0f * g * cosTheta, 1.5f));

        return scatter * mieBeta * phase * 20.0f;
    }

    float AtmosphereHelper::ComputeOpticalDepth(
        const glm::vec3& rayOrigin,
        const glm::vec3& rayDir,
        float rayLength,
        float planetRadius,
        float atmosphereRadius,
        int numSamples) {

        float stepSize = rayLength / float(numSamples);
        float opticalDepth = 0.0f;

        for (int i = 0; i < numSamples; ++i) {
            float t = (float(i) + 0.5f) * stepSize;
            glm::vec3 samplePos = rayOrigin + rayDir * t;

            float height = glm::length(samplePos) - planetRadius;
            float density = exp(-height / 8000.0f);

            opticalDepth += density * stepSize;
        }

        return opticalDepth;
    }

    void SkyRenderer::CreateUniformBuffers() {
        m_atmosphereUBO = CreateUniformBuffer<AtmosphereUBO>(m_device);
        m_cloudUBO = CreateUniformBuffer<CloudUBO>(m_device);
        m_starUBO = CreateUniformBuffer<StarUBO>(m_device);
    }

    void SkyRenderer::CreateTextures() {
        const auto& settings = g_qualityPresets[static_cast<int>(m_qualityProfile)];

        // --- Star texture (2D) ---
        m_starTexture = std::make_unique<RHI::Vulkan::Image>(
            m_device,
            settings.starResolution,
            settings.starResolution,
            1, // depthOrArrayLayers
            1,
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_TYPE_2D,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_SAMPLE_COUNT_1_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY,
            VK_SHARING_MODE_EXCLUSIVE
        );
        m_starTexture->TransitionLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, m_context);

        // --- Cloud noise texture (3D) ---
        m_cloudNoiseTexture = std::make_unique<RHI::Vulkan::Image>(
            m_device,
            settings.cloudResolution,              // width
            settings.cloudResolution,              // height
            settings.cloudResolution / 4,          // depth
            1,                                     // arrayLayers (всегда 1 для 3D)
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_TYPE_3D,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_SAMPLE_COUNT_1_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY,
            VK_SHARING_MODE_EXCLUSIVE
        );
        m_cloudNoiseTexture->TransitionLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, m_context);

        // --- Cloud detail noise (3D) ---
        m_cloudDetailNoise = std::make_unique<RHI::Vulkan::Image>(
            m_device,
            settings.cloudResolution / 2,
            settings.cloudResolution / 2,
            settings.cloudResolution / 8,
            1,
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_TYPE_3D,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_SAMPLE_COUNT_1_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY,
            VK_SHARING_MODE_EXCLUSIVE
        );
        m_cloudDetailNoise->TransitionLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, m_context);

        // --- Milky Way texture (2D) ---
        m_milkyWayTexture = std::make_unique<RHI::Vulkan::Image>(
            m_device,
            2048, 1024,
            1,
            1,
            VK_FORMAT_R8_UNORM,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_IMAGE_TYPE_2D,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_SAMPLE_COUNT_1_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY,
            VK_SHARING_MODE_EXCLUSIVE
        );

        m_milkyWayTexture->TransitionLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_context);

        // --- Moon texture (2D) ---
        m_moonTexture = std::make_unique<RHI::Vulkan::Image>(
            m_device,
            512, 512,
            1,
            1,
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_IMAGE_TYPE_2D,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_SAMPLE_COUNT_1_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY,
            VK_SHARING_MODE_EXCLUSIVE
        );

        m_moonTexture->TransitionLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_context);
    }

    void SkyRenderer::CreateLUTs() {
        // Transmittance LUT
        m_transmittanceLUT = std::make_unique<RHI::Vulkan::Image>(
            m_device,
            256, 64, 1,1,
            VK_FORMAT_R32G32B32A32_SFLOAT,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_TYPE_2D,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_SAMPLE_COUNT_1_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY,
            VK_SHARING_MODE_EXCLUSIVE
        );
        m_transmittanceLUT->TransitionLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, m_context);

        // Multi-scattering LUT
        m_multiScatteringLUT = std::make_unique<RHI::Vulkan::Image>(
            m_device,
            32, 32, 1, 1,
            VK_FORMAT_R32G32B32A32_SFLOAT,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_TYPE_2D,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_SAMPLE_COUNT_1_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY,
            VK_SHARING_MODE_EXCLUSIVE
        );
        m_multiScatteringLUT->TransitionLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, m_context);

        //// BRDF LUT
        //m_brdfLUT = std::make_unique<RHI::Vulkan::Image>(
        //    m_device,
        //    512, 512, 1, 1,
        //    VK_FORMAT_R16G16_SFLOAT,
        //    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        //    VK_IMAGE_TYPE_2D,
        //    VK_IMAGE_TILING_OPTIMAL,
        //    VK_IMAGE_LAYOUT_UNDEFINED,
        //    VK_SAMPLE_COUNT_1_BIT,
        //    VK_IMAGE_ASPECT_COLOR_BIT,
        //    VMA_MEMORY_USAGE_GPU_ONLY,
        //    VK_SHARING_MODE_EXCLUSIVE
        //);
        //m_brdfLUT->TransitionLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_context);
    }

    void SkyRenderer::CreateRenderTargets() {
        const auto& settings = g_qualityPresets[static_cast<int>(m_qualityProfile)];

        // Sky buffer
        m_skyBuffer = std::make_unique<RHI::Vulkan::Image>(
            m_device,
            settings.skyResolution, settings.skyResolution, 1, 1,
            VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_TYPE_2D,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_SAMPLE_COUNT_1_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY,
            VK_SHARING_MODE_EXCLUSIVE
        );
        m_skyBuffer->TransitionLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, m_context);

        // Cloud buffer
        m_cloudBuffer = std::make_unique<RHI::Vulkan::Image>(
            m_device,
            settings.cloudResolution * 2, settings.cloudResolution * 2, 1, 1,
            VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_TYPE_2D,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_SAMPLE_COUNT_1_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY,
            VK_SHARING_MODE_EXCLUSIVE
        );
        m_cloudBuffer->TransitionLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, m_context);

        // Bloom buffer (5 mip levels)
        m_bloomBuffer = std::make_unique<RHI::Vulkan::Image>(
            m_device,
            settings.skyResolution / 2, settings.skyResolution / 2, 1, 1,
            VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_TYPE_2D,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_SAMPLE_COUNT_1_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY,
            VK_SHARING_MODE_EXCLUSIVE,
            5 // mipLevels
        );
        m_bloomBuffer->TransitionLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_context);

        // History buffer
        if (settings.temporalAccumulation) {
            m_historyBuffer = std::make_unique<RHI::Vulkan::Image>(
                m_device,
                settings.skyResolution, settings.skyResolution, 1, 1,
                VK_FORMAT_R16G16B16A16_SFLOAT,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_IMAGE_TYPE_2D,
                VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_SAMPLE_COUNT_1_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT,
                VMA_MEMORY_USAGE_GPU_ONLY,
                VK_SHARING_MODE_EXCLUSIVE
            );
            m_historyBuffer->TransitionLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_context);
        }
    }

    void SkyRenderer::CreateSamplers() {
        // Linear sampler
        m_linearSamplerObj = std::make_unique<RHI::Vulkan::Sampler>(
            m_device, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            true, 16.0f
        );
        m_linearSampler = m_linearSamplerObj->GetHandle();

        // Nearest sampler
        m_nearestSamplerObj = std::make_unique<RHI::Vulkan::Sampler>(
            m_device, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            false, 1.0f
        );
        m_nearestSampler = m_nearestSamplerObj->GetHandle();

        // Cloud sampler (wrap mode)
        m_cloudSamplerObj = std::make_unique<RHI::Vulkan::Sampler>(
            m_device, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT,
            true, 8.0f
        );
        m_cloudSampler = m_cloudSamplerObj->GetHandle();
    }

    void SkyRenderer::UpdateUniformBuffers() {
        // Update atmosphere UBO
        struct AtmosphereUBO {
            glm::vec3 sunDirection;
            float sunIntensity;
            glm::vec3 rayleighBeta;
            float turbidity;
            glm::vec3 mieBeta;
            float mieG;
            glm::vec3 groundColor;
            float planetRadius;
            glm::vec3 sunColor;
            float atmosphereRadius;
        } atmosphereData;

        atmosphereData.sunDirection = m_skyParams.sunDirection;
        atmosphereData.sunIntensity = m_skyParams.sunIntensity;
        atmosphereData.rayleighBeta = m_skyParams.rayleighBeta * m_skyParams.rayleighCoeff;
        atmosphereData.turbidity = m_skyParams.turbidity;
        atmosphereData.mieBeta = m_skyParams.mieBeta * m_skyParams.mieCoeff;
        atmosphereData.mieG = m_skyParams.mieG;
        atmosphereData.groundColor = m_skyParams.groundColor;
        atmosphereData.planetRadius = EARTH_RADIUS;
        atmosphereData.sunColor = glm::vec3(1.0f, 0.98f, 0.9f);
        atmosphereData.atmosphereRadius = ATMOSPHERE_RADIUS;

        m_atmosphereUBO->Upload(&atmosphereData, sizeof(atmosphereData));

        // Update cloud UBO
        struct CloudUBO {
            float cloudAltitude;
            float cloudThickness;
            float cloudDensity;
            float cloudAbsorption;
            glm::vec3 cloudColor;
            float cloudLacunarity;
            glm::vec3 cloudScatterColor;
            float cloudGain;
            int cloudOctaves;
            float cloudAnimSpeed;
            float cloudDetailScale;
            float cloudDetailStrength;
        } cloudData;

        cloudData.cloudAltitude = m_skyParams.cloudAltitude;
        cloudData.cloudThickness = m_skyParams.cloudThickness;
        cloudData.cloudDensity = m_skyParams.cloudDensity;
        cloudData.cloudAbsorption = 0.5f;
        cloudData.cloudColor = glm::vec3(1.0f);
        cloudData.cloudLacunarity = m_skyParams.cloudLacunarity;
        cloudData.cloudScatterColor = glm::vec3(0.8f, 0.9f, 1.0f);
        cloudData.cloudGain = m_skyParams.cloudGain;
        cloudData.cloudOctaves = m_skyParams.cloudOctaves;
        cloudData.cloudAnimSpeed = m_skyParams.cloudSpeed;
        cloudData.cloudDetailScale = 2.0f;
        cloudData.cloudDetailStrength = 0.3f;

        m_cloudUBO->Upload(&cloudData, sizeof(cloudData));
    }

    void SkyRenderer::GenerateAtmosphereLUT(VkCommandBuffer cmd) {
        // Bind compute pipeline
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_atmosphereLUTPipeline->GetPipeline());

        // Bind descriptor set with LUT images
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
            m_atmosphereLUTPipeline->GetLayout(),
            0, 1, &m_atmosphereLUTDescSet, 0, nullptr);

        // Dispatch compute threads
        uint32_t groupCountX = (256 + 7) / 8;
        uint32_t groupCountY = (64 + 7) / 8;
        vkCmdDispatch(cmd, groupCountX, groupCountY, 1);

        // Barrier to ensure LUT is ready
        VkMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 1, &barrier, 0, nullptr, 0, nullptr);
    }

    void SkyRenderer::GenerateCloudNoise(VkCommandBuffer cmd) {
        const auto& settings = g_qualityPresets[static_cast<int>(m_qualityProfile)];

        // Bind compute pipeline
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_cloudNoisePipeline->GetPipeline());

        // Bind descriptor set
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
            m_cloudNoisePipeline->GetLayout(),
            0, 1, &m_cloudNoiseDescSet, 0, nullptr);

        // Обновляем push constants
        CloudNoisePushConstants pushConstants{};
        pushConstants.octaves = settings.cloudOctaves;    // Или ваши значения
        pushConstants.lacunarity = 2.0f;
        pushConstants.gain = 0.5f;
        pushConstants.scale = static_cast<float>(settings.cloudResolution);      // Например, из настроек качества

        vkCmdPushConstants(
            cmd,
            m_cloudNoisePipeline->GetLayout(),
            VK_SHADER_STAGE_COMPUTE_BIT,
            0,
            sizeof(CloudNoisePushConstants),
            &pushConstants
        );

        // Dispatch for 3D texture
        uint32_t groupCountX = (settings.cloudResolution + 7) / 8;
        uint32_t groupCountY = (settings.cloudResolution + 7) / 8;
        uint32_t groupCountZ = (settings.cloudResolution / 4 + 7) / 8;
        vkCmdDispatch(cmd, groupCountX, groupCountY, groupCountZ);

        // Transition to shader read
        VkImageMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = m_cloudNoiseTexture->GetHandle();
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);

        // Same for detail noise
        barrier.image = m_cloudDetailNoise->GetHandle();
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    void SkyRenderer::GenerateStarTexture(VkCommandBuffer cmd) {
        const auto& settings = g_qualityPresets[static_cast<int>(m_qualityProfile)];

        // Generate procedural star field
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_starGeneratorPipeline->GetPipeline());
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
            m_starGeneratorPipeline->GetLayout(), 
            0, 1, &m_starGeneratorDescSet, 0, nullptr);

        uint32_t groupCount = (settings.starResolution + 7) / 8;
        vkCmdDispatch(cmd, groupCount, groupCount, 1);

        // Transition to shader read
        VkImageMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = m_starTexture->GetHandle();
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    void SkyRenderer::GenerateEnvironmentMaps(VkCommandBuffer cmd) {
        // This would generate IBL maps from the current sky
        // Implementation would involve:
        // 1. Render sky to cubemap
        // 2. Generate irradiance map via convolution
        // 3. Generate prefiltered environment map for different roughness levels
        // 4. Update BRDF LUT if needed

        // For now, we'll use the analytical sky directly for IBL
        // This is a placeholder for full IBL generation
    }

    void SkyRenderer::CreateAtmospherePipeline() {
        const std::string programName = "Atmosphere";

        // --- Проверка что программа действительно зарегистрирована ---
        if (!m_shaderManager->GetProgram(programName)) {
            throw std::runtime_error("Shader program '" + programName + "' not found. "
                "Make sure MeadowApp::LoadShaders() was called before creating pipelines.");
        }

        // --- Descriptor layout  ---
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &binding;

        if (vkCreateDescriptorSetLayout(m_device->GetDevice(), &layoutInfo, nullptr, &m_atmosphereDescSetLayout) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create atmosphere descriptor set layout");
        }

        // --- Push constants ---
        VkPushConstantRange pushConstant{};
        pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pushConstant.offset = 0;
        pushConstant.size = sizeof(glm::mat4) + sizeof(glm::vec3) + sizeof(float);

        // --- Подготовка CreateInfo через хелпер ---
        auto info = RHI::Vulkan::ReloadablePipeline::MakePipelineInfo(
            programName,
            VK_FORMAT_R16G16B16A16_SFLOAT,
            &m_atmosphereDescSetLayout,
            1,
            false, // blending off
            VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD,
            VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD
        );

        info.depthFormat = m_depthFormat;
        info.depthTestEnable = false;
        info.depthWriteEnable = false;
        info.cullMode = VK_CULL_MODE_NONE;
        info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        info.pushConstants.push_back(pushConstant);

        // Сохраним info, чтобы можно было пересоздать позже при хот-релоаде
        m_atmospherePipelineCreateInfo = info; // <-- добавь поле в SkyRenderer: ReloadablePipeline::CreateInfo m_atmospherePipelineCreateInfo;

        // --- Создаём ReloadablePipeline ---
        m_atmospherePipeline = std::make_unique<RHI::Vulkan::ReloadablePipeline>(m_device, m_shaderManager);
        if (!m_atmospherePipeline->Create(info)) {
            throw std::runtime_error("Failed to create atmosphere pipeline");
        }
    }

    void SkyRenderer::CreateCloudPipeline() {
        // --- Descriptor set layout ---
        std::vector<VkDescriptorSetLayoutBinding> bindings(3);

        // UBO
        bindings[0] = { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
        // Cloud noise texture
        bindings[1] = { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
        // Cloud detail texture
        bindings[2] = { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };


        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(m_device->GetDevice(), &layoutInfo, nullptr, &m_cloudDescSetLayout) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create cloud descriptor set layout");
        }

        // Определяем push constant для облаков
        VkPushConstantRange cloudPushRange{};
        cloudPushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        cloudPushRange.offset = 0;
        cloudPushRange.size = sizeof(CloudPushConstants);

        // --- Pipeline info ---
        auto info = RHI::Vulkan::ReloadablePipeline::MakePipelineInfo(
            "Clouds",                // имя программы из LoadShaders
            VK_FORMAT_R16G16B16A16_SFLOAT,
            &m_cloudDescSetLayout,
            1,
            true, // включаем blending
            VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD,
            VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD
        );

        info.pushConstants.push_back(cloudPushRange);

        info.depthFormat = m_depthFormat;
        info.depthTestEnable = false;
        info.depthWriteEnable = false;
        info.cullMode = VK_CULL_MODE_NONE;
        info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        m_cloudPipeline = std::make_unique<RHI::Vulkan::ReloadablePipeline>(m_device, m_shaderManager);
        if (!m_cloudPipeline->Create(info)) {
            throw std::runtime_error("Failed to create cloud pipeline");
        }
    }

    void SkyRenderer::CreateStarsPipeline() {
        // Descriptor layout for stars
        std::vector<VkDescriptorSetLayoutBinding> bindings(3); // Увеличьте размер

        // Uniform buffer
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        // Star texture
        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        // Milky Way texture
        bindings[2].binding = 2;
        bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo = {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(m_device->GetDevice(), &layoutInfo, nullptr, &m_starsDescSetLayout) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create stars descriptor set layout");
        }

        // --- Push constants для звёзд (если нужны) ---
        VkPushConstantRange starsPushRange{};
        starsPushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT; // или vertex + fragment
        starsPushRange.offset = 0;
        starsPushRange.size = sizeof(StarsPushConstants); // структура push-constants для звёзд

        // --- Pipeline info ---
        auto info = RHI::Vulkan::ReloadablePipeline::MakePipelineInfo(
            "Stars",
            VK_FORMAT_R16G16B16A16_SFLOAT,
            &m_starsDescSetLayout,
            1,       // 1 дескрипторный сет
            true,    // включаем blending
            VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE, VK_BLEND_OP_ADD,
            VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_OP_ADD
        );

        info.pushConstants.push_back(starsPushRange);

        info.depthFormat = m_depthFormat;
        info.depthTestEnable = false;
        info.depthWriteEnable = false;
        info.cullMode = VK_CULL_MODE_NONE;
        info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        // --- Создаём пайплайн ---
        m_starsPipeline = std::make_unique<RHI::Vulkan::ReloadablePipeline>(m_device, m_shaderManager);
        if (!m_starsPipeline->Create(info)) {
            throw std::runtime_error("Failed to create stars pipeline");
        }
    }

    void SkyRenderer::CreatePostProcessPipeline() {
        std::vector<VkDescriptorSetLayoutBinding> bindings(3);

        bindings[0] = { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }; // Sky buffer
        bindings[1] = { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }; // Cloud buffer
        bindings[2] = { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }; // Bloom buffer

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(m_device->GetDevice(), &layoutInfo, nullptr, &m_postProcessDescSetLayout) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create post-process descriptor set layout");
        }

        VkPushConstantRange postprocessPushRange{};
        postprocessPushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT; // или vertex + fragment
        postprocessPushRange.offset = 0;
        postprocessPushRange.size = sizeof(PostProcessPushConstants); // структура push-constants для звёзд

        auto info = RHI::Vulkan::ReloadablePipeline::MakePipelineInfo(
            "PostProcess",
            m_colorFormat,
            &m_postProcessDescSetLayout,
            1,
            false // без blending
        );

        info.pushConstants.push_back(postprocessPushRange);

        info.depthFormat = m_depthFormat;
        info.depthTestEnable = false;
        info.depthWriteEnable = false;
        info.cullMode = VK_CULL_MODE_NONE;
        info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        m_postProcessPipeline = std::make_unique<RHI::Vulkan::ReloadablePipeline>(m_device, m_shaderManager);
        if (!m_postProcessPipeline->Create(info)) {
            throw std::runtime_error("Failed to create post-process pipeline");
        }
    }

    void SkyRenderer::CreateComputePipelines() {
        // Create atmosphere LUT generation pipeline
        CreateAtmosphereLUTPipeline();

        // Create cloud noise generation pipeline
        CreateCloudNoisePipeline();

        // Create star texture generation pipeline
        CreateStarGeneratorPipeline();
    }

    void SkyRenderer::CreateAtmosphereLUTPipeline() {
        RHI::Vulkan::ComputePipelineCreateInfo info{};
        info.shaderProgramName = "GenerateLUT"; // Имя программы в ShaderManager

        // Descriptor set layout
        std::vector<VkDescriptorSetLayoutBinding> bindings(3);
        bindings[0] = { 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        bindings[1] = { 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        bindings[2] = { 2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

      
        if (vkCreateDescriptorSetLayout(m_device->GetDevice(), &layoutInfo, nullptr, &m_atmosphereLUTDescSetLayout) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create LUT descriptor set layout");
        }
        info.descriptorSetLayout = m_atmosphereLUTDescSetLayout;

        // Push constants отсутствуют
        info.pushConstants.clear();

        m_atmosphereLUTPipeline = std::make_unique<RHI::Vulkan::ComputePipeline>(m_device, m_shaderManager);
        if (!m_atmosphereLUTPipeline->Create(info)) {
            throw std::runtime_error("Failed to create atmosphere LUT compute pipeline");
        }
    }

    void SkyRenderer::CreateCloudNoisePipeline() {
        RHI::Vulkan::ComputePipelineCreateInfo info{};
        info.shaderProgramName = "CloudNoise";

        std::vector<VkDescriptorSetLayoutBinding> bindings(2);
        bindings[0] = { 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        bindings[1] = { 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(m_device->GetDevice(), &layoutInfo, nullptr, &m_cloudNoiseDescSetLayout) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create cloud noise descriptor set layout");
        }
        info.descriptorSetLayout = m_cloudNoiseDescSetLayout;

        // Добавляем push constants
        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(CloudNoisePushConstants); // Определите эту структуру

        info.pushConstants.push_back(pushRange);

        m_cloudNoisePipeline = std::make_unique<RHI::Vulkan::ComputePipeline>(m_device, m_shaderManager);
        if (!m_cloudNoisePipeline->Create(info)) {
            throw std::runtime_error("Failed to create cloud noise compute pipeline");
        }
    }

    void SkyRenderer::CreateStarGeneratorPipeline() {
        RHI::Vulkan::ComputePipelineCreateInfo info{};
        info.shaderProgramName = "GenerateStars";

        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &binding;

        if (vkCreateDescriptorSetLayout(m_device->GetDevice(), &layoutInfo, nullptr, &m_starGeneratorDescSetLayout) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create star descriptor set layout");
        }
        info.descriptorSetLayout = m_starGeneratorDescSetLayout;

        // Push constant для seed
        VkPushConstantRange pushConstant{};
        pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushConstant.offset = 0;
        pushConstant.size = sizeof(uint32_t);
        info.pushConstants.push_back(pushConstant);

        m_starGeneratorPipeline = std::make_unique<RHI::Vulkan::ComputePipeline>(m_device, m_shaderManager);
        if (!m_starGeneratorPipeline->Create(info)) {
            throw std::runtime_error("Failed to create star generator compute pipeline");
        }
    }

    void SkyRenderer::CreateDescriptorSets() {
        // Create descriptor pool
        std::vector<VkDescriptorPoolSize> poolSizes = {
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 20},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 20}
        };

        VkDescriptorPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = 10;

        if (vkCreateDescriptorPool(m_device->GetDevice(), &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create descriptor pool");
        }

        // Allocate atmosphere descriptor set
        VkDescriptorSetAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_atmosphereDescSetLayout;

        if (vkAllocateDescriptorSets(m_device->GetDevice(), &allocInfo, &m_atmosphereDescSet) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate atmosphere descriptor set");
        }

        // Update atmosphere descriptor set
        VkDescriptorBufferInfo bufferInfo = {};
        bufferInfo.buffer = m_atmosphereUBO->GetBuffer();
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(AtmosphereUBO);

        VkWriteDescriptorSet descriptorWrite = {};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = m_atmosphereDescSet;
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(m_device->GetDevice(), 1, &descriptorWrite, 0, nullptr);

        // Allocate and update cloud descriptor set
        if (m_cloudDescSetLayout == VK_NULL_HANDLE) {
            throw std::runtime_error("Atmosphere descriptor set layout is VK_NULL_HANDLE - pipeline creation failed");
        }

        allocInfo.pSetLayouts = &m_cloudDescSetLayout;
        if (vkAllocateDescriptorSets(m_device->GetDevice(), &allocInfo, &m_cloudDescSet) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate cloud descriptor set");
        }

        // Allocate star generator descriptor set
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_starGeneratorDescSetLayout;

        if (vkAllocateDescriptorSets(m_device->GetDevice(), &allocInfo, &m_starGeneratorDescSet) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate star generator descriptor set");
        }

        // Update star generator descriptor set
        VkDescriptorImageInfo starGenImageInfo = {};
        starGenImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        starGenImageInfo.imageView = m_starTexture->GetView();
        starGenImageInfo.sampler = VK_NULL_HANDLE;

        VkWriteDescriptorSet starGenWrite = {};
        starGenWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        starGenWrite.dstSet = m_starGeneratorDescSet;
        starGenWrite.dstBinding = 0;
        starGenWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        starGenWrite.descriptorCount = 1;
        starGenWrite.pImageInfo = &starGenImageInfo;

        vkUpdateDescriptorSets(m_device->GetDevice(), 1, &starGenWrite, 0, nullptr);

        // Allocate cloud noise descriptor set
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_descriptorPool;  // Используем тот же пул
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_cloudNoiseDescSetLayout;

        if (vkAllocateDescriptorSets(m_device->GetDevice(), &allocInfo, &m_cloudNoiseDescSet) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate cloud noise descriptor set");
        }

        std::vector<VkWriteDescriptorSet> noiseWrites(2);

        // Binding 0: Cloud noise texture
        VkDescriptorImageInfo noiseImageInfo = {};
        noiseImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        noiseImageInfo.imageView = m_cloudNoiseTexture->GetView();
        noiseImageInfo.sampler = VK_NULL_HANDLE;

        noiseWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        noiseWrites[0].dstSet = m_cloudNoiseDescSet;
        noiseWrites[0].dstBinding = 0;
        noiseWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        noiseWrites[0].descriptorCount = 1;
        noiseWrites[0].pImageInfo = &noiseImageInfo;

        // Binding 1: Cloud detail noise
        VkDescriptorImageInfo detailImageInfo = {};
        detailImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        detailImageInfo.imageView = m_cloudDetailNoise->GetView();
        detailImageInfo.sampler = VK_NULL_HANDLE;

        noiseWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        noiseWrites[1].dstSet = m_cloudNoiseDescSet;
        noiseWrites[1].dstBinding = 1;
        noiseWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        noiseWrites[1].descriptorCount = 1;
        noiseWrites[1].pImageInfo = &detailImageInfo;

        vkUpdateDescriptorSets(m_device->GetDevice(),
            static_cast<uint32_t>(noiseWrites.size()),
            noiseWrites.data(), 0, nullptr);

        // Allocate atmosphere LUT descriptor set
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_atmosphereLUTDescSetLayout;

        if (vkAllocateDescriptorSets(m_device->GetDevice(), &allocInfo, &m_atmosphereLUTDescSet) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate atmosphere LUT descriptor set");
        }

        // Update atmosphere LUT descriptor set
        std::vector<VkWriteDescriptorSet> lutWrites(3);

        // Binding 0: Storage Image (например, transmission LUT)
        VkDescriptorImageInfo transmissionImageInfo = {};
        transmissionImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        transmissionImageInfo.imageView = m_transmittanceLUT->GetView(); // замените на ваше изображение
        transmissionImageInfo.sampler = VK_NULL_HANDLE;

        lutWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        lutWrites[0].dstSet = m_atmosphereLUTDescSet;
        lutWrites[0].dstBinding = 0;
        lutWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        lutWrites[0].descriptorCount = 1;
        lutWrites[0].pImageInfo = &transmissionImageInfo;

        // Binding 1: Storage Image (например, scattering LUT)
        VkDescriptorImageInfo scatteringImageInfo = {};
        scatteringImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        scatteringImageInfo.imageView = m_multiScatteringLUT->GetView(); // замените на ваше изображение
        scatteringImageInfo.sampler = VK_NULL_HANDLE;

        lutWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        lutWrites[1].dstSet = m_atmosphereLUTDescSet;
        lutWrites[1].dstBinding = 1;
        lutWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        lutWrites[1].descriptorCount = 1;
        lutWrites[1].pImageInfo = &scatteringImageInfo;

        // Binding 2: Uniform Buffer
        VkDescriptorBufferInfo lutBufferInfo = {};
        lutBufferInfo.buffer = m_atmosphereUBO->GetBuffer(); // можете переиспользовать тот же UBO
        lutBufferInfo.offset = 0;
        lutBufferInfo.range = sizeof(AtmosphereUBO);

        lutWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        lutWrites[2].dstSet = m_atmosphereLUTDescSet;
        lutWrites[2].dstBinding = 2;
        lutWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        lutWrites[2].descriptorCount = 1;
        lutWrites[2].pBufferInfo = &lutBufferInfo;

        vkUpdateDescriptorSets(m_device->GetDevice(),
            static_cast<uint32_t>(lutWrites.size()),
            lutWrites.data(), 0, nullptr);

        std::vector<VkWriteDescriptorSet> cloudWrites(3);

        // Cloud UBO
        VkDescriptorBufferInfo cloudBufferInfo = {};
        cloudBufferInfo.buffer = m_cloudUBO->GetBuffer(); 
        cloudBufferInfo.offset = 0;
        cloudBufferInfo.range = sizeof(CloudUBO);

        cloudWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        cloudWrites[0].dstSet = m_cloudDescSet;
        cloudWrites[0].dstBinding = 0;
        cloudWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        cloudWrites[0].descriptorCount = 1;
        cloudWrites[0].pBufferInfo = &cloudBufferInfo;

        // Cloud noise texture
        noiseImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        noiseImageInfo.imageView = m_cloudNoiseTexture->GetView();
        noiseImageInfo.sampler = m_cloudSampler;

        cloudWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        cloudWrites[1].dstSet = m_cloudDescSet;
        cloudWrites[1].dstBinding = 1;
        cloudWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        cloudWrites[1].descriptorCount = 1;
        cloudWrites[1].pImageInfo = &noiseImageInfo;

        // Cloud detail texture
        detailImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        detailImageInfo.imageView = m_cloudDetailNoise->GetView();
        detailImageInfo.sampler = m_cloudSampler;

        cloudWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        cloudWrites[2].dstSet = m_cloudDescSet;
        cloudWrites[2].dstBinding = 2;
        cloudWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        cloudWrites[2].descriptorCount = 1;
        cloudWrites[2].pImageInfo = &detailImageInfo;

        vkUpdateDescriptorSets(m_device->GetDevice(),
            static_cast<uint32_t>(cloudWrites.size()),
            cloudWrites.data(), 0, nullptr);

        // Allocate and update stars descriptor set
        allocInfo.pSetLayouts = &m_starsDescSetLayout;
        if (vkAllocateDescriptorSets(m_device->GetDevice(), &allocInfo, &m_starsDescSet) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate stars descriptor set");
        }

        std::vector<VkWriteDescriptorSet> starWrites(3);

        // Stars uniform buffer
        VkDescriptorBufferInfo starsBufferInfo = {};
        starsBufferInfo.buffer = m_starUBO->GetBuffer();  
        starsBufferInfo.offset = 0;
        starsBufferInfo.range = sizeof(StarUBO);

        // Star texture
        starWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        starWrites[0].dstSet = m_starsDescSet;
        starWrites[0].dstBinding = 0;
        starWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        starWrites[0].descriptorCount = 1;
        starWrites[0].pBufferInfo = &starsBufferInfo;

        // Star texture (теперь binding = 1)
        VkDescriptorImageInfo starImageInfo = {};
        starImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        starImageInfo.imageView = m_starTexture->GetView();
        starImageInfo.sampler = m_nearestSampler;

        starWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        starWrites[1].dstSet = m_starsDescSet;
        starWrites[1].dstBinding = 1;  // Изменено на binding = 1
        starWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        starWrites[1].descriptorCount = 1;
        starWrites[1].pImageInfo = &starImageInfo;

        // Milky Way texture (теперь binding = 2)
        VkDescriptorImageInfo milkyWayImageInfo = {};
        milkyWayImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        milkyWayImageInfo.imageView = m_milkyWayTexture->GetView();
        milkyWayImageInfo.sampler = m_linearSampler;

        starWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        starWrites[2].dstSet = m_starsDescSet;
        starWrites[2].dstBinding = 2;  // Изменено на binding = 2
        starWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        starWrites[2].descriptorCount = 1;
        starWrites[2].pImageInfo = &milkyWayImageInfo;

        vkUpdateDescriptorSets(m_device->GetDevice(),
            static_cast<uint32_t>(starWrites.size()),
            starWrites.data(), 0, nullptr);

        // Allocate and update post-process descriptor set
        allocInfo.pSetLayouts = &m_postProcessDescSetLayout;
        if (vkAllocateDescriptorSets(m_device->GetDevice(), &allocInfo, &m_postProcessDescSet) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate post-process descriptor set");
        }

        std::vector<VkWriteDescriptorSet> postWrites(3);

        // Sky buffer
        VkDescriptorImageInfo skyImageInfo = {};
        skyImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        skyImageInfo.imageView = m_skyBuffer->GetView();
        skyImageInfo.sampler = m_linearSampler;

        postWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        postWrites[0].dstSet = m_postProcessDescSet;
        postWrites[0].dstBinding = 0;
        postWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        postWrites[0].descriptorCount = 1;
        postWrites[0].pImageInfo = &skyImageInfo;

        // Cloud buffer
        VkDescriptorImageInfo cloudImageInfo = {};
        cloudImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        cloudImageInfo.imageView = m_cloudBuffer->GetView();
        cloudImageInfo.sampler = m_linearSampler;

        postWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        postWrites[1].dstSet = m_postProcessDescSet;
        postWrites[1].dstBinding = 1;
        postWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        postWrites[1].descriptorCount = 1;
        postWrites[1].pImageInfo = &cloudImageInfo;

        // Bloom buffer
        VkDescriptorImageInfo bloomImageInfo = {};
        bloomImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        bloomImageInfo.imageView = m_bloomBuffer->GetView();
        bloomImageInfo.sampler = m_linearSampler;

        postWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        postWrites[2].dstSet = m_postProcessDescSet;
        postWrites[2].dstBinding = 2;
        postWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        postWrites[2].descriptorCount = 1;
        postWrites[2].pImageInfo = &bloomImageInfo;

        vkUpdateDescriptorSets(m_device->GetDevice(),
            static_cast<uint32_t>(postWrites.size()),
            postWrites.data(), 0, nullptr);
    }

    void SkyRenderer::UpdateDescriptorSets() {
        // Atmosphere descriptor set
        {
            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = m_atmosphereUBO->GetBuffer();
            bufferInfo.offset = 0;
            bufferInfo.range = sizeof(AtmosphereUBO);

            VkWriteDescriptorSet descriptorWrite{};
            descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrite.dstSet = m_atmosphereDescSet;
            descriptorWrite.dstBinding = 0;
            descriptorWrite.dstArrayElement = 0;
            descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            descriptorWrite.descriptorCount = 1;
            descriptorWrite.pBufferInfo = &bufferInfo;

            vkUpdateDescriptorSets(m_device->GetDevice(), 1, &descriptorWrite, 0, nullptr);
        }

        // Star generator descriptor set
        {
            VkDescriptorImageInfo starGenImageInfo{};
            starGenImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            starGenImageInfo.imageView = m_starTexture->GetView();
            starGenImageInfo.sampler = VK_NULL_HANDLE;

            VkWriteDescriptorSet starGenWrite{};
            starGenWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            starGenWrite.dstSet = m_starGeneratorDescSet;
            starGenWrite.dstBinding = 0;
            starGenWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            starGenWrite.descriptorCount = 1;
            starGenWrite.pImageInfo = &starGenImageInfo;

            vkUpdateDescriptorSets(m_device->GetDevice(), 1, &starGenWrite, 0, nullptr);
        }

        // Cloud noise descriptor set
        {
            std::vector<VkWriteDescriptorSet> noiseWrites(2);

            VkDescriptorImageInfo noiseImageInfo{};
            noiseImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            noiseImageInfo.imageView = m_cloudNoiseTexture->GetView();
            noiseImageInfo.sampler = VK_NULL_HANDLE;

            noiseWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            noiseWrites[0].dstSet = m_cloudNoiseDescSet;
            noiseWrites[0].dstBinding = 0;
            noiseWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            noiseWrites[0].descriptorCount = 1;
            noiseWrites[0].pImageInfo = &noiseImageInfo;

            VkDescriptorImageInfo detailImageInfo{};
            detailImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            detailImageInfo.imageView = m_cloudDetailNoise->GetView();
            detailImageInfo.sampler = VK_NULL_HANDLE;

            noiseWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            noiseWrites[1].dstSet = m_cloudNoiseDescSet;
            noiseWrites[1].dstBinding = 1;
            noiseWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            noiseWrites[1].descriptorCount = 1;
            noiseWrites[1].pImageInfo = &detailImageInfo;

            vkUpdateDescriptorSets(m_device->GetDevice(), static_cast<uint32_t>(noiseWrites.size()), noiseWrites.data(), 0, nullptr);
        }

        // Atmosphere LUT descriptor set
        {
            std::vector<VkWriteDescriptorSet> lutWrites(3);

            VkDescriptorImageInfo transmissionImageInfo{};
            transmissionImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            transmissionImageInfo.imageView = m_transmittanceLUT->GetView();
            transmissionImageInfo.sampler = VK_NULL_HANDLE;

            lutWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            lutWrites[0].dstSet = m_atmosphereLUTDescSet;
            lutWrites[0].dstBinding = 0;
            lutWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            lutWrites[0].descriptorCount = 1;
            lutWrites[0].pImageInfo = &transmissionImageInfo;

            VkDescriptorImageInfo scatteringImageInfo{};
            scatteringImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            scatteringImageInfo.imageView = m_multiScatteringLUT->GetView();
            scatteringImageInfo.sampler = VK_NULL_HANDLE;

            lutWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            lutWrites[1].dstSet = m_atmosphereLUTDescSet;
            lutWrites[1].dstBinding = 1;
            lutWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            lutWrites[1].descriptorCount = 1;
            lutWrites[1].pImageInfo = &scatteringImageInfo;

            VkDescriptorBufferInfo lutBufferInfo{};
            lutBufferInfo.buffer = m_atmosphereUBO->GetBuffer();
            lutBufferInfo.offset = 0;
            lutBufferInfo.range = sizeof(AtmosphereUBO);

            lutWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            lutWrites[2].dstSet = m_atmosphereLUTDescSet;
            lutWrites[2].dstBinding = 2;
            lutWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            lutWrites[2].descriptorCount = 1;
            lutWrites[2].pBufferInfo = &lutBufferInfo;

            vkUpdateDescriptorSets(m_device->GetDevice(), static_cast<uint32_t>(lutWrites.size()), lutWrites.data(), 0, nullptr);
        }

        // Cloud descriptor set
        {
            std::vector<VkWriteDescriptorSet> cloudWrites(3);

            VkDescriptorBufferInfo cloudBufferInfo{};
            cloudBufferInfo.buffer = m_cloudUBO->GetBuffer();
            cloudBufferInfo.offset = 0;
            cloudBufferInfo.range = sizeof(CloudUBO);

            cloudWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            cloudWrites[0].dstSet = m_cloudDescSet;
            cloudWrites[0].dstBinding = 0;
            cloudWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            cloudWrites[0].descriptorCount = 1;
            cloudWrites[0].pBufferInfo = &cloudBufferInfo;

            VkDescriptorImageInfo noiseImageInfo{};
            noiseImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            noiseImageInfo.imageView = m_cloudNoiseTexture->GetView();
            noiseImageInfo.sampler = m_cloudSampler;

            cloudWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            cloudWrites[1].dstSet = m_cloudDescSet;
            cloudWrites[1].dstBinding = 1;
            cloudWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            cloudWrites[1].descriptorCount = 1;
            cloudWrites[1].pImageInfo = &noiseImageInfo;

            VkDescriptorImageInfo detailImageInfo{};
            detailImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            detailImageInfo.imageView = m_cloudDetailNoise->GetView();
            detailImageInfo.sampler = m_cloudSampler;

            cloudWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            cloudWrites[2].dstSet = m_cloudDescSet;
            cloudWrites[2].dstBinding = 2;
            cloudWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            cloudWrites[2].descriptorCount = 1;
            cloudWrites[2].pImageInfo = &detailImageInfo;

            vkUpdateDescriptorSets(m_device->GetDevice(), static_cast<uint32_t>(cloudWrites.size()), cloudWrites.data(), 0, nullptr);
        }

        // Stars descriptor set
        {
            std::vector<VkWriteDescriptorSet> starWrites(3);

            VkDescriptorBufferInfo starsBufferInfo{};
            starsBufferInfo.buffer = m_starUBO->GetBuffer();
            starsBufferInfo.offset = 0;
            starsBufferInfo.range = sizeof(StarUBO);

            starWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            starWrites[0].dstSet = m_starsDescSet;
            starWrites[0].dstBinding = 0;
            starWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            starWrites[0].descriptorCount = 1;
            starWrites[0].pBufferInfo = &starsBufferInfo;

            VkDescriptorImageInfo starImageInfo{};
            starImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            starImageInfo.imageView = m_starTexture->GetView();
            starImageInfo.sampler = m_nearestSampler;

            starWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            starWrites[1].dstSet = m_starsDescSet;
            starWrites[1].dstBinding = 1;
            starWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            starWrites[1].descriptorCount = 1;
            starWrites[1].pImageInfo = &starImageInfo;

            VkDescriptorImageInfo milkyWayImageInfo{};
            milkyWayImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            milkyWayImageInfo.imageView = m_milkyWayTexture->GetView();
            milkyWayImageInfo.sampler = m_linearSampler;

            starWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            starWrites[2].dstSet = m_starsDescSet;
            starWrites[2].dstBinding = 2;
            starWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            starWrites[2].descriptorCount = 1;
            starWrites[2].pImageInfo = &milkyWayImageInfo;

            vkUpdateDescriptorSets(m_device->GetDevice(), static_cast<uint32_t>(starWrites.size()), starWrites.data(), 0, nullptr);
        }

        // Post-process descriptor set
        {
            std::vector<VkWriteDescriptorSet> postWrites(3);

            VkDescriptorImageInfo skyImageInfo{};
            skyImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            skyImageInfo.imageView = m_skyBuffer->GetView();
            skyImageInfo.sampler = m_linearSampler;

            postWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            postWrites[0].dstSet = m_postProcessDescSet;
            postWrites[0].dstBinding = 0;
            postWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            postWrites[0].descriptorCount = 1;
            postWrites[0].pImageInfo = &skyImageInfo;

            VkDescriptorImageInfo cloudImageInfo{};
            cloudImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            cloudImageInfo.imageView = m_cloudBuffer->GetView();
            cloudImageInfo.sampler = m_linearSampler;

            postWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            postWrites[1].dstSet = m_postProcessDescSet;
            postWrites[1].dstBinding = 1;
            postWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            postWrites[1].descriptorCount = 1;
            postWrites[1].pImageInfo = &cloudImageInfo;

            VkDescriptorImageInfo bloomImageInfo{};
            bloomImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            bloomImageInfo.imageView = m_bloomBuffer->GetView();
            bloomImageInfo.sampler = m_linearSampler;

            postWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            postWrites[2].dstSet = m_postProcessDescSet;
            postWrites[2].dstBinding = 2;
            postWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            postWrites[2].descriptorCount = 1;
            postWrites[2].pImageInfo = &bloomImageInfo;

            vkUpdateDescriptorSets(m_device->GetDevice(), static_cast<uint32_t>(postWrites.size()), postWrites.data(), 0, nullptr);
        }
    }

    void SkyRenderer::TransitionImageToLayout(VkCommandBuffer cmd, VkImage image, VkImageLayout newLayout) {
       
        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; // просто гарантируем корректный переход
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;

        VkDependencyInfo depInfo{};
        depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &barrier;

        vkCmdPipelineBarrier2(cmd, &depInfo);
    }

    void SkyRenderer::TransitionAllStorageImagesToGeneral(VkCommandBuffer cmd) {
        TransitionImageToLayout(cmd, m_cloudNoiseTexture->GetHandle(), VK_IMAGE_LAYOUT_GENERAL);
        TransitionImageToLayout(cmd, m_cloudDetailNoise->GetHandle(), VK_IMAGE_LAYOUT_GENERAL);
        TransitionImageToLayout(cmd, m_starTexture->GetHandle(), VK_IMAGE_LAYOUT_GENERAL);
        TransitionImageToLayout(cmd, m_transmittanceLUT->GetHandle(), VK_IMAGE_LAYOUT_GENERAL);
        // добавь остальные storage images по аналогии
    }
} // namespace Renderer