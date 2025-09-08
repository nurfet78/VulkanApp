// engine/renderer/shadow_mapper.h
#pragma once

#include "rhi/vulkan/vulkan_common.h"


namespace RHI::Vulkan {
    class Device;
    class Image;
    class Pipeline;
}

namespace Renderer {

class ShadowMapper {
public:
    struct CascadeInfo {
        glm::mat4 viewProjMatrix;
        float splitDistance;
        float padding[3];
    };
    
    ShadowMapper(RHI::Vulkan::Device* device, uint32_t shadowMapSize = 2048, uint32_t cascadeCount = 4);
    ~ShadowMapper();
    
    void UpdateCascades(const glm::mat4& viewMatrix, const glm::mat4& projMatrix,
                       const glm::vec3& lightDirection, float nearPlane, float farPlane);
    
    void BeginShadowPass(VkCommandBuffer cmd, uint32_t cascade);
    void EndShadowPass(VkCommandBuffer cmd);
    
    VkImageView GetShadowMapView(uint32_t cascade) const;
    const CascadeInfo& GetCascadeInfo(uint32_t cascade) const { return m_cascades[cascade]; }
    uint32_t GetCascadeCount() const { return m_cascadeCount; }
    
    VkSampler GetShadowSampler() const { return m_shadowSampler; }
    
private:
    void CreateShadowMaps();
    void CreateShadowSampler();
    void CalculateCascadeSplits(float nearPlane, float farPlane);
    glm::mat4 CalculateLightViewProjMatrix(const glm::mat4& viewMatrix, const glm::mat4& projMatrix,
                                          const glm::vec3& lightDirection, float nearSplit, float farSplit);
    
    RHI::Vulkan::Device* m_device;
    uint32_t m_shadowMapSize;
    uint32_t m_cascadeCount;
    
    std::vector<std::unique_ptr<RHI::Vulkan::Image>> m_shadowMaps;
    std::vector<CascadeInfo> m_cascades;
    std::vector<float> m_cascadeSplits;
    
    VkSampler m_shadowSampler = VK_NULL_HANDLE;
    
    // Shadow bias parameters
    float m_constantBias = 0.002f;
    float m_slopeBias = 0.005f;
};

} // namespace Renderer