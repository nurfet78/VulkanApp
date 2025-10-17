#include "external/stb/stb_image.h"

#include "hdri_converter.h"


#include "rhi/vulkan/device.h"
#include "rhi/vulkan/resource.h"
#include "core/core_context.h"
#include "rhi/vulkan/compute_pipeline.h"

namespace Core {

	HDRIConverter::HDRIConverter(
		RHI::Vulkan::Device* device,
		Core::CoreContext* context,
		RHI::Vulkan::ShaderManager* shaderManager)
		: m_device(device)
		, m_context(context)
		, m_shaderManager(shaderManager)
	{
		CreateSampler();
		CreatePipeline();
	}

	HDRIConverter::~HDRIConverter() {
		if (m_descriptorPool) {
			vkDestroyDescriptorPool(m_device->GetDevice(), m_descriptorPool, nullptr);
		}
		if (m_descriptorSetLayout) {
			vkDestroyDescriptorSetLayout(m_device->GetDevice(), m_descriptorSetLayout, nullptr);
		}
	}

	void HDRIConverter::CreateSampler() {
		m_samplerObj = std::make_unique<RHI::Vulkan::Sampler>(
			m_device,
			VK_FILTER_LINEAR,
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			true,  // anisotropic filtering
			16.0f
		);
		m_sampler = m_samplerObj->GetHandle();
	}

	void HDRIConverter::CreatePipeline() {
		// Descriptor set layout
		std::vector<VkDescriptorSetLayoutBinding> bindings(2);

		// Input: equirectangular map
		bindings[0].binding = 0;
		bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		bindings[0].descriptorCount = 1;
		bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

		// Output: cubemap
		bindings[1].binding = 1;
		bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		bindings[1].descriptorCount = 1;
		bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
		layoutInfo.pBindings = bindings.data();

		if (vkCreateDescriptorSetLayout(m_device->GetDevice(), &layoutInfo, nullptr,
			&m_descriptorSetLayout) != VK_SUCCESS) {
			throw std::runtime_error("Failed to create HDRI converter descriptor layout");
		}

		// Create descriptor pool
		std::vector<VkDescriptorPoolSize> poolSizes(2);
		poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		poolSizes[0].descriptorCount = 10;
		poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		poolSizes[1].descriptorCount = 10;

		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
		poolInfo.pPoolSizes = poolSizes.data();
		poolInfo.maxSets = 10;

		if (vkCreateDescriptorPool(m_device->GetDevice(), &poolInfo, nullptr,
			&m_descriptorPool) != VK_SUCCESS) {
			throw std::runtime_error("Failed to create HDRI converter descriptor pool");
		}

		// Create compute pipeline
		RHI::Vulkan::ComputePipelineCreateInfo pipelineInfo{};
		pipelineInfo.shaderProgramName = "EquirectToCubemap";
		pipelineInfo.descriptorSetLayout = m_descriptorSetLayout;

		m_pipeline = std::make_unique<RHI::Vulkan::ComputePipeline>(m_device, m_shaderManager);
		if (!m_pipeline->Create(pipelineInfo)) {
			throw std::runtime_error("Failed to create HDRI converter pipeline");
		}
	}

	std::unique_ptr<RHI::Vulkan::Image> HDRIConverter::LoadHDRImage(const std::string& path) {
		// Загружаем HDR файл с помощью stb_image
		int width, height, channels;
		float* pixels = stbi_loadf(path.c_str(), &width, &height, &channels, 4);

		if (!pixels) {
			throw std::runtime_error("Failed to load HDR image: " + path);
		}

		// Создаём staging buffer
		VkDeviceSize imageSize = width * height * 4 * sizeof(float);
		auto stagingBuffer = std::make_unique<RHI::Vulkan::Buffer>(
			m_device,
			imageSize,
			VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT_KHR,
			VMA_MEMORY_USAGE_CPU_ONLY
		);

		// Копируем данные
		void* mapped = stagingBuffer->Map();
		memcpy(mapped, pixels, imageSize);
		stagingBuffer->Unmap();

		stbi_image_free(pixels);

		// Создаём GPU текстуру
		RHI::Vulkan::ImageDesc desc{};
		desc.width = width;
		desc.height = height;
		desc.depth = 1;
		desc.arrayLayers = 1;
		desc.mipLevels = 1;
		desc.format = VK_FORMAT_R32G32B32A32_SFLOAT; // HDR формат
		desc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		desc.imageType = VK_IMAGE_TYPE_2D;
		desc.tiling = VK_IMAGE_TILING_OPTIMAL;
		desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		desc.samples = VK_SAMPLE_COUNT_1_BIT;
		desc.aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;
		desc.memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY;

		auto image = std::make_unique<RHI::Vulkan::Image>(m_device, desc);

		// Копируем данные из staging buffer
		VkCommandBuffer cmd = m_context->GetCommandPoolManager()->BeginSingleTimeCommandsCompute();

		// Transition to TRANSFER_DST
		VkImageMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = image->GetHandle();
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

		vkCmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			0, 0, nullptr, 0, nullptr, 1, &barrier);

		// Copy buffer to image
		VkBufferImageCopy region{};
		region.bufferOffset = 0;
		region.bufferRowLength = 0;
		region.bufferImageHeight = 0;
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.mipLevel = 0;
		region.imageSubresource.baseArrayLayer = 0;
		region.imageSubresource.layerCount = 1;
		region.imageOffset = { 0, 0, 0 };
		region.imageExtent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };

		vkCmdCopyBufferToImage(cmd, stagingBuffer->GetBuffer(),
			image->GetHandle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

		// Transition to SHADER_READ_ONLY
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		vkCmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			0, 0, nullptr, 0, nullptr, 1, &barrier);

		m_context->GetCommandPoolManager()->EndSingleTimeCommandsCompute(cmd);

		return image;
	}

	std::unique_ptr<RHI::Vulkan::Image> HDRIConverter::ConvertEquirectangularToCubemap(
		const std::string& hdriPath,
		uint32_t cubemapSize)
	{
		// Загружаем equirectangular карту
		auto equirectMap = LoadHDRImage(hdriPath);

		// Конвертируем в cubemap
		return ConvertToCubemap(equirectMap.get(), cubemapSize);
	}

	std::unique_ptr<RHI::Vulkan::Image> HDRIConverter::ConvertToCubemap(
		RHI::Vulkan::Image* equirectangularMap,
		uint32_t cubemapSize)
	{
		// Создаём cubemap для записи
		RHI::Vulkan::ImageDesc desc{};
		desc.width = cubemapSize;
		desc.height = cubemapSize;
		desc.depth = 1;
		desc.arrayLayers = 6;
		desc.mipLevels = 1;
		desc.format = VK_FORMAT_R16G16B16A16_SFLOAT; // HDR формат, 16-bit per channel
		desc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		desc.imageType = VK_IMAGE_TYPE_2D;
		desc.tiling = VK_IMAGE_TILING_OPTIMAL;
		desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		desc.samples = VK_SAMPLE_COUNT_1_BIT;
		desc.aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;
		desc.memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY;
		desc.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

		auto cubemap = std::make_unique<RHI::Vulkan::Image>(m_device, desc);

		// Создаём descriptor set
		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = m_descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &m_descriptorSetLayout;

		VkDescriptorSet descriptorSet;
		if (vkAllocateDescriptorSets(m_device->GetDevice(), &allocInfo, &descriptorSet) != VK_SUCCESS) {
			throw std::runtime_error("Failed to allocate descriptor set");
		}

		// Update descriptor set
		std::vector<VkWriteDescriptorSet> writes(2);

		// Input: equirectangular map
		VkDescriptorImageInfo inputInfo{};
		inputInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		inputInfo.imageView = equirectangularMap->GetView();
		inputInfo.sampler = m_sampler;

		writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[0].dstSet = descriptorSet;
		writes[0].dstBinding = 0;
		writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[0].descriptorCount = 1;
		writes[0].pImageInfo = &inputInfo;

		// Output: cubemap
		VkDescriptorImageInfo outputInfo{};
		outputInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		outputInfo.imageView = cubemap->GetView();

		writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[1].dstSet = descriptorSet;
		writes[1].dstBinding = 1;
		writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		writes[1].descriptorCount = 1;
		writes[1].pImageInfo = &outputInfo;

		vkUpdateDescriptorSets(m_device->GetDevice(), 2, writes.data(), 0, nullptr);

		// Execute compute shader
		VkCommandBuffer cmd = m_context->GetCommandPoolManager()->BeginSingleTimeCommandsCompute();

		// Transition cubemap to GENERAL
		VkImageMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = cubemap->GetHandle();
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 6;
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

		vkCmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			0, 0, nullptr, 0, nullptr, 1, &barrier);

		// Bind pipeline and dispatch
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline->GetPipeline());
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
			m_pipeline->GetLayout(), 0, 1, &descriptorSet, 0, nullptr);

		uint32_t groupCountX = (cubemapSize + 7) / 8;
		uint32_t groupCountY = (cubemapSize + 7) / 8;
		vkCmdDispatch(cmd, groupCountX, groupCountY, 1);

		// Transition to SHADER_READ_ONLY
		barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		vkCmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			0, 0, nullptr, 0, nullptr, 1, &barrier);

		m_context->GetCommandPoolManager()->EndSingleTimeCommandsCompute(cmd);

		return cubemap;
	}
}