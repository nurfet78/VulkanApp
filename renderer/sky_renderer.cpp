// engine/renderer/sky_renderer.cpp
#include "sky_renderer.h"
#include "rhi/vulkan/device.h"
#include "rhi/vulkan/pipeline.h"
#include "rhi/vulkan/resource.h"

namespace Renderer {

SkyRenderer::SkyRenderer(RHI::Vulkan::Device* device, VkFormat colorFormat) : m_device(device) {
    CreatePipeline();
    CreateIBLResources();
}

SkyRenderer::~SkyRenderer() {
    if (m_pipelineLayout) {
        vkDestroyPipelineLayout(m_device->GetDevice(), m_pipelineLayout, nullptr);
    }
}

void SkyRenderer::CreatePipeline() {
    // Create pipeline layout with push constants
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(glm::mat4) + sizeof(glm::vec4) * 2; // invViewProj + cameraPos + time
    
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;
    
    VK_CHECK(vkCreatePipelineLayout(m_device->GetDevice(), &layoutInfo, nullptr, &m_pipelineLayout));
    
    // Sky pipeline will be created with actual shaders
    // For now, using placeholder
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
    
    vkCmdPushConstants(cmd, m_pipelineLayout, 
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(pc), &pc);
    
    // Bind pipeline and draw fullscreen triangle
    if (m_pipeline) {
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