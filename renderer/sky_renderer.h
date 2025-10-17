// SkyRenderer.h - Enhanced procedural sky rendering system
#pragma once

#include "rhi/vulkan/vulkan_common.h"
#include "rhi/vulkan/shader_manager.h"


namespace RHI::Vulkan {
    class Device;
    class ShaderManager;
    class Image;
    class Buffer;
    class CommandPoolManager;
    class ComputePipeline;
    class Sampler;
}

namespace Core {
    class CoreContext;
}

namespace vkinit {
    inline VkImageSubresourceRange image_subresource_range(VkImageAspectFlags aspectMask) {
        VkImageSubresourceRange range{};
        range.aspectMask = aspectMask;
        range.baseMipLevel = 0;
        range.levelCount = 1;
        range.baseArrayLayer = 0;
        range.layerCount = 1;
        return range;
    }
}

namespace Renderer {

	// Constants for atmosphere simulation
	constexpr float EARTH_RADIUS = 6371000.0f;      // 6371 км в метрах
	constexpr float ATMOSPHERE_RADIUS = 6471000.0f; // +100 км атмосфера
	constexpr float ATMOSPHERE_HEIGHT = 100000.0f;  // 100 км
	constexpr float SUN_ANGULAR_RADIUS = 0.00465f;    // Sun's angular radius
	constexpr float MOON_ANGULAR_RADIUS = 0.00257f;   // Moon's angular radius


    // Sky quality profiles for performance scaling
    enum class SkyQualityProfile {
        Low,     // 512x512 sky, no volumetric clouds, simple scattering
        Medium,  // 1024x1024 sky, basic volumetric clouds
        High,    // 2048x2048 sky, full volumetric clouds, temporal accumulation
        Ultra    // 4096x4096 sky, all features maxed
    };

    // Comprehensive sky parameters
    struct SkyParams {
        // Time and sun/moon
        float timeOfDay = 0.0f;              // 0-24 hours
        glm::vec3 sunDirection = glm::vec3(0, 1, 0);
        float sunIntensity = 15.0f;
        float sunAngularRadius = 0.00465f;    // Sun's angular radius in radians

        // Atmosphere
        float turbidity = 2.0f;                // Atmospheric turbidity (1-10)
        float rayleighCoeff = 0.8f;           // Rayleigh scattering multiplier
        float mieCoeff = 0.8f;                // Mie scattering multiplier
        float mieG = 0.76f;                   // Mie phase function g parameter
        glm::vec3 rayleighBeta = glm::vec3(5.8e-6f, 13.5e-6f, 33.1e-6f) * 0.4f;
        glm::vec3 mieBeta = glm::vec3(21e-6f) * 0.8f;

        // Clouds
        float cloudCoverage = 0.8f;           // 0-1
        float cloudSpeed = 0.5f;              // Cloud animation speed
        float cloudScale = 1.0f;              // Cloud detail scale
        float cloudDensity = 0.5f;            // Cloud opacity
        float cloudAltitude = 2000.0f;        // Cloud layer height in meters
        float cloudThickness = 500.0f;        // Cloud layer thickness
        int cloudOctaves = 6;                 // Noise octaves for cloud detail
        float cloudLacunarity = 2.3f;         // Fractal lacunarity
        float cloudGain = 0.5f;               // Fractal gain

        // Stars and night sky
        float starIntensity = 1.0f;           // Star brightness multiplier
        float moonPhase = 0.5f;               // 0-1 (new moon to full moon)
        float milkyWayIntensity = 1.0f;       // Milky way visibility

        // Post-processing
        float exposure = 1.0f;                // HDR exposure
        float bloomThreshold = 1.0f;          // Bloom threshold
        float bloomIntensity = 0.5f;          // Bloom strength

        // Fog/haze
        float fogDensity = 0.001f;            // Exponential fog density
        float fogHeightFalloff = 0.01f;       // Height-based fog falloff
        glm::vec3 fogColor = glm::vec3(0.7f, 0.8f, 0.9f);

        // Ambient
        float ambientLightMultiplier = 1.0f;  // Global ambient multiplier
        glm::vec3 groundColor = glm::vec3(0.1f, 0.15f, 0.1f);
    };

    // Cloud layer configuration
    struct CloudLayer {
        float altitude;
        float thickness;
        float coverage;
        float speed;
        float scale;
        int type; // 0: Cumulus, 1: Stratus, 2: Cirrus
		float offset = 0.0f;           // Текущее смещение
		float dynamicDensity = 0.5f;   // Динамическая плотность
    };

    // Performance metrics
    struct SkyRenderStats {
        float atmospherePassMs = 0.0f;
        float cloudPassMs = 0.0f;
        float starPassMs = 0.0f;
        float postProcessMs = 0.0f;
        float totalMs = 0.0f;
        uint32_t cloudRayMarches = 0;
        bool temporalAccumulation = false;
    };

    struct AtmosphereUBO {
		alignas(16) glm::vec3 sunDirection; // 0–12
		alignas(4)  float sunIntensity;     // 12–16

		alignas(16) glm::vec3 rayleighBeta; // 16–28
		alignas(16) glm::vec3 mieBeta;      // 32–44
		alignas(4)  float mieG;             // 44–48

		float turbidity;       // 48–52
		float planetRadius;    // 52–56
		float atmosphereRadius; // 56–60
		float time;            // 60–64

		alignas(16) glm::vec3 cameraPos; // 64–76
		alignas(4)  float exposure;      // 76–80
    };

    struct CloudUBO {
		alignas(16) glm::vec3 coverage;      // vec3 coverage
		float speed;                         // float speed

		alignas(16) glm::vec3 windDirection; // vec3 windDirection
		float scale;                         // float scale

		float density;                       // float density
		float altitude;                      // float altitude
		float thickness;                     // float thickness
		float time;                          // float time

		int octaves;                         // int octaves
		float lacunarity;                    // float lacunarity
		float gain;                          // float gain
        float animationOffset;
    };

    struct StarUBO {
        float intensity;
        float twinkle;
        float milkyWayIntensity;
        float time;
    };

    struct CloudPushConstants {
        glm::mat4 invViewProj;
        glm::vec3 cameraPos;
        float time;
        glm::vec3 sunDirection;
        float coverage;
        glm::vec3 windDirection;
        float cloudScale;
    };

    struct StarsPushConstants {
        glm::mat4 invViewProj;
        float intensity;
        float twinkle;
        float time;
        float nightBlend;
    };

    struct PostProcessPushConstants {
        float exposure;
        float bloomThreshold;
        float bloomIntensity;
        float time;
    };

    struct CloudNoisePushConstants {
        int octaves;
        float lacunarity;
        float gain;
        float scale;
    };

    class SkyRenderer {
    public:
        SkyRenderer(RHI::Vulkan::Device* device,
            Core::CoreContext* context,
            RHI::Vulkan::ShaderManager* shaderManager,
            VkExtent2D currentExtent,
            VkFormat colorFormat,
            VkFormat depthFormat);
        ~SkyRenderer();

        // Main rendering interface
        void Render(VkCommandBuffer cmd,
            VkImageView targetImageView,
            VkImage targetImage,
            VkExtent2D extent,
            const glm::mat4& projection,
            const glm::mat4& viewRotationOnly,
            const glm::vec3& cameraPos);

        // IBL generation for scene lighting
        void GenerateEnvironmentMaps(VkCommandBuffer cmd);
        VkImageView GetIrradianceMap() const;
        VkImageView GetPrefilterMap() const;
        VkImageView GetBRDFLUT() const;

        // Sky configuration
        void SetSkyParams(const SkyParams& params) { m_skyParams = params; UpdateAtmosphere(); }
        const SkyParams& GetSkyParams() const { return m_skyParams; }

        void SetTimeOfDay(float hours);
        float GetTimeOfDay() const { return m_skyParams.timeOfDay; }

        void SetQualityProfile(SkyQualityProfile profile);
        SkyQualityProfile GetQualityProfile() const { return m_qualityProfile; }

        // Animation control
        void SetAnimationSpeed(float speed) { m_animationSpeed = speed; }
        void SetAutoAnimate(bool animate) { m_autoAnimate = animate; }
        bool GetAutoAnimate() const { return m_autoAnimate; }

        // Cloud control
        void AddCloudLayer(const CloudLayer& layer);
        void ClearCloudLayers();
        void SetCloudParameters(float coverage, float speed, float scale);

        // Update and queries
        void Update(float deltaTime);
        glm::vec3 GetSunDirection() const { return m_skyParams.sunDirection; }
        glm::vec3 GetMoonDirection() const { return -m_skyParams.sunDirection; }

        // Performance
        const SkyRenderStats& GetStats() const { return m_stats; }
        void EnableTemporalAccumulation(bool enable) { m_useTemporalAccumulation = enable; }

        float TimeStringToFloat(const std::string& timeStr);

    private:
        // Pipeline creation
        void CreateAtmospherePipeline();
        void CreateCloudPipeline();
        void CreateStarsPipeline();
        void CreateSunTexturePipeline();
        void CreateSunBillboardPipeline();
        void CreatePostProcessPipeline();

        void CreateComputePipelines();
        void CreateAtmosphereLUTPipeline();
        void CreateCloudNoisePipeline();
        void CreateStarGeneratorPipeline();

        // Resource creation
        void CreateUniformBuffers();
        void CreateTextures();
        void GenerateSunTexture(VkCommandBuffer cmd);
        void CreateDescriptorSets();
        void CreateRenderTargets();
        void CreateLUTs();

        void InitCloudeLayers();

        // Rendering passes
        void RenderAtmosphere(VkCommandBuffer cmd);
        void RenderSunBillboard(VkCommandBuffer cmd);
        void RenderClouds(VkCommandBuffer cmd);
        void RenderStars(VkCommandBuffer cmd);
        void RenderMoon(VkCommandBuffer cmd);
        void PostProcess(VkCommandBuffer cmd, VkImageView target, VkImage targetImage);

        void InitializeRenderTargetLayouts();

        // Updates
        void UpdateAtmosphere();
        void UpdateDescriptorSets();
        void UpdateSunMoonPositions();
        void UpdateCloudAnimation(float deltaTime);
        void UpdateStarField();
        void UpdateUniformBuffers();

        // Compute passes
        void GenerateAtmosphereLUT(VkCommandBuffer cmd);
        void GenerateCloudNoise(VkCommandBuffer cmd);
        void GenerateStarTexture(VkCommandBuffer cmd);

        void CreateSamplers();

        template<typename T>
        std::unique_ptr<RHI::Vulkan::Buffer> CreateUniformBuffer(RHI::Vulkan::Device* device) {
            return std::make_unique<RHI::Vulkan::Buffer>(
                device,
                sizeof(T),
                VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT,
                VMA_MEMORY_USAGE_CPU_TO_GPU
            );
        }

        void TransitionImageToLayout(VkCommandBuffer cmd, VkImage image, VkImageLayout newLayout);
        void TransitionAllStorageImagesToGeneral(VkCommandBuffer cmd);

        private:
            VkImageLayout m_skyBufferCurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    private:
        // Device and resources
        RHI::Vulkan::Device* m_device;
        RHI::Vulkan::ShaderManager* m_shaderManager;
        RHI::Vulkan::CommandPoolManager* m_commandPoolManager;
        Core::CoreContext* m_context;
        VkFormat m_colorFormat = VK_FORMAT_UNDEFINED;
        VkFormat m_depthFormat = VK_FORMAT_UNDEFINED;

        // Pipelines
        std::unique_ptr<RHI::Vulkan::ReloadablePipeline> m_atmospherePipeline;
        std::unique_ptr<RHI::Vulkan::ReloadablePipeline> m_sunBillboardPipeline;
        std::unique_ptr<RHI::Vulkan::ReloadablePipeline> m_cloudPipeline;
        std::unique_ptr<RHI::Vulkan::ReloadablePipeline> m_starsPipeline;
        std::unique_ptr<RHI::Vulkan::ReloadablePipeline> m_moonPipeline;
        std::unique_ptr<RHI::Vulkan::ReloadablePipeline> m_postProcessPipeline;

        // Compute pipelines
        std::unique_ptr<RHI::Vulkan::ComputePipeline> m_atmosphereLUTPipeline;
        std::unique_ptr<RHI::Vulkan::ComputePipeline> m_cloudNoisePipeline;
        std::unique_ptr<RHI::Vulkan::ComputePipeline> m_starGeneratorPipeline;
        std::unique_ptr<RHI::Vulkan::ComputePipeline> m_sunTexturePipeline;

        // Uniform buffers
        std::unique_ptr<RHI::Vulkan::Buffer> m_atmosphereUBO;
        std::unique_ptr<RHI::Vulkan::Buffer> m_cloudUBO;
        std::unique_ptr<RHI::Vulkan::Buffer> m_starUBO;
        std::unique_ptr<RHI::Vulkan::Buffer> m_postProcessUBO;

        // Textures and LUTs
        std::unique_ptr<RHI::Vulkan::Image> m_transmittanceLUT;      // Atmosphere transmittance
        std::unique_ptr<RHI::Vulkan::Image> m_multiScatteringLUT;   // Multiple scattering
        std::unique_ptr<RHI::Vulkan::Image> m_skyViewLUT;           // Sky view LUT
        std::unique_ptr<RHI::Vulkan::Image> m_cloudNoiseTexture;    // 3D cloud noise
        std::unique_ptr<RHI::Vulkan::Image> m_cloudDetailNoise;     // High-freq detail
        std::unique_ptr<RHI::Vulkan::Image> m_starTexture;          // Procedural stars
        std::unique_ptr<RHI::Vulkan::Image> m_milkyWayTexture;      // Milky way texture
        std::unique_ptr<RHI::Vulkan::Image> m_moonTexture;          // Moon surface
        std::unique_ptr<RHI::Vulkan::Image> m_sunBillboardTexture;

        // Render targets
        std::unique_ptr<RHI::Vulkan::Image> m_skyBuffer;            // HDR sky buffer
        std::unique_ptr<RHI::Vulkan::Image> m_cloudBuffer;          // Cloud layer
        std::unique_ptr<RHI::Vulkan::Image> m_bloomBuffer;          // Bloom buffer
        std::unique_ptr<RHI::Vulkan::Image> m_historyBuffer;        // Temporal history

        // IBL resources
        std::unique_ptr<RHI::Vulkan::Image> m_environmentMap;
        std::unique_ptr<RHI::Vulkan::Image> m_irradianceMap;
        std::unique_ptr<RHI::Vulkan::Image> m_prefilterMap;
        std::unique_ptr<RHI::Vulkan::Image> m_brdfLUT;

        // Descriptor sets
        VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_atmosphereDescSetLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_cloudDescSetLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_starsDescSetLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_postProcessDescSetLayout = VK_NULL_HANDLE;

        VkDescriptorSetLayout m_atmosphereLUTDescSetLayout = VK_NULL_HANDLE;
        VkDescriptorSet m_atmosphereLUTDescSet = VK_NULL_HANDLE;

        VkDescriptorSetLayout m_cloudNoiseDescSetLayout = VK_NULL_HANDLE;
        VkDescriptorSet m_cloudNoiseDescSet = VK_NULL_HANDLE;

        VkDescriptorSetLayout m_starGeneratorDescSetLayout = VK_NULL_HANDLE;
        VkDescriptorSet m_starGeneratorDescSet = VK_NULL_HANDLE;

        VkDescriptorSet m_atmosphereDescSet = VK_NULL_HANDLE;
        VkDescriptorSet m_cloudDescSet = VK_NULL_HANDLE;
        VkDescriptorSet m_starsDescSet = VK_NULL_HANDLE;
        VkDescriptorSet m_postProcessDescSet = VK_NULL_HANDLE;

		VkDescriptorSetLayout m_sunTextureDescSetLayout = VK_NULL_HANDLE;
		VkDescriptorSet m_sunTextureDescSet = VK_NULL_HANDLE;

		VkDescriptorSetLayout m_sunBillboardDescSetLayout = VK_NULL_HANDLE;
		VkDescriptorSet m_sunBillboardDescSet = VK_NULL_HANDLE;

        // Samplers
        VkSampler m_linearSampler = VK_NULL_HANDLE;
        VkSampler m_nearestSampler = VK_NULL_HANDLE;
        VkSampler m_cloudSampler = VK_NULL_HANDLE;

        std::unique_ptr<RHI::Vulkan::Sampler> m_linearSamplerObj;
        std::unique_ptr<RHI::Vulkan::Sampler> m_nearestSamplerObj;
        std::unique_ptr<RHI::Vulkan::Sampler> m_cloudSamplerObj;

        // State
        SkyParams m_skyParams;
        std::vector<CloudLayer> m_cloudLayers;
        SkyQualityProfile m_qualityProfile = SkyQualityProfile::High;

        RHI::Vulkan::ReloadablePipeline::CreateInfo m_atmospherePipelineCreateInfo;

        // Animation
        float m_animationSpeed = 1.0f;
        float m_cloudAnimationTime = 1.0f;
        bool m_autoAnimate = false;

        bool m_needsCloudGeneration = false;
        bool m_needsLUTGeneration = false;
        bool m_needsStarGeneration = false;

        // Temporal accumulation
        bool m_useTemporalAccumulation = true;
        uint32_t m_frameIndex = 0;
        glm::mat4 m_previousViewProj;

        // Performance
        SkyRenderStats m_stats;

        // Current render state
        VkExtent2D m_currentExtent;
        glm::mat4 m_currentProjection;
        glm::mat4 m_currentView;
        glm::vec3 m_currentCameraPos;
    };
} // namespace Renderer