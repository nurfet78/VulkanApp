#include "skybox_renderer.h"

#include "external/stb/stb_image.h"

#include "rhi/vulkan/device.h"
#include "core/core_context.h"
#include "core/hdri_converter.h"


namespace Renderer {

	SkyboxRenderer::SkyboxRenderer(
		RHI::Vulkan::Device* device,
		Core::CoreContext* context,
		RHI::Vulkan::ShaderManager* shaderManager,
		VkFormat colorFormat,
		VkFormat depthFormat)
		: m_device(device)
		, m_context(context)
		, m_shaderManager(shaderManager)
		, m_colorFormat(colorFormat)
		, m_depthFormat(depthFormat)
	{
		CreateSampler();
		// НЕ вызываем LoadSkyboxTexture() здесь!
		// Пользователь сам вызовет LoadFromHDRI() или LoadSkybox()
		CreatePipeline();
		// Descriptor set создастся после загрузки текстуры
	}

	SkyboxRenderer::~SkyboxRenderer() {
		if (m_descriptorPool) {
			vkDestroyDescriptorPool(m_device->GetDevice(), m_descriptorPool, nullptr);
		}
		if (m_descriptorSetLayout) {
			vkDestroyDescriptorSetLayout(m_device->GetDevice(), m_descriptorSetLayout, nullptr);
		}
	}

	SkyboxRenderer::ImageData::~ImageData() {
		if (pixels) stbi_image_free(pixels);
	}

	void SkyboxRenderer::LoadFromHDRI(const std::string& hdriPath, uint32_t cubemapSize) {
		m_hdriPath = hdriPath;
		m_cubemapSize = cubemapSize;
		LoadSkyboxFromHDRI();

		// Пересоздаём descriptor set с новой текстурой
		CreateDescriptorSet();
	}

	void SkyboxRenderer::LoadSkybox(const std::vector<std::string>& faces) {
		if (faces.size() != 6) {
			throw std::runtime_error("Skybox requires exactly 6 face textures");
		}

		// Сохраняем пути
		m_cubemapFaces = faces;

		// Загружаем из 6 файлов
		LoadSkyboxTexture();

		// Создаём descriptor set с новой текстурой
		CreateDescriptorSet();
	}

	void SkyboxRenderer::LoadSkyboxFromHDRI() {
		// Создаём конвертер
		Core::HDRIConverter converter(m_device, m_context, m_shaderManager);

		// Конвертируем HDRI в cubemap
		m_skyboxTexture = converter.ConvertEquirectangularToCubemap(m_hdriPath, m_cubemapSize);
	}

	void SkyboxRenderer::LoadSkyboxTexture() {
		// Используем TextureLoader если он есть, или stb_image напрямую
		// Загрузка cubemap из 6 файлов
		// Порядок: +X, -X, +Y, -Y, +Z, -Z
		std::vector<std::string> faces = {
			"assets/textures/skybox/right.jpg",   // +X
			"assets/textures/skybox/left.jpg",    // -X
			"assets/textures/skybox/top.jpg",     // +Y
			"assets/textures/skybox/bottom.jpg",  // -Y
			"assets/textures/skybox/front.jpg",   // +Z
			"assets/textures/skybox/back.jpg"     // -Z
		};

		// Загружаем первое изображение чтобы узнать размеры
		auto firstImage = LoadImage(faces[0]);
		int width = firstImage.width;
		int height = firstImage.height;

		// Создаём cubemap image
		RHI::Vulkan::ImageDesc desc{};
		desc.width = width;
		desc.height = height;
		desc.depth = 1;
		desc.arrayLayers = 6; // 6 граней куба
		desc.mipLevels = 1;
		desc.format = VK_FORMAT_R8G8B8A8_SRGB;
		desc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		desc.imageType = VK_IMAGE_TYPE_2D;
		desc.tiling = VK_IMAGE_TILING_OPTIMAL;
		desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		desc.samples = VK_SAMPLE_COUNT_1_BIT;
		desc.aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;
		desc.memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY;
		desc.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT; 

		m_skyboxTexture = std::make_unique<RHI::Vulkan::Image>(m_device, desc);

		// Создаём staging buffer для всех граней
		VkDeviceSize imageSize = width * height * 4; // RGBA
		VkDeviceSize totalSize = imageSize * 6;

		// Используем Buffer с правильным конструктором
		auto stagingBuffer = std::make_unique<RHI::Vulkan::Buffer>(
			m_device,
			totalSize,
			VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT_KHR,
			VMA_MEMORY_USAGE_CPU_ONLY
		);

		// Загружаем все 6 граней в staging buffer
		unsigned char* mappedData = static_cast<unsigned char*>(stagingBuffer->Map());

		for (size_t i = 0; i < 6; i++) {
			auto imageData = LoadImage(faces[i]);

			if (imageData.width != width || imageData.height != height) {
				stagingBuffer->Unmap();
				throw std::runtime_error("Skybox face size mismatch: " + faces[i]);
			}

			// Копируем данные в staging buffer
			memcpy(mappedData + i * imageSize, imageData.pixels, imageSize);
		}

		stagingBuffer->Unmap();

		// Копируем из staging buffer в image
		VkCommandBuffer cmd = m_context->GetCommandPoolManager()->BeginSingleTimeCommands();

		// Transition image к TRANSFER_DST
		VkImageMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = m_skyboxTexture->GetHandle();
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 6;
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

		vkCmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			0, 0, nullptr, 0, nullptr, 1, &barrier);

		// Копируем каждую грань
		std::vector<VkBufferImageCopy> regions(6);
		for (uint32_t i = 0; i < 6; i++) {
			regions[i].bufferOffset = i * imageSize;
			regions[i].bufferRowLength = 0;
			regions[i].bufferImageHeight = 0;
			regions[i].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			regions[i].imageSubresource.mipLevel = 0;
			regions[i].imageSubresource.baseArrayLayer = i;
			regions[i].imageSubresource.layerCount = 1;
			regions[i].imageOffset = { 0, 0, 0 };
			regions[i].imageExtent = { static_cast<uint32_t>(width),
									 static_cast<uint32_t>(height), 1 };
		}

		vkCmdCopyBufferToImage(cmd, stagingBuffer->GetBuffer(),
			m_skyboxTexture->GetHandle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			6, regions.data());

		// Transition к SHADER_READ_ONLY
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		vkCmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			0, 0, nullptr, 0, nullptr, 1, &barrier);

		m_context->GetCommandPoolManager()->EndSingleTimeCommands(cmd);
	}

	SkyboxRenderer::ImageData SkyboxRenderer::LoadImage(const std::string& path) {
		ImageData data;
		int channels;
		data.pixels = stbi_load(path.c_str(), &data.width, &data.height, &channels, 4);

		if (!data.pixels) {
			throw std::runtime_error("Failed to load image: " + path + " - " +
				std::string(stbi_failure_reason()));
		}

		return data;
	}

	void SkyboxRenderer::CreateSampler() {
		m_samplerObj = std::make_unique<RHI::Vulkan::Sampler>(
			m_device,
			VK_FILTER_LINEAR,
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, // Для cubemap
			true,  // Anisotropic filtering
			16.0f
		);
		m_sampler = m_samplerObj->GetHandle();
	}

	void SkyboxRenderer::CreatePipeline() {
		// Descriptor set layout
		VkDescriptorSetLayoutBinding binding{};
		binding.binding = 0;
		binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		binding.descriptorCount = 1;
		binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = 1;
		layoutInfo.pBindings = &binding;

		if (vkCreateDescriptorSetLayout(m_device->GetDevice(), &layoutInfo,
			nullptr, &m_descriptorSetLayout) != VK_SUCCESS) {
			throw std::runtime_error("Failed to create skybox descriptor set layout");
		}

		// Push constants
		VkPushConstantRange pushConstant{};
		pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		pushConstant.offset = 0;
		pushConstant.size = sizeof(glm::mat4);

		// Pipeline info
		auto info = RHI::Vulkan::ReloadablePipeline::MakePipelineInfo(
			"Skybox",  // Имя программы шейдера
			m_colorFormat,
			&m_descriptorSetLayout,
			1,
			false  // Без blending
		);

		info.pushConstants.push_back(pushConstant);
		info.depthFormat = m_depthFormat;
		info.depthTestEnable = true;
		info.depthWriteEnable = false; // Не пишем в depth buffer
		info.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL; // Для z = w трюка
		info.cullMode = VK_CULL_MODE_FRONT_BIT; // Рисуем внутреннюю сторону куба
		info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		m_pipeline = std::make_unique<RHI::Vulkan::ReloadablePipeline>(
			m_device, m_shaderManager
		);

		if (!m_pipeline->Create(info)) {
			throw std::runtime_error("Failed to create skybox pipeline");
		}
	}

	void SkyboxRenderer::CreateDescriptorSet() {
		// Create descriptor pool
		VkDescriptorPoolSize poolSize{};
		poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		poolSize.descriptorCount = 1;

		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.poolSizeCount = 1;
		poolInfo.pPoolSizes = &poolSize;
		poolInfo.maxSets = 1;

		if (vkCreateDescriptorPool(m_device->GetDevice(), &poolInfo,
			nullptr, &m_descriptorPool) != VK_SUCCESS) {
			throw std::runtime_error("Failed to create descriptor pool");
		}

		// Allocate descriptor set
		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = m_descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &m_descriptorSetLayout;

		if (vkAllocateDescriptorSets(m_device->GetDevice(), &allocInfo,
			&m_descriptorSet) != VK_SUCCESS) {
			throw std::runtime_error("Failed to allocate descriptor set");
		}

		// Update descriptor set
		VkDescriptorImageInfo imageInfo{};
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageInfo.imageView = m_skyboxTexture->GetView();
		imageInfo.sampler = m_sampler;

		VkWriteDescriptorSet descriptorWrite{};
		descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrite.dstSet = m_descriptorSet;
		descriptorWrite.dstBinding = 0;
		descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		descriptorWrite.descriptorCount = 1;
		descriptorWrite.pImageInfo = &imageInfo;

		vkUpdateDescriptorSets(m_device->GetDevice(), 1, &descriptorWrite, 0, nullptr);
	}

	void SkyboxRenderer::Render(
		VkCommandBuffer cmd,
		VkImageView colorTarget,
		const glm::mat4& projection,
		const glm::mat4& view,
		VkExtent2D extent)
	{
		// Проверяем что текстура загружена
		if (!m_skyboxTexture || !m_descriptorSet) {
			return; // Ничего не рендерим
		}

		// --- Dynamic rendering setup ---
		VkRenderingAttachmentInfo colorAttachment{};
		colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		colorAttachment.imageView = colorTarget;
		colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;  // очистка перед рендером
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE; // сохранить после рендера
		colorAttachment.clearValue.color = { {0.0f, 0.0f, 0.0f, 1.0f} };

		VkRenderingInfo renderingInfo{};
		renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
		renderingInfo.renderArea.offset = { 0, 0 };
		renderingInfo.renderArea.extent = extent;
		renderingInfo.layerCount = 1;
		renderingInfo.colorAttachmentCount = 1;
		renderingInfo.pColorAttachments = &colorAttachment;

		// Начало render pass
		vkCmdBeginRendering(cmd, &renderingInfo);

		// Убираем translation из view матрицы
		glm::mat4 viewRotationOnly = glm::mat4(glm::mat3(view));
		glm::mat4 viewProj = projection * viewRotationOnly;

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
		scissor.offset = { 0, 0 };
		scissor.extent = extent;
		vkCmdSetScissor(cmd, 0, 1, &scissor);

		// Bind pipeline и descriptor set
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
			m_pipeline->GetPipeline());
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
			m_pipeline->GetLayout(), 0, 1,
			&m_descriptorSet, 0, nullptr);

		// Push constants
		vkCmdPushConstants(cmd, m_pipeline->GetLayout(),
			VK_SHADER_STAGE_VERTEX_BIT,
			0, sizeof(glm::mat4), &viewProj);

		// Draw cube (36 vertices)
		vkCmdDraw(cmd, 36, 1, 0, 0);

		// Завершение render pass
		vkCmdEndRendering(cmd);
	}
}