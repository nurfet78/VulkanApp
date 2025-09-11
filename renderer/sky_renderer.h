// engine/renderer/sky_renderer.h
#pragma once

#include "rhi/vulkan/vulkan_common.h"


namespace RHI::Vulkan {
    class Device;
    class ShaderManager;
    class ReloadablePipeline;
    class Image;
    class Buffer;
}

namespace Core {
    class Application;
}

namespace Renderer {

    class SkyRenderer {
    public:
        // Параметры неба для настройки внешнего вида
        struct SkyParams {
            glm::vec3 sunDirection = glm::vec3(-0.5f, 0.3f, -0.5f);
            glm::vec3 sunColor = glm::vec3(1.0f, 0.95f, 0.8f);
            float sunIntensity = 3.0f;
            float sunSize = 64.0f;

            glm::vec3 dayHorizonColor = glm::vec3(0.6f, 0.75f, 0.9f);
            glm::vec3 dayZenithColor = glm::vec3(0.1f, 0.3f, 0.7f);
            glm::vec3 sunsetHorizonColor = glm::vec3(0.9f, 0.5f, 0.3f);
            glm::vec3 sunsetZenithColor = glm::vec3(0.3f, 0.2f, 0.5f);
            glm::vec3 nightHorizonColor = glm::vec3(0.02f, 0.02f, 0.05f);
            glm::vec3 nightZenithColor = glm::vec3(0.0f, 0.0f, 0.02f);
            glm::vec3 groundColor = glm::vec3(0.3f, 0.25f, 0.2f);

            float cloudCoverage = 0.4f;
            float cloudSpeed = 0.02f;
            float cloudScale = 3.0f;
            float cloudHeight = 0.3f;

            float atmosphereDensity = 1.5f;
            float horizonSharpness = 3.0f;
            float starIntensity = 0.5f;
        };

        // Должна быть <= 256 байт!
        struct SkyPushConstants {
            glm::mat4 invProjection;       // 64 байта
            glm::mat4 invView;              // 64 байта
            glm::vec4 cameraPosAndTime;    // 16 байт (xyz = camera pos, w = animation time)
            glm::vec4 sunDirAndIntensity;  // 16 байт (xyz = sun dir, w = intensity)
            glm::vec4 skyParams1;           // 16 байт (x = timeOfDay, y = cloudCoverage, z = cloudSpeed, w = cloudScale)
            glm::vec4 skyParams2;           // 16 байт (x = atmosphereDensity, y = sunSize, z = starIntensity, w = horizonSharpness)
            glm::vec2 resolution;           // 8 байт
            glm::vec2 padding;              // 8 байт для выравнивания
            // Итого: 64 + 64 + 16 + 16 + 16 + 16 + 8 + 8 = 208 байт < 256 ✓
        };

        // Структура для Uniform Buffer с остальными параметрами
        struct SkyUniformData {
            glm::vec4 dayHorizonColor;      // 16 байт
            glm::vec4 dayZenithColor;        // 16 байт
            glm::vec4 sunsetHorizonColor;   // 16 байт
            glm::vec4 sunsetZenithColor;    // 16 байт
            glm::vec4 nightHorizonColor;    // 16 байт
            glm::vec4 nightZenithColor;     // 16 байт
            glm::vec4 groundColor;           // 16 байт
            glm::vec4 sunColor;              // 16 байт
            // Итого: 128 байт
        };

    public:
        SkyRenderer(RHI::Vulkan::Device* device,
            RHI::Vulkan::ShaderManager* shaderManager,
            VkFormat colorFormat,
            VkFormat depthFormat);
        ~SkyRenderer();

        void Render(VkCommandBuffer cmd,
            VkImageView targetImageView,
            VkImageView depthImageView,
            VkExtent2D extent,
            const glm::mat4& projection,
            const glm::mat4& viewRotationOnly,
            const glm::vec3& cameraPos);

        void GenerateEnvironmentMaps(VkCommandBuffer cmd);

        //VkImageView GetEnvironmentMap() const { return m_environmentMap ? m_environmentMap->GetView() : VK_NULL_HANDLE;}
        VkImageView GetIrradianceMap() const;
        VkImageView GetPrefilterMap() const;
        VkImageView GetBRDFLUT() const;

        void SetTimeOfDay(float hours) { m_timeOfDay = hours; UpdateSunPosition(); }
        float GetTimeOfDay() const { return m_timeOfDay; }

        void SetAnimationSpeed(float speed) { m_animationSpeed = speed; }
        void SetAutoAnimate(bool animate) { m_autoAnimate = animate; }
        bool GetAutoAnimate() const { return m_autoAnimate; }

        void ConfigureSky();
        void SetSkyParams(const SkyParams& params) { m_skyParams = params; UpdateSunPosition(); UpdateUniformBuffer(); }
        const SkyParams& GetSkyParams() const { return m_skyParams; }

        void Update(float deltaTime);

        glm::vec3 GetCurrentSkyColor(const glm::vec3& direction) const;
        glm::vec3 GetAmbientLight() const;

        void RecreateSwapchainResources();

    private:
        void CreatePipeline(VkFormat colorFormat, VkFormat depthFormat);
        void CreateUniformBuffer();
        void CreateDescriptorSet();
        void CreateIBLResources();
        void UpdateSunPosition();
        void UpdateUniformBuffer();
        glm::vec3 CalculateSkyColor(const glm::vec3& rayDir) const;

    private:
        RHI::Vulkan::Device* m_device;
        RHI::Vulkan::ShaderManager* m_shaderManager;
        std::unique_ptr<RHI::Vulkan::ReloadablePipeline> m_pipeline;

        // Uniform buffer для цветов
        std::unique_ptr<RHI::Vulkan::Buffer> m_uniformBuffer;
        VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;

        // IBL ресурсы
        std::unique_ptr<RHI::Vulkan::Image> m_environmentMap;
        std::unique_ptr<RHI::Vulkan::Image> m_irradianceMap;
        std::unique_ptr<RHI::Vulkan::Image> m_prefilterMap;
        std::unique_ptr<RHI::Vulkan::Image> m_brdfLUT;

        // Параметры
        SkyParams m_skyParams;
        float m_timeOfDay = 12.0f;
        float m_animationSpeed = 1.0f;
        float m_currentTime = 0.0f;
        bool m_autoAnimate = false;
        float m_cloudAnimationTime = 0.0f;
    };

} // namespace Renderer