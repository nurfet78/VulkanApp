#pragma once

#include "pch.h"

namespace RHI::Vulkan {
	class Device;
	class ShaderManager;
	class Image;
	class ComputePipeline;
	class Sampler;
}

namespace Core {

	class CoreContext;

	class HDRIConverter {
	public:
		HDRIConverter(
			RHI::Vulkan::Device* device,
			Core::CoreContext* context,
			RHI::Vulkan::ShaderManager* shaderManager
		);

		~HDRIConverter();

		// Загрузить HDRI файл и сконвертировать в cubemap
		std::unique_ptr<RHI::Vulkan::Image> ConvertEquirectangularToCubemap(
			const std::string& hdriPath,
			uint32_t cubemapSize = 1024
		);

		// Сконвертировать уже загруженную текстуру
		std::unique_ptr<RHI::Vulkan::Image> ConvertToCubemap(
			RHI::Vulkan::Image* equirectangularMap,
			uint32_t cubemapSize = 1024
		);

	private:
		void CreatePipeline();
		void CreateSampler();

		// Загрузить HDR файл (.hdr)
		std::unique_ptr<RHI::Vulkan::Image> LoadHDRImage(const std::string& path);

		RHI::Vulkan::Device* m_device = nullptr;
		Core::CoreContext* m_context = nullptr;
		RHI::Vulkan::ShaderManager* m_shaderManager = nullptr;

		// Pipeline
		std::unique_ptr<RHI::Vulkan::ComputePipeline> m_pipeline;
		VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
		VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;

		// Sampler для чтения equirectangular карты
		std::unique_ptr<RHI::Vulkan::Sampler> m_samplerObj;
		VkSampler m_sampler = VK_NULL_HANDLE;
	};
}
