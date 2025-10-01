#pragma once

#include "rhi/vulkan/vulkan_common.h"
#include "renderer/lighting_types.h"

namespace RHI::Vulkan {
	class Device;
	class Buffer;
	class ShaderManager;
	class ResourceManager;
	class ReloadablePipeline;
	class DescriptorAllocator;
	class DescriptorLayoutCache;
	class Mesh;
}

namespace Renderer {

	class MaterialSystem;
	class Material;

	template<typename T>
	std::unique_ptr<RHI::Vulkan::Buffer> CreateUniformBuffer(RHI::Vulkan::Device* device) {
		return std::make_unique<RHI::Vulkan::Buffer>(
			device,
			sizeof(T),
			VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT,
			VMA_MEMORY_USAGE_CPU_TO_GPU
		);
	}

	struct CubePushConstants {
		glm::mat4 model;
		glm::mat4 normalMatrix;
	};

	class CubeRenderer {
	public:
		CubeRenderer(RHI::Vulkan::Device* device, RHI::Vulkan::ShaderManager* shaderManager,
			MaterialSystem* materialSystem,
			RHI::Vulkan::ResourceManager* resourceManager,
			VkFormat colorFormat, VkFormat depthFormat);

		~CubeRenderer();

		void UpdateUniforms(const glm::mat4& view, const glm::mat4& projection,
			                const glm::vec3& cameraPos, float time,
			                const std::vector<LightData>& lights,
			                const glm::vec3& ambientColor);

		void Render(VkCommandBuffer cmd, VkImageView colorTarget, VkImageView depthTarget,
			VkExtent2D extent, const glm::mat4& model);

	private:
		void CreateUniformBuffers();
		void CreateDescriptorSets();
		void UpdateDescriptorSets();

		RHI::Vulkan::Device* m_device;
		RHI::Vulkan::ShaderManager* m_shaderManager;
		MaterialSystem* m_materialSystem;
		RHI::Vulkan::ResourceManager* m_resourceManager;



		VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;
		std::unique_ptr<RHI::Vulkan::DescriptorAllocator> m_descriptorAllocator;


		std::unique_ptr<RHI::Vulkan::Buffer> m_uniformBuffer;
		SceneUBO m_sceneUBO{};

		RHI::Vulkan::Mesh* m_cubeMesh;
		Material* m_material = nullptr;
	};
}
