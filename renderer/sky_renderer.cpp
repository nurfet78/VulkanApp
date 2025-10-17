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
		int atmosphereSamples;
		int cloudSamples;
		bool volumetricClouds;
		bool temporalAccumulation;
		int cloudOctaves;
    };

    static const QualitySettings g_qualityPresets[] = {
		{8,  16, false, false, 2},  // Low
	    {16, 32, true,  false, 3},  // Medium
	    {32, 64, true,  true,  4},  // High
	    {64, 128, true, true,  5}   // Ultra
    };

    SkyRenderer::SkyRenderer(RHI::Vulkan::Device* device,
        Core::CoreContext* context,
        RHI::Vulkan::ShaderManager* shaderManager,
        VkExtent2D currentExtent,
        VkFormat colorFormat,
        VkFormat depthFormat)
        : m_device(device)
        , m_context(context)
        , m_shaderManager(shaderManager)
        , m_currentExtent(currentExtent)
        , m_colorFormat(colorFormat) 
        , m_depthFormat(depthFormat) {

        SetQualityProfile(Renderer::SkyQualityProfile::High);
        //SetAutoAnimate(true);

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

		CreateSunTexturePipeline();   
		CreateSunBillboardPipeline();

        // Create descriptor sets
        CreateDescriptorSets();

        // Initial setup
        SetAnimationSpeed(0.5f);
        SetTimeOfDay(TimeStringToFloat("12:00"));
		InitCloudeLayers();
        UpdateUniformBuffers();

        m_needsCloudGeneration = true;
        m_needsLUTGeneration = true;
        m_needsStarGeneration = true;

		// Генерируем процедурный контент
		VkCommandBuffer cmd = m_context->GetCommandPoolManager()->BeginSingleTimeCommandsCompute();
		GenerateAtmosphereLUT(cmd);
		GenerateCloudNoise(cmd);
		GenerateStarTexture(cmd);
        GenerateSunTexture(cmd);
		m_context->GetCommandPoolManager()->EndSingleTimeCommandsCompute(cmd);

		// Переводим placeholder текстуры через существующую функцию
		// Она использует отдельный command buffer на graphics queue
		m_milkyWayTexture->TransitionLayout(
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			m_context
		);

		m_moonTexture->TransitionLayout(
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			m_context
		);

        InitializeRenderTargetLayouts();

		m_cloudLayers.push_back({ 2000.0f, 500.0f, 0.5f, 0.1f, 1.0f, 0 }); // Cumulus
		m_cloudLayers.push_back({ 8000.0f, 200.0f, 0.3f, 0.2f, 2.0f, 2 }); // Cirrus
    }

    SkyRenderer::~SkyRenderer() {
       
		if (!m_device) return;

		vkDeviceWaitIdle(m_device->GetDevice());

		// 1. Уничтожаем пайплайны (они могут ссылаться на дескрипторы)
		m_atmospherePipeline.reset();
		m_cloudPipeline.reset();
		m_starsPipeline.reset();
		m_postProcessPipeline.reset();
		m_atmosphereLUTPipeline.reset();
		m_cloudNoisePipeline.reset();
		m_starGeneratorPipeline.reset();
		m_sunBillboardPipeline.reset();
		m_sunTexturePipeline.reset();

		// 2. Уничтожаем дескрипторный пул (это освободит все дескрипторные сеты)
		if (m_descriptorPool) {
			vkDestroyDescriptorPool(m_device->GetDevice(), m_descriptorPool, nullptr);
			m_descriptorPool = VK_NULL_HANDLE;
		}

		// 3. Уничтожаем дескрипторные layouts
		if (m_sunBillboardDescSetLayout) {
			vkDestroyDescriptorSetLayout(m_device->GetDevice(), m_sunBillboardDescSetLayout, nullptr);
		}
		if (m_sunTextureDescSetLayout) {
			vkDestroyDescriptorSetLayout(m_device->GetDevice(), m_sunTextureDescSetLayout, nullptr);
		}
		if (m_atmosphereDescSetLayout) {
			vkDestroyDescriptorSetLayout(m_device->GetDevice(), m_atmosphereDescSetLayout, nullptr);
			m_atmosphereDescSetLayout = VK_NULL_HANDLE;
		}
		if (m_cloudDescSetLayout) {
			vkDestroyDescriptorSetLayout(m_device->GetDevice(), m_cloudDescSetLayout, nullptr);
			m_cloudDescSetLayout = VK_NULL_HANDLE;
		}
		if (m_starsDescSetLayout) {
			vkDestroyDescriptorSetLayout(m_device->GetDevice(), m_starsDescSetLayout, nullptr);
			m_starsDescSetLayout = VK_NULL_HANDLE;
		}
		if (m_postProcessDescSetLayout) {
			vkDestroyDescriptorSetLayout(m_device->GetDevice(), m_postProcessDescSetLayout, nullptr);
			m_postProcessDescSetLayout = VK_NULL_HANDLE;
		}
		if (m_atmosphereLUTDescSetLayout) {
			vkDestroyDescriptorSetLayout(m_device->GetDevice(), m_atmosphereLUTDescSetLayout, nullptr);
			m_atmosphereLUTDescSetLayout = VK_NULL_HANDLE;
		}
		if (m_cloudNoiseDescSetLayout) {
			vkDestroyDescriptorSetLayout(m_device->GetDevice(), m_cloudNoiseDescSetLayout, nullptr);
			m_cloudNoiseDescSetLayout = VK_NULL_HANDLE;
		}
		if (m_starGeneratorDescSetLayout) {
			vkDestroyDescriptorSetLayout(m_device->GetDevice(), m_starGeneratorDescSetLayout, nullptr);
			m_starGeneratorDescSetLayout = VK_NULL_HANDLE;
		}

		// 4. Уничтожаем samplers (через unique_ptr wrapper, если есть)
		m_linearSamplerObj.reset();
		m_nearestSamplerObj.reset();
		m_cloudSamplerObj.reset();

		// 5. Уничтожаем изображения (render targets)
		m_skyBuffer.reset();
		m_cloudBuffer.reset();
		m_bloomBuffer.reset();
		m_historyBuffer.reset();

		// 6. Уничтожаем текстуры
        m_sunBillboardTexture.reset();
		m_starTexture.reset();
		m_cloudNoiseTexture.reset();
		m_cloudDetailNoise.reset();
		m_milkyWayTexture.reset();
		m_moonTexture.reset();

		// 7. Уничтожаем LUT'ы
		m_transmittanceLUT.reset();
		m_multiScatteringLUT.reset();

		// 8. Уничтожаем uniform buffers
		m_atmosphereUBO.reset();
		m_cloudUBO.reset();
		m_starUBO.reset();
    }

    void SkyRenderer::Render(VkCommandBuffer cmd,
        VkImageView targetImageView,
        VkImage targetImage,
        VkExtent2D extent,
        const glm::mat4& projection,
        const glm::mat4& viewRotationOnly,
        const glm::vec3& cameraPos) {

		if (m_skyBuffer && (m_currentExtent.width != extent.width ||
			                m_currentExtent.height != extent.height)) {

			vkDeviceWaitIdle(m_device->GetDevice());

			// Пересоздаем render targets
			m_skyBuffer.reset();
			m_cloudBuffer.reset();
			m_bloomBuffer.reset();
			if (m_historyBuffer) m_historyBuffer.reset();

			m_currentExtent = extent;
			CreateRenderTargets();
			InitializeRenderTargetLayouts();
			UpdateDescriptorSets();
		}

		m_currentExtent = extent;
		m_currentProjection = projection;
		m_currentView = viewRotationOnly;
		m_currentCameraPos = cameraPos;

		// Update uniform buffers
		//UpdateUniformBuffers();

		auto startTime = std::chrono::high_resolution_clock::now();

		RenderAtmosphere(cmd);
        RenderSunBillboard(cmd);

		auto atmosphereTime = std::chrono::high_resolution_clock::now();
		m_stats.atmospherePassMs = std::chrono::duration<float, std::milli>(atmosphereTime - startTime).count();

		// Render stars
		float dayNightBlend = glm::smoothstep(0.3f, 0.7f,
			glm::dot(m_skyParams.sunDirection, glm::vec3(0, 1, 0)));
		if (dayNightBlend < 0.9f) {
			RenderStars(cmd);
			RenderMoon(cmd);
		}

		auto starsTime = std::chrono::high_resolution_clock::now();
		m_stats.starPassMs = std::chrono::duration<float, std::milli>(starsTime - atmosphereTime).count();

		if (m_skyParams.cloudCoverage > 0.01f) {
			 RenderClouds(cmd);
		}

		auto cloudsTime = std::chrono::high_resolution_clock::now();
		m_stats.cloudPassMs = std::chrono::duration<float, std::milli>(cloudsTime - starsTime).count();

		PostProcess(cmd, targetImageView, targetImage);

		auto endTime = std::chrono::high_resolution_clock::now();
		m_stats.postProcessMs = std::chrono::duration<float, std::milli>(endTime - cloudsTime).count();
		m_stats.totalMs = std::chrono::duration<float, std::milli>(endTime - startTime).count();

		m_previousViewProj = projection * viewRotationOnly;
		m_frameIndex++;
    }

    void SkyRenderer::RenderAtmosphere(VkCommandBuffer cmd) {

		{
			// Ensure sky buffer is in COLOR_ATTACHMENT_OPTIMAL before BeginRendering
			VkImageMemoryBarrier2 toAttachment{};
			toAttachment.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;

			// We're coming from shader-read (previous frame) -> need to write as color attachment now
			toAttachment.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
			toAttachment.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
			toAttachment.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
			toAttachment.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;

			
			if (m_frameIndex == 0) {
				toAttachment.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; // <-- используем UNDEFINED
			}
			else {
				toAttachment.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			}
			toAttachment.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			toAttachment.image = m_skyBuffer->GetHandle();
			toAttachment.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

			VkDependencyInfo depInfo{};
			depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
			depInfo.imageMemoryBarrierCount = 1;
			depInfo.pImageMemoryBarriers = &toAttachment;

			vkCmdPipelineBarrier2(cmd, &depInfo);
		}

		VkClearValue clearValue = {};
		clearValue.color = { {0.0f, 0.0f, 0.0f, 0.0f} };

		VkRenderingAttachmentInfo colorAttachment = {};
		colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		colorAttachment.imageView = m_skyBuffer->GetView();
		colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		// ВАЖНО: для первого кадра image уже в COLOR_ATTACHMENT_OPTIMAL
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		colorAttachment.clearValue = clearValue; //clearValue;

		VkRenderingInfo renderInfo = {};
		renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
		renderInfo.renderArea = { 0, 0, m_currentExtent.width, m_currentExtent.height };
		renderInfo.layerCount = 1;
		renderInfo.colorAttachmentCount = 1;
		renderInfo.pColorAttachments = &colorAttachment;

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

		vkCmdBeginRendering(cmd, &renderInfo);

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_atmospherePipeline->GetPipeline());

		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
			m_atmospherePipeline->GetLayout(),
			0, 1, &m_atmosphereDescSet, 0, nullptr);

		struct PushConstants {
			glm::mat4 invViewProj;
			glm::vec3 cameraPos;
			float time;
		} pushConstants;

		pushConstants.invViewProj = glm::inverse(m_currentProjection * m_currentView);
		pushConstants.cameraPos = m_currentCameraPos;
		pushConstants.time = m_skyParams.timeOfDay;

		vkCmdPushConstants(cmd, m_atmospherePipeline->GetLayout(),
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			0, sizeof(pushConstants), &pushConstants);

		vkCmdDraw(cmd, 3, 1, 0, 0);

		vkCmdEndRendering(cmd);

		// Переводим из COLOR_ATTACHMENT в SHADER_READ_ONLY
		VkImageMemoryBarrier2 barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
		barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
		barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
		barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT |
			VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
		barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
			VK_ACCESS_2_SHADER_READ_BIT;
		barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; // ИЗМЕНЕНО с COLOR_ATTACHMENT
		barrier.image = m_skyBuffer->GetHandle();
		barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

		VkDependencyInfo depInfo{};
		depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		depInfo.imageMemoryBarrierCount = 1;
		depInfo.pImageMemoryBarriers = &barrier;

		vkCmdPipelineBarrier2(cmd, &depInfo);
    }

	void SkyRenderer::RenderSunBillboard(VkCommandBuffer cmd) {
		// Transition sky buffer to attachment
		{
			VkImageMemoryBarrier2 toAttachment{};
			toAttachment.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
			toAttachment.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
			toAttachment.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
			toAttachment.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
			toAttachment.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
				VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
			toAttachment.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			toAttachment.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			toAttachment.image = m_skyBuffer->GetHandle();
			toAttachment.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

			VkDependencyInfo depInfo{};
			depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
			depInfo.imageMemoryBarrierCount = 1;
			depInfo.pImageMemoryBarriers = &toAttachment;

			vkCmdPipelineBarrier2(cmd, &depInfo);
		}

		VkRenderingAttachmentInfo colorAttachment{};
		colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		colorAttachment.imageView = m_skyBuffer->GetView();
		colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

		VkRenderingInfo renderInfo{};
		renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
		renderInfo.renderArea = { 0, 0, m_currentExtent.width, m_currentExtent.height };
		renderInfo.layerCount = 1;
		renderInfo.colorAttachmentCount = 1;
		renderInfo.pColorAttachments = &colorAttachment;

		vkCmdBeginRendering(cmd, &renderInfo);

		// Set viewport and scissor
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

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_sunBillboardPipeline->GetPipeline());
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
			m_sunBillboardPipeline->GetLayout(),
			0, 1, &m_sunBillboardDescSet, 0, nullptr);

		struct PushConstants {
			glm::mat4 viewProj;
			float sunSize;
			float padding1[3];
			glm::vec3 cameraPos;
			float aspectRatio;
			float padding2[3];
		} push;

		push.viewProj = m_currentProjection * m_currentView;
		push.sunSize = 0.08f; // Размер в "мировых единицах" (относительный)
		push.cameraPos = m_currentCameraPos;
		push.aspectRatio = (float)m_currentExtent.width / (float)m_currentExtent.height;

		vkCmdPushConstants(cmd, m_sunBillboardPipeline->GetLayout(),
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			0, sizeof(push), &push);

		vkCmdDraw(cmd, 6, 1, 0, 0);

		vkCmdEndRendering(cmd);

		// Transition back to shader read
		{
			VkImageMemoryBarrier2 toShaderRead{};
			toShaderRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
			toShaderRead.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
			toShaderRead.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
			toShaderRead.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
			toShaderRead.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
			toShaderRead.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			toShaderRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			toShaderRead.image = m_skyBuffer->GetHandle();
			toShaderRead.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

			VkDependencyInfo depInfo{};
			depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
			depInfo.imageMemoryBarrierCount = 1;
			depInfo.pImageMemoryBarriers = &toShaderRead;

			vkCmdPipelineBarrier2(cmd, &depInfo);
		}
	}

    void SkyRenderer::RenderClouds(VkCommandBuffer cmd) {

		{
			// Ensure sky buffer is in COLOR_ATTACHMENT_OPTIMAL before BeginRendering
			VkImageMemoryBarrier2 toAttachment{};
			toAttachment.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;

			// We're coming from shader-read (previous frame) -> need to write as color attachment now
			toAttachment.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
			toAttachment.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
			toAttachment.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
			toAttachment.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;


			if (m_frameIndex == 0) {
				toAttachment.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; // <-- используем UNDEFINED
			}
			else {
				toAttachment.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			}
			toAttachment.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			toAttachment.image = m_cloudBuffer->GetHandle();
			toAttachment.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

			VkDependencyInfo depInfo{};
			depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
			depInfo.imageMemoryBarrierCount = 1;
			depInfo.pImageMemoryBarriers = &toAttachment;

			vkCmdPipelineBarrier2(cmd, &depInfo);
		}

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

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_cloudPipeline->GetPipeline());
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
			m_cloudPipeline->GetLayout(),
			0, 1, &m_cloudDescSet, 0, nullptr);

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

		vkCmdDraw(cmd, 3, 1, 0, 0);

		vkCmdEndRendering(cmd);

		// Барьер для перевода в SHADER_READ_ONLY
		VkImageMemoryBarrier2 barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
		barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
		barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
		barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
		barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
		barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.image = m_cloudBuffer->GetHandle();
		barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

		VkDependencyInfo depInfo{};
		depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		depInfo.imageMemoryBarrierCount = 1;
		depInfo.pImageMemoryBarriers = &barrier;

		vkCmdPipelineBarrier2(cmd, &depInfo);
    }

    void SkyRenderer::RenderStars(VkCommandBuffer cmd) {
		// Переводим из SHADER_READ_ONLY обратно в COLOR_ATTACHMENT для записи
		VkImageMemoryBarrier2 toAttachment{};
		toAttachment.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
		toAttachment.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
		toAttachment.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
		toAttachment.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
		toAttachment.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
			VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT; // нужен READ для LOAD_OP_LOAD
		toAttachment.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		toAttachment.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		toAttachment.image = m_skyBuffer->GetHandle();
		toAttachment.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

		VkDependencyInfo depInfo{};
		depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		depInfo.imageMemoryBarrierCount = 1;
		depInfo.pImageMemoryBarriers = &toAttachment;
		vkCmdPipelineBarrier2(cmd, &depInfo);

		// Additive blending over existing sky buffer
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
		starsPush.time = m_skyParams.timeOfDay;
		starsPush.nightBlend = 1.0f - dayNightBlend;

		vkCmdPushConstants(cmd, m_starsPipeline->GetLayout(),
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			0, sizeof(starsPush), &starsPush);

		vkCmdDraw(cmd, 3, 1, 0, 0);

		vkCmdEndRendering(cmd);

		// Переводим обратно в SHADER_READ_ONLY для PostProcess
		VkImageMemoryBarrier2 toShaderRead{};
		toShaderRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
		toShaderRead.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
		toShaderRead.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
		toShaderRead.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
		toShaderRead.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
		toShaderRead.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		toShaderRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		toShaderRead.image = m_skyBuffer->GetHandle();
		toShaderRead.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

		VkDependencyInfo depInfo2{};
		depInfo2.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		depInfo2.imageMemoryBarrierCount = 1;
		depInfo2.pImageMemoryBarriers = &toShaderRead;

		vkCmdPipelineBarrier2(cmd, &depInfo2);
    }

    void SkyRenderer::RenderMoon(VkCommandBuffer cmd) {

    }

    void SkyRenderer::PostProcess(VkCommandBuffer cmd, VkImageView targetView, VkImage targetImage) {

		// Sky buffer уже в SHADER_READ_ONLY_OPTIMAL после RenderStars
	    // Cloud buffer тоже должен быть в правильном layout
	    // НЕ нужны барьеры перед рендерингом!

		VkRenderingAttachmentInfo colorAttachment = {};
		colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		colorAttachment.imageView = targetView;
		colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // Загружаем существующее содержимое (куб)
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
		postPush.time = m_skyParams.timeOfDay;

		vkCmdPushConstants(cmd, m_postProcessPipeline->GetLayout(),
			VK_SHADER_STAGE_FRAGMENT_BIT,
			0, sizeof(postPush), &postPush);

		vkCmdDraw(cmd, 3, 1, 0, 0);

		vkCmdEndRendering(cmd);
		// НЕ нужен барьер - target image будет обработан в meadow_app.cpp
    }

    void SkyRenderer::Update(float deltaTime) {
		deltaTime = glm::min(deltaTime, 0.1f);
		if (m_autoAnimate) {
			float hoursPerSecond = 0.5f * m_animationSpeed;
			SetTimeOfDay(fmod(m_skyParams.timeOfDay + deltaTime * hoursPerSecond, 24.0f));
		}

		// Update cloud animation
		m_cloudAnimationTime += deltaTime * m_skyParams.cloudSpeed;

		// Обновляем звёзды только ночью
		float dayNightBlend = glm::smoothstep(0.3f, 0.7f,
			glm::dot(m_skyParams.sunDirection, glm::vec3(0, 1, 0)));
		if (dayNightBlend < 0.9f) {
			UpdateStarField();
		}

		UpdateCloudAnimation(deltaTime);

		// Update uniform buffers
		UpdateUniformBuffers();
    }

    void SkyRenderer::SetTimeOfDay(float hours) {
        m_skyParams.timeOfDay = fmod(hours, 24.0f);

        UpdateSunMoonPositions();
        UpdateAtmosphere();
    }

	void SkyRenderer::UpdateStarField() {
		// Обновление параметров звёзд
		StarUBO starData{};

		// Интенсивность зависит от времени суток
		float dayNightBlend = glm::smoothstep(0.3f, 0.7f,
			glm::dot(m_skyParams.sunDirection, glm::vec3(0, 1, 0)));

		starData.intensity = m_skyParams.starIntensity * (1.0f - dayNightBlend);
		starData.twinkle = 0.3f + 0.2f * sin(m_skyParams.timeOfDay * 2.0f); // Мерцание
		starData.milkyWayIntensity = m_skyParams.milkyWayIntensity * (1.0f - dayNightBlend);
		starData.time = m_skyParams.timeOfDay;

		m_starUBO->Upload(&starData, sizeof(starData));
	}

	void SkyRenderer::UpdateCloudAnimation(float deltaTime) {
		// Обновляем каждый слой облаков
		for (auto& layer : m_cloudLayers) {
			// Каждый слой движется с собственной скоростью
			layer.offset += deltaTime * layer.speed;

			// Ограничиваем offset, чтобы не было overflow
			if (layer.offset > 1000.0f) {
				layer.offset = fmod(layer.offset, 1000.0f);
			}

			// Волновое изменение плотности
			float densityWave = sin(m_cloudAnimationTime * 0.05f + layer.altitude * 0.001f);
			layer.dynamicDensity = layer.coverage * (1.0f + densityWave * 0.2f);

			// Турбулентность
			float turbulence = sin(m_cloudAnimationTime * 0.1f + layer.type * 1.5f) *
				cos(m_cloudAnimationTime * 0.07f);
			layer.offset += turbulence * 0.1f * layer.speed;
		}
	}

    void SkyRenderer::UpdateSunMoonPositions() {
		// Положение солнца: Y = вверх
        float sunAngle = (m_skyParams.timeOfDay / 24.0f) * 2.0f * glm::pi<float>() - glm::half_pi<float>();

		float azimuth = glm::radians<float>(-90.0f); // восток
		float elevation = sunAngle;

		m_skyParams.sunDirection = glm::normalize(glm::vec3(
			cos(elevation) * cos(azimuth),
			sin(elevation),
			cos(elevation) * sin(azimuth)
		));

		// Интенсивность солнца растет с высотой
		float sunElevation = glm::max(m_skyParams.sunDirection.y, 0.0f);
		m_skyParams.sunIntensity = sunElevation * 20.0f; // усиление для наглядности
    }

    void SkyRenderer::UpdateAtmosphere() {
  
		float sunElevation = m_skyParams.sunDirection.y;

		// Учитываем что солнце может быть под горизонтом
		if (sunElevation < 0.0f) {
			// НОЧЬ - солнце под горизонтом
			m_skyParams.turbidity = 1.5f; // Чистая ночная атмосфера
			m_skyParams.mieCoeff = 0.3f;
			m_skyParams.mieBeta = glm::vec3(21e-6f) * 0.2f;
		}
		else if (abs(sunElevation) < 0.3f) {
			// ЗАКАТ/РАССВЕТ
			float sunsetFactor = 1.0f - sunElevation / 0.3f;
			m_skyParams.turbidity = glm::mix(2.0f, 4.0f, sunsetFactor);
			m_skyParams.mieCoeff = glm::mix(0.8f, 1.5f, sunsetFactor);
			m_skyParams.mieBeta = glm::vec3(21e-6f) * (0.8f + sunsetFactor * 0.5f);
		}
		else {
			// ДЕНЬ
			m_skyParams.turbidity = 2.0f;
			m_skyParams.mieCoeff = 0.8f;
			m_skyParams.mieBeta = glm::vec3(21e-6f) * 0.8f;
		}
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

	void SkyRenderer::AddCloudLayer(const CloudLayer& layer) {
		m_cloudLayers.push_back(layer);
	}

	void SkyRenderer::ClearCloudLayers() {
		m_cloudLayers.clear();
	}

	void SkyRenderer::SetCloudParameters(float coverage, float speed, float scale) {
		// Обновляем все слои
		for (auto& layer : m_cloudLayers) {
			layer.coverage = coverage;
			layer.speed = speed;
			layer.scale = scale;
		}
	}

    void SkyRenderer::InitCloudeLayers() {
		// Инициализируем облачные слои
		m_cloudLayers.clear();

		// Cumulus - низкие пушистые облака
		CloudLayer cumulus{};
		cumulus.altitude = 2000.0f;
		cumulus.thickness = 500.0f;
		cumulus.coverage = 0.5f;
		cumulus.speed = 0.1f;
		cumulus.scale = 1.0f;
		cumulus.type = 0;
		cumulus.offset = 0.0f;
		cumulus.dynamicDensity = cumulus.coverage;
		m_cloudLayers.push_back(cumulus);

		// Cirrus - высокие перистые облака 
		// CloudLayer cirrus{};
		// cirrus.altitude = 8000.0f;
		// cirrus.thickness = 200.0f;
		// cirrus.coverage = 0.3f;
		// cirrus.speed = 0.2f;
		// cirrus.scale = 2.0f;
		// cirrus.type = 2;
		// cirrus.offset = 0.0f;
		// cirrus.dynamicDensity = cirrus.coverage;
		// m_cloudLayers.push_back(cirrus);
    }

    void SkyRenderer::CreateUniformBuffers() {
        m_atmosphereUBO = CreateUniformBuffer<AtmosphereUBO>(m_device);
        m_cloudUBO = CreateUniformBuffer<CloudUBO>(m_device);
        m_starUBO = CreateUniformBuffer<StarUBO>(m_device);
    }

    void SkyRenderer::CreateTextures() {
		const uint32_t STAR_RESOLUTION = 2048;
		const uint32_t CLOUD_NOISE_RESOLUTION = 256;  // 3D текстуры дорогие, держим разумный размер

		// === STORAGE IMAGES (будут заполняться compute шейдерами) ===

		// Star texture (2D) - storage для compute, потом sampled
		RHI::Vulkan::ImageDesc starDesc{};
		starDesc.width = STAR_RESOLUTION;
		starDesc.height = STAR_RESOLUTION;
		starDesc.depth = 1;
		starDesc.mipLevels = 1;
		starDesc.format = VK_FORMAT_R8G8B8A8_UNORM;
		starDesc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		starDesc.imageType = VK_IMAGE_TYPE_2D;
		starDesc.tiling = VK_IMAGE_TILING_OPTIMAL;
		starDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		starDesc.samples = VK_SAMPLE_COUNT_1_BIT;
		starDesc.aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;
		starDesc.memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY;
		starDesc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		m_starTexture = std::make_unique<RHI::Vulkan::Image>(m_device, starDesc);
		// Переводим в GENERAL для compute shader записи
		m_starTexture->TransitionLayout(
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_GENERAL,
			m_context
		);

		// Cloud noise texture (3D) - storage для compute, потом sampled
		RHI::Vulkan::ImageDesc cloudNoiseDesc{};
		cloudNoiseDesc.width = CLOUD_NOISE_RESOLUTION;
		cloudNoiseDesc.height = CLOUD_NOISE_RESOLUTION;
		cloudNoiseDesc.depth = CLOUD_NOISE_RESOLUTION / 2; // 3D depth
		cloudNoiseDesc.arrayLayers = 1;    // для 3D — всегда 1
		cloudNoiseDesc.mipLevels = 1;    // без мип-уровней
		cloudNoiseDesc.format = VK_FORMAT_R8G8B8A8_UNORM;
		cloudNoiseDesc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		cloudNoiseDesc.imageType = VK_IMAGE_TYPE_3D;
		cloudNoiseDesc.tiling = VK_IMAGE_TILING_OPTIMAL;
		cloudNoiseDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		cloudNoiseDesc.samples = VK_SAMPLE_COUNT_1_BIT;
		cloudNoiseDesc.aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;
		cloudNoiseDesc.memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY;
		cloudNoiseDesc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		m_cloudNoiseTexture = std::make_unique<RHI::Vulkan::Image>(m_device, cloudNoiseDesc);
		m_cloudNoiseTexture->TransitionLayout(
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_GENERAL,
			m_context
		);

		// Cloud detail noise (3D)
		RHI::Vulkan::ImageDesc cloudDetailNoiseDesc{};
		cloudDetailNoiseDesc.width = CLOUD_NOISE_RESOLUTION / 2;
		cloudDetailNoiseDesc.height = CLOUD_NOISE_RESOLUTION / 2;
		cloudDetailNoiseDesc.depth = CLOUD_NOISE_RESOLUTION / 4; // глубина для 3D
		cloudDetailNoiseDesc.arrayLayers = 1;    // для 3D всегда 1
		cloudDetailNoiseDesc.mipLevels = 1;    // без мип-уровней
		cloudDetailNoiseDesc.format = VK_FORMAT_R8G8B8A8_UNORM;
		cloudDetailNoiseDesc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		cloudDetailNoiseDesc.imageType = VK_IMAGE_TYPE_3D;
		cloudDetailNoiseDesc.tiling = VK_IMAGE_TILING_OPTIMAL;
		cloudDetailNoiseDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		cloudDetailNoiseDesc.samples = VK_SAMPLE_COUNT_1_BIT;
		cloudDetailNoiseDesc.aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;
		cloudDetailNoiseDesc.memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY;
		cloudDetailNoiseDesc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		m_cloudDetailNoise = std::make_unique<RHI::Vulkan::Image>(m_device, cloudDetailNoiseDesc);
		m_cloudDetailNoise->TransitionLayout(
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_GENERAL,
			m_context
		);

		// === PLACEHOLDER TEXTURES (не будут заполняться, можно сразу в SHADER_READ_ONLY) ===

		// Milky Way texture (2D) - placeholder, нужно загрузить из файла или оставить чёрной
		RHI::Vulkan::ImageDesc milkyWayDesc{};
		milkyWayDesc.width = 2048;
		milkyWayDesc.height = 1024;
		milkyWayDesc.depth = 1;
		milkyWayDesc.arrayLayers = 1; // обычное 2D-изображение
		milkyWayDesc.mipLevels = 1;
		milkyWayDesc.format = VK_FORMAT_R8_UNORM;
		milkyWayDesc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT; // для загрузки из файла
		milkyWayDesc.imageType = VK_IMAGE_TYPE_2D;
		milkyWayDesc.tiling = VK_IMAGE_TILING_OPTIMAL;
		milkyWayDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		milkyWayDesc.samples = VK_SAMPLE_COUNT_1_BIT;
		milkyWayDesc.aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;
		milkyWayDesc.memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY;
		milkyWayDesc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		m_milkyWayTexture = std::make_unique<RHI::Vulkan::Image>(m_device, milkyWayDesc);
		

		// Moon texture (2D) - placeholder
		RHI::Vulkan::ImageDesc moonDesc{};
		moonDesc.width = 512;
		moonDesc.height = 512;
		moonDesc.depth = 1;
		moonDesc.arrayLayers = 1; // обычное 2D-изображение
		moonDesc.mipLevels = 1;
		moonDesc.format = VK_FORMAT_R8G8B8A8_UNORM;
		moonDesc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		moonDesc.imageType = VK_IMAGE_TYPE_2D;
		moonDesc.tiling = VK_IMAGE_TILING_OPTIMAL;
		moonDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		moonDesc.samples = VK_SAMPLE_COUNT_1_BIT;
		moonDesc.aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;
		moonDesc.memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY;
		moonDesc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		m_moonTexture = std::make_unique<RHI::Vulkan::Image>(m_device, moonDesc);

		// Sun billboard texture
		RHI::Vulkan::ImageDesc sunDesc{};
		sunDesc.width = 512;
		sunDesc.height = 512;
		sunDesc.depth = 1;
		sunDesc.mipLevels = 1;
		sunDesc.format = VK_FORMAT_R16G16B16A16_SFLOAT;
		sunDesc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		sunDesc.imageType = VK_IMAGE_TYPE_2D;
		sunDesc.tiling = VK_IMAGE_TILING_OPTIMAL;
		sunDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		sunDesc.samples = VK_SAMPLE_COUNT_1_BIT;
		sunDesc.aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;
		sunDesc.memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY;

		m_sunBillboardTexture = std::make_unique<RHI::Vulkan::Image>(m_device, sunDesc);
		m_sunBillboardTexture->TransitionLayout(
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_GENERAL,
			m_context
		);
    }

	void SkyRenderer::GenerateSunTexture(VkCommandBuffer cmd) {

        printf("Generating sun texture...\n");

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_sunTexturePipeline->GetPipeline());
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
			m_sunTexturePipeline->GetLayout(),
			0, 1, &m_sunTextureDescSet, 0, nullptr);

		vkCmdDispatch(cmd, 512 / 8, 512 / 8, 1);

		// Transition to shader read
		VkImageMemoryBarrier barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = m_sunBillboardTexture->GetHandle();
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

    void SkyRenderer::CreateLUTs() {
		// Transmittance LUT - storage для compute
		RHI::Vulkan::ImageDesc transmittanceDesc{};
		transmittanceDesc.width = 256;
		transmittanceDesc.height = 64;
		transmittanceDesc.depth = 1;
		transmittanceDesc.arrayLayers = 1;
		transmittanceDesc.mipLevels = 1;
		transmittanceDesc.format = VK_FORMAT_R32G32B32A32_SFLOAT;
		transmittanceDesc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		transmittanceDesc.imageType = VK_IMAGE_TYPE_2D;
		transmittanceDesc.tiling = VK_IMAGE_TILING_OPTIMAL;
		transmittanceDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		transmittanceDesc.samples = VK_SAMPLE_COUNT_1_BIT;
		transmittanceDesc.aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;
		transmittanceDesc.memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY;
		transmittanceDesc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		m_transmittanceLUT = std::make_unique<RHI::Vulkan::Image>(m_device, transmittanceDesc);

		m_transmittanceLUT->TransitionLayout(
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_GENERAL,
			m_context
		);

		// Multi-scattering LUT
		RHI::Vulkan::ImageDesc multiScatteringDesc{};
		multiScatteringDesc.width = 32;
		multiScatteringDesc.height = 32;
		multiScatteringDesc.depth = 1;
		multiScatteringDesc.arrayLayers = 1;
		multiScatteringDesc.mipLevels = 1;
		multiScatteringDesc.format = VK_FORMAT_R32G32B32A32_SFLOAT;
		multiScatteringDesc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		multiScatteringDesc.imageType = VK_IMAGE_TYPE_2D;
		multiScatteringDesc.tiling = VK_IMAGE_TILING_OPTIMAL;
		multiScatteringDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		multiScatteringDesc.samples = VK_SAMPLE_COUNT_1_BIT;
		multiScatteringDesc.aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;
		multiScatteringDesc.memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY;
		multiScatteringDesc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		m_multiScatteringLUT = std::make_unique<RHI::Vulkan::Image>(m_device, multiScatteringDesc);

		m_multiScatteringLUT->TransitionLayout(
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_GENERAL,
			m_context
		);
    }

    void SkyRenderer::CreateRenderTargets() {
		// Используем текущий extent или дефолтный размер
		uint32_t width = m_currentExtent.width > 0 ? m_currentExtent.width : 1920;
		uint32_t height = m_currentExtent.height > 0 ? m_currentExtent.height : 1080;

		// Sky buffer - полный размер экрана
		RHI::Vulkan::ImageDesc skyDesc{};
		skyDesc.width = width;
		skyDesc.height = height;
		skyDesc.depth = 1;
		skyDesc.arrayLayers = 1;
		skyDesc.mipLevels = 1;
		skyDesc.format = VK_FORMAT_R16G16B16A16_SFLOAT;
		skyDesc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		skyDesc.imageType = VK_IMAGE_TYPE_2D;
		skyDesc.tiling = VK_IMAGE_TILING_OPTIMAL;
		skyDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		skyDesc.samples = VK_SAMPLE_COUNT_1_BIT;
		skyDesc.aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;
		skyDesc.memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY;
		skyDesc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		m_skyBuffer = std::make_unique<RHI::Vulkan::Image>(m_device, skyDesc);

		// Cloud buffer - полный размер экрана
		RHI::Vulkan::ImageDesc cloudDesc = skyDesc; // копируем настройки
		m_cloudBuffer = std::make_unique<RHI::Vulkan::Image>(m_device, cloudDesc);

		// Bloom buffer - половина размера экрана с мипами
		RHI::Vulkan::ImageDesc bloomDesc{};
		bloomDesc.width = width / 2;
		bloomDesc.height = height / 2;
		bloomDesc.depth = 1;
		bloomDesc.arrayLayers = 1;
		bloomDesc.mipLevels = 5;
		bloomDesc.format = VK_FORMAT_R16G16B16A16_SFLOAT;
		bloomDesc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		bloomDesc.imageType = VK_IMAGE_TYPE_2D;
		bloomDesc.tiling = VK_IMAGE_TILING_OPTIMAL;
		bloomDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		bloomDesc.samples = VK_SAMPLE_COUNT_1_BIT;
		bloomDesc.aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;
		bloomDesc.memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY;
		bloomDesc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		m_bloomBuffer = std::make_unique<RHI::Vulkan::Image>(m_device, bloomDesc);

		// History buffer только если используется temporal accumulation
		if (m_useTemporalAccumulation) {
			RHI::Vulkan::ImageDesc historyDesc = skyDesc;
			m_historyBuffer = std::make_unique<RHI::Vulkan::Image>(m_device, historyDesc);
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
		AtmosphereUBO atmosphereData{};
		// Солнце и параметры рассеяния
		atmosphereData.sunDirection = m_skyParams.sunDirection;
		atmosphereData.sunIntensity = m_skyParams.sunIntensity;
		atmosphereData.rayleighBeta = m_skyParams.rayleighBeta * m_skyParams.rayleighCoeff;
		atmosphereData.mieBeta = m_skyParams.mieBeta * m_skyParams.mieCoeff;
		atmosphereData.mieG = m_skyParams.mieG;
		atmosphereData.turbidity = m_skyParams.turbidity;

		// Радиусы планеты и атмосферы
		atmosphereData.planetRadius = EARTH_RADIUS;
		atmosphereData.atmosphereRadius = ATMOSPHERE_RADIUS;

		// Время суток (можно нормализовать в диапазон 0–24 или перевести в секунды)
		atmosphereData.time = m_skyParams.timeOfDay;

		// Камера и экспозиция
		atmosphereData.cameraPos = m_currentCameraPos;
		atmosphereData.exposure = m_skyParams.exposure;

		// Загрузка в UBO
		m_atmosphereUBO->Upload(&atmosphereData, sizeof(atmosphereData));


        // Update cloud UBO
		CloudUBO cloudData{};

		if (!m_cloudLayers.empty()) {
			// Используем первый слой (основной)
			const auto& layer = m_cloudLayers[0];

			cloudData.coverage = glm::vec3(layer.dynamicDensity);
			cloudData.speed = layer.speed;
			cloudData.windDirection = glm::vec3(1.0f, 0.0f, 0.3f);
			cloudData.scale = layer.scale;
			cloudData.density = layer.dynamicDensity; // Из слоя, а не из m_skyParams
			cloudData.altitude = layer.altitude;
			cloudData.thickness = layer.thickness;
			cloudData.time = m_cloudAnimationTime;
			cloudData.octaves = m_skyParams.cloudOctaves;
			cloudData.lacunarity = m_skyParams.cloudLacunarity;
			cloudData.gain = m_skyParams.cloudGain;
			cloudData.animationOffset = layer.offset;
		}
		else {
			// Fallback - используем m_skyParams если слоёв нет
			cloudData.coverage = glm::vec3(m_skyParams.cloudCoverage);
			cloudData.speed = m_skyParams.cloudSpeed;
			cloudData.windDirection = glm::vec3(1.0f, 0.0f, 0.3f);
			cloudData.scale = m_skyParams.cloudScale;
			cloudData.density = m_skyParams.cloudDensity;
			cloudData.altitude = m_skyParams.cloudAltitude;
			cloudData.thickness = m_skyParams.cloudThickness;
			cloudData.time = m_cloudAnimationTime;
			cloudData.octaves = m_skyParams.cloudOctaves;
			cloudData.lacunarity = m_skyParams.cloudLacunarity;
			cloudData.gain = m_skyParams.cloudGain;
			cloudData.animationOffset = 0.0f;
		}

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
        const uint32_t CLOUD_NOISE_RESOLUTION = 128;

        // Bind compute pipeline
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_cloudNoisePipeline->GetPipeline());

        // Bind descriptor set
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
            m_cloudNoisePipeline->GetLayout(),
            0, 1, &m_cloudNoiseDescSet, 0, nullptr);

        // Обновляем push constants
        CloudNoisePushConstants pushConstants{};
        pushConstants.octaves = m_skyParams.cloudOctaves;    // Или ваши значения
        pushConstants.lacunarity = 2.0f;
        pushConstants.gain = 0.5f;
        pushConstants.scale = 5.0f;      // Например, из настроек качества

        vkCmdPushConstants(
            cmd,
            m_cloudNoisePipeline->GetLayout(),
            VK_SHADER_STAGE_COMPUTE_BIT,
            0,
            sizeof(CloudNoisePushConstants),
            &pushConstants
        );

        // Dispatch for 3D texture
        uint32_t groupCountX = (CLOUD_NOISE_RESOLUTION + 7) / 8;
        uint32_t groupCountY = (CLOUD_NOISE_RESOLUTION + 7) / 8;
        uint32_t groupCountZ = (CLOUD_NOISE_RESOLUTION / 4 + 7) / 8;
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
        const uint32_t STAR_RESOLUTION = 2048;

        // Generate procedural star field
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_starGeneratorPipeline->GetPipeline());
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
            m_starGeneratorPipeline->GetLayout(), 
            0, 1, &m_starGeneratorDescSet, 0, nullptr);

        uint32_t groupCount = (STAR_RESOLUTION + 7) / 8;
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
        m_atmospherePipelineCreateInfo = info; 

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
        starsPushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT; // или vertex + fragment
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

	void SkyRenderer::CreateSunTexturePipeline() {
		RHI::Vulkan::ComputePipelineCreateInfo info{};
		info.shaderProgramName = "SunTexture";

		VkDescriptorSetLayoutBinding binding{};
		binding.binding = 0;
		binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		binding.descriptorCount = 1;
		binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = 1;
		layoutInfo.pBindings = &binding;

		if (vkCreateDescriptorSetLayout(m_device->GetDevice(), &layoutInfo, nullptr,
			&m_sunTextureDescSetLayout) != VK_SUCCESS) {
			throw std::runtime_error("Failed to create sun texture descriptor set layout");
		}

		info.descriptorSetLayout = m_sunTextureDescSetLayout;

		m_sunTexturePipeline = std::make_unique<RHI::Vulkan::ComputePipeline>(m_device, m_shaderManager);
		if (!m_sunTexturePipeline->Create(info)) {
			throw std::runtime_error("Failed to create sun texture compute pipeline");
		}
	}

	void SkyRenderer::CreateSunBillboardPipeline() {
		// Два binding: текстура + atmosphere UBO
		std::vector<VkDescriptorSetLayoutBinding> bindings(2);

        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		// Binding 1: atmosphere UBO (для sunDirection)
		bindings[1].binding = 1;
		bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

		if (vkCreateDescriptorSetLayout(m_device->GetDevice(), &layoutInfo, nullptr,
			&m_sunBillboardDescSetLayout) != VK_SUCCESS) {
			throw std::runtime_error("Failed to create sun billboard descriptor set layout");
		}

		// Push constants
		VkPushConstantRange pushConstant{};
		pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
		pushConstant.offset = 0;
		pushConstant.size = 128;

		auto info = RHI::Vulkan::ReloadablePipeline::MakePipelineInfo(
			"SunBillboard",
			VK_FORMAT_R16G16B16A16_SFLOAT,
			&m_sunBillboardDescSetLayout,
			1,
			true, // Blending enabled
			VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_OP_ADD,
			VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_OP_ADD
		);

		info.pushConstants.push_back(pushConstant);
		info.depthFormat = m_depthFormat;
		info.depthTestEnable = false;  // Солнце всегда "за" всем
		info.depthWriteEnable = false;
		info.cullMode = VK_CULL_MODE_NONE;
		info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		m_sunBillboardPipeline = std::make_unique<RHI::Vulkan::ReloadablePipeline>(m_device, m_shaderManager);
		if (!m_sunBillboardPipeline->Create(info)) {
			throw std::runtime_error("Failed to create sun billboard pipeline");
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
        transmissionImageInfo.imageView = m_transmittanceLUT->GetView(); 
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
        scatteringImageInfo.imageView = m_multiScatteringLUT->GetView();
        scatteringImageInfo.sampler = VK_NULL_HANDLE;

        lutWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        lutWrites[1].dstSet = m_atmosphereLUTDescSet;
        lutWrites[1].dstBinding = 1;
        lutWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        lutWrites[1].descriptorCount = 1;
        lutWrites[1].pImageInfo = &scatteringImageInfo;

        // Binding 2: Uniform Buffer
        VkDescriptorBufferInfo lutBufferInfo = {};
        lutBufferInfo.buffer = m_atmosphereUBO->GetBuffer();
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


		// Allocate sun texture descriptor set
		allocInfo.pSetLayouts = &m_sunTextureDescSetLayout;
		if (vkAllocateDescriptorSets(m_device->GetDevice(), &allocInfo, &m_sunTextureDescSet) != VK_SUCCESS) {
			throw std::runtime_error("Failed to allocate sun texture descriptor set");
		}

		VkDescriptorImageInfo sunTexImageInfo{};
		sunTexImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		sunTexImageInfo.imageView = m_sunBillboardTexture->GetView();
		sunTexImageInfo.sampler = VK_NULL_HANDLE;

		VkWriteDescriptorSet sunTexWrite{};
		sunTexWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		sunTexWrite.dstSet = m_sunTextureDescSet;
		sunTexWrite.dstBinding = 0;
		sunTexWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		sunTexWrite.descriptorCount = 1;
		sunTexWrite.pImageInfo = &sunTexImageInfo;

		vkUpdateDescriptorSets(m_device->GetDevice(), 1, &sunTexWrite, 0, nullptr);

		// Allocate sun billboard descriptor set
		allocInfo.pSetLayouts = &m_sunBillboardDescSetLayout;
		if (vkAllocateDescriptorSets(m_device->GetDevice(), &allocInfo, &m_sunBillboardDescSet) != VK_SUCCESS) {
			throw std::runtime_error("Failed to allocate sun billboard descriptor set");
		}

        std::vector<VkWriteDescriptorSet> sunBillboardWrites(2);

		// Texture
		VkDescriptorImageInfo sunBillboardImageInfo{};
		sunBillboardImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		sunBillboardImageInfo.imageView = m_sunBillboardTexture->GetView();
		sunBillboardImageInfo.sampler = m_linearSampler;

		sunBillboardWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		sunBillboardWrites[0].dstSet = m_sunBillboardDescSet;
		sunBillboardWrites[0].dstBinding = 0;
		sunBillboardWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		sunBillboardWrites[0].descriptorCount = 1;
		sunBillboardWrites[0].pImageInfo = &sunBillboardImageInfo;

		// Atmosphere UBO
		VkDescriptorBufferInfo atmosphereBufferInfo{};
		atmosphereBufferInfo.buffer = m_atmosphereUBO->GetBuffer();
		atmosphereBufferInfo.offset = 0;
		atmosphereBufferInfo.range = sizeof(AtmosphereUBO);

		sunBillboardWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		sunBillboardWrites[1].dstSet = m_sunBillboardDescSet;
		sunBillboardWrites[1].dstBinding = 1;
		sunBillboardWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		sunBillboardWrites[1].descriptorCount = 1;
		sunBillboardWrites[1].pBufferInfo = &atmosphereBufferInfo;

		vkUpdateDescriptorSets(m_device->GetDevice(),
			static_cast<uint32_t>(sunBillboardWrites.size()),
			sunBillboardWrites.data(), 0, nullptr);
    }

	void SkyRenderer::InitializeRenderTargetLayouts() {
		m_skyBuffer->TransitionLayout(
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			m_context
		);

		m_cloudBuffer->TransitionLayout(
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			m_context
		);

		m_bloomBuffer->TransitionLayout(
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			m_context
		);

		if (m_historyBuffer) {
			m_historyBuffer->TransitionLayout(
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				m_context
			);
		}
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

	float SkyRenderer::TimeStringToFloat(const std::string& timeStr) {
		int hours = 0;
		int minutes = 0;
		char sep = ':';

		std::istringstream ss(timeStr);
		if (!(ss >> hours >> sep >> minutes) || sep != ':' || hours < 0 || hours > 23 || minutes < 0 || minutes > 59) {
			throw std::invalid_argument("Invalid time format, expected HH:MM");
		}

		return hours + minutes / 60.0f;
	}
}