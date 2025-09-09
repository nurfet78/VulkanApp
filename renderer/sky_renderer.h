// engine/renderer/sky_renderer.h
#pragma once

#include "rhi/vulkan/vulkan_common.h"


namespace RHI::Vulkan {
    class Device;
    class ReloadablePipeline;
    class Image;
    class ShaderManager;
}

namespace Renderer {

class SkyRenderer {
public:
    enum class SkyType {
        Procedural,
        CubeMap,
        HDRI
    };
    
    SkyRenderer(RHI::Vulkan::Device* device,
        RHI::Vulkan::ShaderManager* shaderManager,
        VkFormat colorFormat,
        VkFormat depthFormat);
    ~SkyRenderer() = default;
    
    void SetSkyType(SkyType type) { m_skyType = type; }
    void SetSunDirection(const glm::vec3& direction) { m_sunDirection = glm::normalize(direction); }
    void SetTimeOfDay(float time) { m_timeOfDay = time; }
    
    void Render(VkCommandBuffer cmd, VkImageView targetImageView, VkExtent2D extent,
               const glm::mat4& invViewProj, const glm::vec3& cameraPos);
    
    // IBL generation
    void GenerateEnvironmentMaps(VkCommandBuffer cmd);
    VkImageView GetIrradianceMap() const;
    VkImageView GetPrefilterMap() const;
    VkImageView GetBRDFLUT() const;
    
private:
    void CreatePipeline(VkFormat colorFormat, VkFormat depthFormat);
    void CreateIBLResources();
    glm::vec3 CalculateSkyColor(const glm::vec3& rayDir) const;
    
    RHI::Vulkan::Device* m_device;
    RHI::Vulkan::ShaderManager* m_shaderManager;
    std::unique_ptr<RHI::Vulkan::ReloadablePipeline> m_pipeline;
    
    SkyType m_skyType = SkyType::Procedural;
    glm::vec3 m_sunDirection{-0.5f, -0.3f, -0.5f};
    float m_timeOfDay = 12.0f; // 0-24 hours
    
    // IBL resources
    std::unique_ptr<RHI::Vulkan::Image> m_environmentMap;
    std::unique_ptr<RHI::Vulkan::Image> m_irradianceMap;
    std::unique_ptr<RHI::Vulkan::Image> m_prefilterMap;
    std::unique_ptr<RHI::Vulkan::Image> m_brdfLUT;
    
    // Sky parameters
    struct SkyParams {
        glm::vec3 sunColor{1.0f, 0.9f, 0.7f};
        glm::vec3 horizonColor{0.7f, 0.8f, 0.9f};
        glm::vec3 zenithColor{0.4f, 0.6f, 0.9f};
        glm::vec3 groundColor{0.3f, 0.35f, 0.4f};
        float sunIntensity = 3.0f;
        float atmosphereDensity = 0.5f;
        float rayleighCoeff = 0.0025f;
        float mieCoeff = 0.0003f;
    } m_skyParams;
};

} // namespace Renderer