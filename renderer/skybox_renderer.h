#pragma once

#include "rhi/vulkan/vulkan_common.h"
#include "rhi/vulkan/shader_manager.h"
#include "rhi/vulkan/resource.h"

namespace RHI::Vulkan {
	class Device;
	class ShaderManager;
	//class Image;
	class Sampler;
	class ReloadablePipeline;
}

namespace Core {
	class CoreContext;
}

namespace Renderer {

	class SkyboxRenderer {
	public:
		SkyboxRenderer(
			RHI::Vulkan::Device* device,
			Core::CoreContext* context,
			RHI::Vulkan::ShaderManager* shaderManager,
			VkFormat colorFormat,
			VkFormat depthFormat
		);

		~SkyboxRenderer();

		// Отрисовка skybox
		void Render(
			VkCommandBuffer cmd,
			VkImageView colorTarget,
			const glm::mat4& projection,
			const glm::mat4& view,
			VkExtent2D extent
		);

		// Загрузка новой текстуры skybox
		void LoadSkybox(const std::vector<std::string>& faces);

		// Загрузка из HDRI файла (equirectangular)
		void LoadFromHDRI(const std::string& hdriPath, uint32_t cubemapSize = 1024);

		VkImageView GetSkyboxView() const {
			return m_skyboxTexture != nullptr ? m_skyboxTexture->GetView() : VK_NULL_HANDLE;
		}

		VkSampler GetSkyboxSampler() const {
			return m_sampler;
		}

	private:
		void LoadSkyboxTexture();
		void LoadSkyboxFromHDRI();
		void CreateSampler();
		void CreatePipeline();
		void CreateDescriptorSet();

		// Helper для загрузки изображения
		struct ImageData {
			unsigned char* pixels = nullptr;
			int width = 0;
			int height = 0;

			~ImageData();
		};

		ImageData LoadImage(const std::string& path);


		RHI::Vulkan::Device* m_device = nullptr;
		Core::CoreContext* m_context = nullptr;
		RHI::Vulkan::ShaderManager* m_shaderManager = nullptr;

		VkFormat m_colorFormat;
		VkFormat m_depthFormat;

		// Путь к HDRI файлу (опционально)
		std::string m_hdriPath;
		uint32_t m_cubemapSize;

		std::vector<std::string> m_cubemapFaces;

		// Resources
		std::unique_ptr<RHI::Vulkan::Image> m_skyboxTexture;
		std::unique_ptr<RHI::Vulkan::Sampler> m_samplerObj;
		VkSampler m_sampler = VK_NULL_HANDLE;

		// Pipeline
		std::unique_ptr<RHI::Vulkan::ReloadablePipeline> m_pipeline;

		// Descriptors
		VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
		VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
		VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;
	public:
		enum class SkyboxQuality {
			Low = 512,
			Medium = 1024,
			High = 2048,
			Ultra = 4096
		};

		using Quality = SkyboxQuality;
	};
}
