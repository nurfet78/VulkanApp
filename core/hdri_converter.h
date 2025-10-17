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

		// ��������� HDRI ���� � ��������������� � cubemap
		std::unique_ptr<RHI::Vulkan::Image> ConvertEquirectangularToCubemap(
			const std::string& hdriPath,
			uint32_t cubemapSize = 1024
		);

		// ��������������� ��� ����������� ��������
		std::unique_ptr<RHI::Vulkan::Image> ConvertToCubemap(
			RHI::Vulkan::Image* equirectangularMap,
			uint32_t cubemapSize = 1024
		);

	private:
		void CreatePipeline();
		void CreateSampler();

		// ��������� HDR ���� (.hdr)
		std::unique_ptr<RHI::Vulkan::Image> LoadHDRImage(const std::string& path);

		RHI::Vulkan::Device* m_device = nullptr;
		Core::CoreContext* m_context = nullptr;
		RHI::Vulkan::ShaderManager* m_shaderManager = nullptr;

		// Pipeline
		std::unique_ptr<RHI::Vulkan::ComputePipeline> m_pipeline;
		VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
		VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;

		// Sampler ��� ������ equirectangular �����
		std::unique_ptr<RHI::Vulkan::Sampler> m_samplerObj;
		VkSampler m_sampler = VK_NULL_HANDLE;
	};
}
