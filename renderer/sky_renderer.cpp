// engine/renderer/sky_renderer.cpp
#include "sky_renderer.h"
#include "rhi/vulkan/device.h"
#include "rhi/vulkan/shader_manager.h"
#include "rhi/vulkan/resource.h"

namespace Renderer {

    SkyRenderer::SkyRenderer(RHI::Vulkan::Device* device, RHI::Vulkan::ShaderManager* shaderManager, VkFormat colorFormat, VkFormat depthFormat)
        : m_device(device), m_shaderManager(shaderManager)
    {
        CreatePipeline(colorFormat, depthFormat); // Передаем форматы
        CreateIBLResources();
    }


void SkyRenderer::CreatePipeline(VkFormat colorFormat, VkFormat depthFormat) {
    RHI::Vulkan::ReloadablePipeline::CreateInfo pipelineInfo{};

    // 1. Указываем имя шейдерной программы
    pipelineInfo.shaderProgram = "Sky"; // Убедитесь, что она загружена в MeadowApp::LoadShaders

    // 2. Настраиваем Push Constants. ReloadablePipeline сам создаст layout.
    struct PushConstants {
        glm::mat4 invViewProj;
        glm::vec3 cameraPos; // В GLSL vec3 занимает место как vec4
        float time;
    };
    pipelineInfo.pushConstantSize = sizeof(PushConstants);
    pipelineInfo.pushConstantStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    // 3. Настраиваем состояние рендера
    pipelineInfo.colorFormat = colorFormat;
    pipelineInfo.depthFormat = depthFormat;

    // Для скайбокса важно правильно настроить тест глубины:
    // мы проверяем глубину, но не пишем ее. Это рисует небо "позади" всего.
    pipelineInfo.depthTestEnable = true;
    pipelineInfo.depthWriteEnable = false;
    pipelineInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL; // Важно для трюка с z=1.0

    // Мы находимся "внутри" скайбокса, поэтому отключаем отсечение
    pipelineInfo.cullMode = VK_CULL_MODE_NONE;

    // Вершинные буферы не используются
    pipelineInfo.vertexBindings = {};
    pipelineInfo.vertexAttributes = {};

    // 4. Создаем пайплайн
    m_pipeline = std::make_unique<RHI::Vulkan::ReloadablePipeline>(m_device, m_shaderManager);
    if (!m_pipeline->Create(pipelineInfo)) {
        throw std::runtime_error("Failed to create sky pipeline!");
    }
}

void SkyRenderer::CreateIBLResources() {
    // Environment map (cube map)
    m_environmentMap = std::make_unique<RHI::Vulkan::Image>(
        m_device, 512, 512, VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT
    );
    
    // Irradiance map for diffuse IBL
    m_irradianceMap = std::make_unique<RHI::Vulkan::Image>(
        m_device, 32, 32, VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT
    );
    
    // Prefilter map for specular IBL
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

void SkyRenderer::Render(VkCommandBuffer cmd, VkImageView targetImageView, VkExtent2D extent,
                        const glm::mat4& invViewProj, const glm::vec3& cameraPos) {
    // Begin rendering
    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = targetImageView;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // Keep previous content
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    
    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.extent = extent;
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    
    vkCmdBeginRendering(cmd, &renderingInfo);
    
    // Set viewport
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
    
    // Push constants
    struct PushConstants {
        glm::mat4 invViewProj;
        glm::vec3 cameraPos;
        float time;
    } pc;
    
    pc.invViewProj = invViewProj;
    pc.cameraPos = cameraPos;
    pc.time = m_timeOfDay;
    
    vkCmdPushConstants(cmd, m_pipeline->GetLayout(),
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(pc), &pc);
    
    // Bind pipeline and draw fullscreen triangle
    if (m_pipeline) {
        // Метод Bind() есть и у ReloadablePipeline
        m_pipeline->Bind(cmd);
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }

    vkCmdEndRendering(cmd);
}

void SkyRenderer::GenerateEnvironmentMaps(VkCommandBuffer cmd) {
    // Generate IBL maps from procedural sky or HDRI
    // This would involve:
    // 1. Render sky to cube map
    // 2. Generate irradiance map (diffuse convolution)
    // 3. Generate prefilter map (specular convolution)
    // 4. Generate BRDF LUT
    
    // Implementation depends on compute shaders or multiple render passes
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

glm::vec3 SkyRenderer::CalculateSkyColor(const glm::vec3& rayDir) const {
    float y = rayDir.y * 0.5f + 0.5f;
    
    glm::vec3 skyColor;
    if (rayDir.y > 0.0f) {
        skyColor = glm::mix(m_skyParams.horizonColor, m_skyParams.zenithColor, glm::pow(y, 0.5f));
    } else {
        skyColor = glm::mix(m_skyParams.horizonColor, m_skyParams.groundColor, glm::pow(-rayDir.y, 0.5f));
    }
    
    // Add sun
    float sun = glm::pow(glm::max(glm::dot(rayDir, -m_sunDirection), 0.0f), 256.0f);
    skyColor += m_skyParams.sunColor * sun * m_skyParams.sunIntensity;
    
    return skyColor;
}

} // namespace Renderer