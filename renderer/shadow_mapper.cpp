// engine/renderer/shadow_mapper.cpp
#include "shadow_mapper.h"
#include "rhi/vulkan/device.h"
#include "rhi/vulkan/resource.h"


namespace Renderer {

ShadowMapper::ShadowMapper(RHI::Vulkan::Device* device, uint32_t shadowMapSize, uint32_t cascadeCount)
    : m_device(device), m_shadowMapSize(shadowMapSize), m_cascadeCount(cascadeCount) {
    
    m_cascades.resize(cascadeCount);
    m_cascadeSplits.resize(cascadeCount + 1);
    
    CreateShadowMaps();
    CreateShadowSampler();
}

ShadowMapper::~ShadowMapper() {
    if (m_shadowSampler) {
        vkDestroySampler(m_device->GetDevice(), m_shadowSampler, nullptr);
    }
}

void ShadowMapper::CreateShadowMaps() {
    m_shadowMaps.reserve(m_cascadeCount);
    
    for (uint32_t i = 0; i < m_cascadeCount; i++) {
		RHI::Vulkan::ImageDesc shadowDesc{};
		shadowDesc.width = m_shadowMapSize;
		shadowDesc.height = m_shadowMapSize;
		shadowDesc.depth = 1;
		shadowDesc.arrayLayers = 1; // одна текстура на один каскад
		shadowDesc.mipLevels = 1;
		shadowDesc.format = VK_FORMAT_D32_SFLOAT;
		shadowDesc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		shadowDesc.imageType = VK_IMAGE_TYPE_2D;
		shadowDesc.tiling = VK_IMAGE_TILING_OPTIMAL;
		shadowDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		shadowDesc.samples = VK_SAMPLE_COUNT_1_BIT;
		shadowDesc.aspectFlags = VK_IMAGE_ASPECT_DEPTH_BIT;
		shadowDesc.memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY;
		shadowDesc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		m_shadowMaps.push_back(std::make_unique<RHI::Vulkan::Image>(m_device, shadowDesc));
    }
}

void ShadowMapper::CreateShadowSampler() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_TRUE;
    samplerInfo.compareOp = VK_COMPARE_OP_LESS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    
    VK_CHECK(vkCreateSampler(m_device->GetDevice(), &samplerInfo, nullptr, &m_shadowSampler));
}

void ShadowMapper::UpdateCascades(const glm::mat4& viewMatrix, const glm::mat4& projMatrix,
                                 const glm::vec3& lightDirection, float nearPlane, float farPlane) {
    CalculateCascadeSplits(nearPlane, farPlane);
    
    for (uint32_t i = 0; i < m_cascadeCount; i++) {
        m_cascades[i].viewProjMatrix = CalculateLightViewProjMatrix(
            viewMatrix, projMatrix, lightDirection,
            m_cascadeSplits[i], m_cascadeSplits[i + 1]
        );
        m_cascades[i].splitDistance = m_cascadeSplits[i + 1];
    }
}

void ShadowMapper::CalculateCascadeSplits(float nearPlane, float farPlane) {
    float lambda = 0.95f; // Blend between linear and logarithmic split
    float ratio = farPlane / nearPlane;
    
    m_cascadeSplits[0] = nearPlane;
    
    for (uint32_t i = 1; i < m_cascadeCount; i++) {
        float si = i / static_cast<float>(m_cascadeCount);
        float logarithmic = nearPlane * std::pow(ratio, si);
        float linear = nearPlane + (farPlane - nearPlane) * si;
        m_cascadeSplits[i] = lambda * logarithmic + (1.0f - lambda) * linear;
    }
    
    m_cascadeSplits[m_cascadeCount] = farPlane;
}

glm::mat4 ShadowMapper::CalculateLightViewProjMatrix(const glm::mat4& viewMatrix, const glm::mat4& projMatrix,
                                                    const glm::vec3& lightDirection, float nearSplit, float farSplit) {
    // Get frustum corners in world space
    glm::mat4 invViewProj = glm::inverse(projMatrix * viewMatrix);
    
    std::vector<glm::vec4> frustumCorners = {
        {-1, -1, 0, 1}, {1, -1, 0, 1}, {1, 1, 0, 1}, {-1, 1, 0, 1},
        {-1, -1, 1, 1}, {1, -1, 1, 1}, {1, 1, 1, 1}, {-1, 1, 1, 1}
    };
    
    for (auto& corner : frustumCorners) {
        corner = invViewProj * corner;
        corner /= corner.w;
    }
    
    // Adjust corners for cascade split
    for (int i = 0; i < 4; i++) {
        glm::vec4 nearCorner = frustumCorners[i];
        glm::vec4 farCorner = frustumCorners[i + 4];
        
        float nearRatio = (nearSplit - 0.1f) / (1000.0f - 0.1f); // Assuming default near/far
        float farRatio = (farSplit - 0.1f) / (1000.0f - 0.1f);
        
        frustumCorners[i] = nearCorner + (farCorner - nearCorner) * nearRatio;
        frustumCorners[i + 4] = nearCorner + (farCorner - nearCorner) * farRatio;
    }
    
    // Calculate bounding sphere
    glm::vec3 center(0.0f);
    for (const auto& corner : frustumCorners) {
        center += glm::vec3(corner);
    }
    center /= frustumCorners.size();
    
    float radius = 0.0f;
    for (const auto& corner : frustumCorners) {
        radius = std::max(radius, glm::length(glm::vec3(corner) - center));
    }
    
    // Create light view matrix
    glm::vec3 lightPos = center - lightDirection * radius * 2.0f;
    glm::mat4 lightView = glm::lookAt(lightPos, center, glm::vec3(0, 1, 0));
    
    // Create light projection matrix
    glm::mat4 lightProj = glm::ortho(-radius, radius, -radius, radius, 0.0f, radius * 4.0f);
    
    // Stabilize shadow edges (snap to texel grid)
    glm::mat4 lightViewProj = lightProj * lightView;
    glm::vec4 shadowOrigin = lightViewProj * glm::vec4(0, 0, 0, 1);
    shadowOrigin *= m_shadowMapSize / 2.0f;
    
    glm::vec2 roundedOrigin = glm::round(glm::vec2(shadowOrigin));
    glm::vec2 offset = roundedOrigin - glm::vec2(shadowOrigin);
    offset *= 2.0f / m_shadowMapSize;
    
    lightProj[3][0] += offset.x;
    lightProj[3][1] += offset.y;
    
    return lightProj * lightView;
}

void ShadowMapper::BeginShadowPass(VkCommandBuffer cmd, uint32_t cascade) {
    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = m_shadowMaps[cascade]->GetView();
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil = {1.0f, 0};
    
    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.extent = {m_shadowMapSize, m_shadowMapSize};
    renderingInfo.layerCount = 1;
    renderingInfo.pDepthAttachment = &depthAttachment;
    
    vkCmdBeginRendering(cmd, &renderingInfo);
    
    // Set viewport
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(m_shadowMapSize);
    viewport.height = static_cast<float>(m_shadowMapSize);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    
    VkRect2D scissor{};
    scissor.extent = {m_shadowMapSize, m_shadowMapSize};
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    
    // Set depth bias for shadow mapping
    vkCmdSetDepthBias(cmd, m_constantBias, 0.0f, m_slopeBias);
}

void ShadowMapper::EndShadowPass(VkCommandBuffer cmd) {
    vkCmdEndRendering(cmd);
}

VkImageView ShadowMapper::GetShadowMapView(uint32_t cascade) const {
    return m_shadowMaps[cascade]->GetView();
}

} // namespace Renderer