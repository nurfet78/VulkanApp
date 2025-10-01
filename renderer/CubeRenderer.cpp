#include "CubeRenderer.h"

#include "rhi/vulkan/device.h"
#include "rhi/vulkan/shader_manager.h"
#include "renderer/material_system.h"
#include "rhi/vulkan/resource.h"

namespace Renderer {

	CubeRenderer::CubeRenderer(RHI::Vulkan::Device* device,
		                       RHI::Vulkan::ShaderManager* shaderManager,
		                       MaterialSystem* materialSystem,
		                       RHI::Vulkan::ResourceManager* resourceManager,
		                       VkFormat colorFormat, VkFormat depthFormat)
		: m_device(device), m_shaderManager(shaderManager),
		  m_materialSystem(materialSystem), m_resourceManager(resourceManager) {

		m_cubeMesh = m_resourceManager->GetMesh("cube");
		if (!m_cubeMesh) {
			throw std::runtime_error("Cube mesh not found");
		}

		m_descriptorAllocator = std::make_unique<RHI::Vulkan::DescriptorAllocator>(device);

		// Создать pipeline для материала
		m_materialSystem->CreatePipelineForTemplate("pbr", shaderManager, "Cube",
			colorFormat, depthFormat);

		m_materialSystem->CreateDefaultMaterials();

		m_material = m_materialSystem->GetMaterial("gold");
		
		if (!m_material) {
			throw std::runtime_error("Default material not found");
		}

		CreateUniformBuffers();
		CreateDescriptorSets();
	}

	CubeRenderer::~CubeRenderer() {
	}

	void CubeRenderer::CreateUniformBuffers() {
		m_uniformBuffer = CreateUniformBuffer<SceneUBO>(m_device);
	}

	void CubeRenderer::CreateDescriptorSets() {
		VkDescriptorSetLayout sceneLayout = m_materialSystem->GetSceneDescriptorLayout();
		if (!m_descriptorAllocator->Allocate(&m_descriptorSet, sceneLayout)) {
			throw std::runtime_error("Failed to allocate scene descriptor set");
		}
		UpdateDescriptorSets();
	}

	void CubeRenderer::UpdateDescriptorSets() {
		VkDescriptorBufferInfo bufferInfo{};
		bufferInfo.buffer = m_uniformBuffer->GetHandle();
		bufferInfo.offset = 0;
		bufferInfo.range = sizeof(SceneUBO);

		VkWriteDescriptorSet descriptorWrite{};
		descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrite.dstSet = m_descriptorSet;
		descriptorWrite.dstBinding = 0;
		descriptorWrite.dstArrayElement = 0;
		descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		descriptorWrite.descriptorCount = 1;
		descriptorWrite.pBufferInfo = &bufferInfo;

		vkUpdateDescriptorSets(m_device->GetDevice(), 1, &descriptorWrite, 0, nullptr);
	}

	void CubeRenderer::UpdateUniforms(const glm::mat4& view, const glm::mat4& projection,
		                              const glm::vec3& cameraPos, float time,
		                              const std::vector<LightData>& lights,
		                              const glm::vec3& ambientColor) {

		m_sceneUBO.view = view;
		m_sceneUBO.projection = projection;
		m_sceneUBO.cameraPos = cameraPos;
		m_sceneUBO.time = time;

		m_sceneUBO.lightCount = std::min((int)lights.size(), 8);
		for (int i = 0; i < m_sceneUBO.lightCount; i++) {
			m_sceneUBO.lights[i] = lights[i];
		}
		m_sceneUBO.ambientColor = ambientColor;

		m_uniformBuffer->Upload(&m_sceneUBO, sizeof(SceneUBO));
	}

	void CubeRenderer::Render(VkCommandBuffer cmd, VkImageView colorTarget, VkImageView depthTarget,
		VkExtent2D extent, const glm::mat4& model) {


		// Begin rendering (similar to SkyRenderer pattern)
		VkRenderingAttachmentInfo colorAttachment{};
		colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		colorAttachment.imageView = colorTarget;
		colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		colorAttachment.clearValue.color = { {0.1f, 0.1f, 0.2f, 1.0f} };

		VkRenderingAttachmentInfo depthAttachment{};
		depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		depthAttachment.imageView = depthTarget;
		depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		depthAttachment.clearValue.depthStencil = { 1.0f, 0 };

		VkRenderingInfo renderingInfo{};
		renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
		renderingInfo.renderArea = { 0, 0, extent.width, extent.height };
		renderingInfo.layerCount = 1;
		renderingInfo.colorAttachmentCount = 1;
		renderingInfo.pColorAttachments = &colorAttachment;
		renderingInfo.pDepthAttachment = &depthAttachment;

		vkCmdBeginRendering(cmd, &renderingInfo);

		// Set viewport and scissor (like in SkyRenderer)
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

		auto* materialTemplate = m_material->GetTemplate();
		auto* pipeline = materialTemplate->GetPipeline();
		if (!pipeline) {
			throw std::runtime_error("Material pipeline not initialized");
		}


		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->GetPipeline());

		// привязываем оба descriptor set (scene=0, material=1)
		m_material->Bind(cmd, m_descriptorSet);

		// Push constants
		CubePushConstants pushConstants;
		pushConstants.model = model;
		pushConstants.normalMatrix = glm::transpose(glm::inverse(model));
		
		vkCmdPushConstants(cmd, materialTemplate->GetPipelineLayout(),
			VK_SHADER_STAGE_VERTEX_BIT,
			0, sizeof(pushConstants), &pushConstants);

		// Bind vertex and index buffers
		VkBuffer vertexBuffers[] = { m_cubeMesh->vertexBuffer->GetHandle() };
		VkDeviceSize offsets[] = { 0 };
		vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
		vkCmdBindIndexBuffer(cmd, m_cubeMesh->indexBuffer->GetHandle(), 0, VK_INDEX_TYPE_UINT32);

		// Draw cube
		vkCmdDrawIndexed(cmd, static_cast<uint32_t>(m_cubeMesh->indices.size()), 1, 0, 0, 0);

		vkCmdEndRendering(cmd);
	}
}