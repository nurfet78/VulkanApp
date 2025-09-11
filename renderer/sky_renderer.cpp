// engine/renderer/sky_renderer.cpp
#include "sky_renderer.h"
#include "rhi/vulkan/device.h"
#include "rhi/vulkan/shader_manager.h"
#include "rhi/vulkan/resource.h"
#include "rhi/vulkan/descriptor_allocator.h"

namespace Renderer {

    SkyRenderer::SkyRenderer(RHI::Vulkan::Device* device,
        RHI::Vulkan::ShaderManager* shaderManager,
        VkFormat colorFormat,
        VkFormat depthFormat)
        : m_device(device)
        , m_shaderManager(shaderManager)
    {
        CreateUniformBuffer();      // ������ uniform buffer
        CreateDescriptorSet();      // ������ descriptor set
        CreatePipeline(colorFormat, depthFormat);
        CreateIBLResources();
        UpdateSunPosition();
        UpdateUniformBuffer();      // ��������� uniform buffer ���������� �������
    }

    SkyRenderer::~SkyRenderer() {
        // 1. ����� pipeline
        m_pipeline.reset(); // ����� ���������, ������� Destroy() ReloadablePipeline

        // 2. ����� uniform buffer
        m_uniformBuffer.reset();

        // 3. ������� ��������-���� � layout
        // Descriptor set ������������� �����, ��� ���������� �� �����
        if (m_descriptorSetLayout) {
            vkDestroyDescriptorSetLayout(m_device->GetDevice(), m_descriptorSetLayout, nullptr);
            m_descriptorSetLayout = VK_NULL_HANDLE;
        }
    }

    void SkyRenderer::CreateUniformBuffer() {
        // ������ uniform buffer ��� �������� �������� ����������
        VkDeviceSize bufferSize = sizeof(SkyUniformData);

        m_uniformBuffer = std::make_unique<RHI::Vulkan::Buffer>(
            m_device,
            bufferSize,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU
        );
    }

    void SkyRenderer::CreateDescriptorSet() {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m_uniformBuffer->GetHandle();
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(SkyUniformData);

        // ���������� DescriptorBuilder
        bool ok = RHI::Vulkan::DescriptorBuilder::Begin(
            m_device->GetDescriptorLayoutCache(),
            m_device->GetDescriptorAllocator())
            .BindBuffer(0, &bufferInfo,
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                VK_SHADER_STAGE_FRAGMENT_BIT)
            .Build(m_descriptorSet, m_descriptorSetLayout);

        if (!ok) {
            throw std::runtime_error("SkyRenderer: failed to build descriptor set!");
        }
    }

    void SkyRenderer::CreatePipeline(VkFormat colorFormat, VkFormat depthFormat) {
        RHI::Vulkan::ReloadablePipeline::CreateInfo pipelineInfo{};

        pipelineInfo.shaderProgram = "Sky";

        // Push constants - ������ �������� �������!
        pipelineInfo.pushConstantSize = sizeof(SkyPushConstants);
        pipelineInfo.pushConstantStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        pipelineInfo.descriptorLayouts = &m_descriptorSetLayout;  // ��������� �� ���� layout
        pipelineInfo.descriptorLayoutCount = 1;                   // ����������� ����������


        pipelineInfo.colorFormat = colorFormat;
        pipelineInfo.depthFormat = depthFormat;

        pipelineInfo.depthTestEnable = true;
        pipelineInfo.depthWriteEnable = false;
        pipelineInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        pipelineInfo.cullMode = VK_CULL_MODE_NONE;

        pipelineInfo.vertexBindings = {};
        pipelineInfo.vertexAttributes = {};

        m_pipeline = std::make_unique<RHI::Vulkan::ReloadablePipeline>(m_device, m_shaderManager);
        if (!m_pipeline->Create(pipelineInfo)) {
            throw std::runtime_error("Failed to create sky pipeline!");
        }
    }

    void SkyRenderer::UpdateUniformBuffer() {

        SkyUniformData ubo{};
        ubo.dayHorizonColor = glm::vec4(m_skyParams.dayHorizonColor, 1.0f);
        ubo.dayZenithColor = glm::vec4(m_skyParams.dayZenithColor, 1.0f);
        ubo.sunsetHorizonColor = glm::vec4(m_skyParams.sunsetHorizonColor, 1.0f);
        ubo.sunsetZenithColor = glm::vec4(m_skyParams.sunsetZenithColor, 1.0f);
        ubo.nightHorizonColor = glm::vec4(m_skyParams.nightHorizonColor, 1.0f);
        ubo.nightZenithColor = glm::vec4(m_skyParams.nightZenithColor, 1.0f);
        ubo.groundColor = glm::vec4(m_skyParams.groundColor, 1.0f);
        ubo.sunColor = glm::vec4(m_skyParams.sunColor, 1.0f);

        m_uniformBuffer->Upload(&ubo, sizeof(ubo));
    }

    void SkyRenderer::CreateIBLResources() {
        // Environment map (cube map) ��� ���������
        m_environmentMap = std::make_unique<RHI::Vulkan::Image>(
            m_device, 512, 512, VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT
        );

        // Irradiance map ��� ���������� IBL
        m_irradianceMap = std::make_unique<RHI::Vulkan::Image>(
            m_device, 32, 32, VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT
        );

        // Prefilter map ��� ����������� IBL
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

    void SkyRenderer::UpdateSunPosition() {
        // ��������� ��������� ������ �� ������ ������� ���
        float sunAngle = (m_timeOfDay - 6.0f) * glm::pi<float>() / 12.0f;
        m_skyParams.sunDirection = glm::normalize(glm::vec3(
            std::cos(sunAngle),
            std::sin(sunAngle),
            -0.3f
        ));
    }

    void SkyRenderer::Update(float deltaTime) {
        // ��������� ����� �������� �������
        m_cloudAnimationTime += deltaTime;

        // �������������� �������� ������� ���
        if (m_autoAnimate) {
            m_timeOfDay += deltaTime * m_animationSpeed;
            if (m_timeOfDay >= 24.0f) {
                m_timeOfDay -= 24.0f;
            }
            UpdateSunPosition();
        }
    }

    void SkyRenderer::Render(VkCommandBuffer cmd,
        VkImageView targetImageView,
        VkImageView depthImageView,
        VkExtent2D extent,
        const glm::mat4& projection,
        const glm::mat4& viewRotationOnly,
        const glm::vec3& cameraPos) {
        // ��������� ����������
        VkRenderingAttachmentInfo colorAttachment{};
        colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachment.imageView = targetImageView;
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        VkRenderingAttachmentInfo depthAttachment{};
        depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAttachment.imageView = depthImageView;
        depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        VkRenderingInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderingInfo.renderArea.extent = extent;
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &colorAttachment;
        renderingInfo.pDepthAttachment = &depthAttachment;

        vkCmdBeginRendering(cmd, &renderingInfo);

        // Viewport � scissor
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

        // ��������� ����������� push constants
        SkyPushConstants pc{};
        pc.invProjection = glm::inverse(projection);
        pc.invView = glm::inverse(viewRotationOnly);
        pc.cameraPosAndTime = glm::vec4(cameraPos, m_cloudAnimationTime);
        pc.sunDirAndIntensity = glm::vec4(m_skyParams.sunDirection, m_skyParams.sunIntensity);
        pc.skyParams1 = glm::vec4(m_timeOfDay, m_skyParams.cloudCoverage, m_skyParams.cloudSpeed, m_skyParams.cloudScale);
        pc.skyParams2 = glm::vec4(m_skyParams.atmosphereDensity, m_skyParams.sunSize, m_skyParams.starIntensity, m_skyParams.horizonSharpness);
        pc.resolution = glm::vec2(extent.width, extent.height);

        vkCmdPushConstants(cmd, m_pipeline->GetLayout(),
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(SkyPushConstants), &pc);

        // ����������� descriptor set � uniform buffer
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_pipeline->GetLayout(), 0, 1, &m_descriptorSet, 0, nullptr);

        // ������
        if (m_pipeline) {
            m_pipeline->Bind(cmd);
            vkCmdDraw(cmd, 3, 1, 0, 0);
        }

        vkCmdEndRendering(cmd);
    }

    void SkyRenderer::ConfigureSky() {
        SkyParams skyParams;

        // ��������� ������� ��� (��������, �������)
        SetTimeOfDay(12.0f);

        // ��� �������� �������������� �������� �������
        // m_skyRenderer->SetAutoAnimate(true);
        // m_skyRenderer->SetAnimationSpeed(0.5f); // 0.5 ���� � �������

        // ��������� ���������� ����
        skyParams.sunIntensity = 3.0f;
        skyParams.sunSize = 64.0f;

        // ����� ���
        skyParams.dayHorizonColor = glm::vec3(0.6f, 0.75f, 0.9f);
        skyParams.dayZenithColor = glm::vec3(0.1f, 0.3f, 0.7f);

        // ����� ������
        skyParams.sunsetHorizonColor = glm::vec3(0.9f, 0.5f, 0.3f);
        skyParams.sunsetZenithColor = glm::vec3(0.3f, 0.2f, 0.5f);

        // ����� ����
        skyParams.nightHorizonColor = glm::vec3(0.02f, 0.02f, 0.05f);
        skyParams.nightZenithColor = glm::vec3(0.0f, 0.0f, 0.02f);

        // ������
        skyParams.cloudCoverage = 0.4f;     // 40% �������� ��������
        skyParams.cloudSpeed = 0.02f;       // �������� ��������
        skyParams.cloudScale = 3.0f;        // �������
        skyParams.cloudHeight = 0.3f;       // ������ ���� �������

        // ���������
        skyParams.atmosphereDensity = 1.5f; // ��������� ���������
        skyParams.horizonSharpness = 3.0f;  // �������� ���������
        skyParams.starIntensity = 0.5f;     // ������� ����

        SetSkyParams(skyParams);
    }

    void SkyRenderer::GenerateEnvironmentMaps(VkCommandBuffer cmd) {
        // TODO: ���������� ��������� IBL ����
        // 1. �������� ���� � ������
        // 2. ���������� irradiance map (������ ��� ���������� ���������)
        // 3. ���������� prefilter map (������ ��� ����������� ���������)
        // 4. ���������� BRDF LUT
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

    glm::vec3 SkyRenderer::GetCurrentSkyColor(const glm::vec3& direction) const {
        return CalculateSkyColor(direction);
    }

    glm::vec3 SkyRenderer::GetAmbientLight() const {
        // ���������� ���������� ���� ���� ��� ����������� ���������
        glm::vec3 upColor = CalculateSkyColor(glm::vec3(0, 1, 0));
        glm::vec3 horizonColor = CalculateSkyColor(glm::vec3(1, 0, 0));
        return glm::mix(horizonColor, upColor, 0.5f) * 0.3f;
    }

    glm::vec3 SkyRenderer::CalculateSkyColor(const glm::vec3& rayDir) const {
        float y = rayDir.y * 0.5f + 0.5f;

        // ������������ ������ � ����������� �� �������
        float dayFactor = glm::smoothstep(6.0f, 9.0f, m_timeOfDay) *
            glm::smoothstep(21.0f, 18.0f, m_timeOfDay);
        float sunsetFactor = glm::max(
            glm::smoothstep(5.0f, 7.0f, m_timeOfDay) * glm::smoothstep(9.0f, 7.0f, m_timeOfDay),
            glm::smoothstep(17.0f, 19.0f, m_timeOfDay) * glm::smoothstep(21.0f, 19.0f, m_timeOfDay)
        );
        float nightFactor = 1.0f - glm::max(dayFactor, sunsetFactor);

        glm::vec3 horizonColor = m_skyParams.dayHorizonColor * dayFactor +
            m_skyParams.sunsetHorizonColor * sunsetFactor +
            m_skyParams.nightHorizonColor * nightFactor;
        glm::vec3 zenithColor = m_skyParams.dayZenithColor * dayFactor +
            m_skyParams.sunsetZenithColor * sunsetFactor +
            m_skyParams.nightZenithColor * nightFactor;

        glm::vec3 skyColor;
        if (rayDir.y > 0.0f) {
            skyColor = glm::mix(horizonColor, zenithColor, glm::pow(y, m_skyParams.atmosphereDensity));
        }
        else {
            skyColor = glm::mix(horizonColor, m_skyParams.groundColor, glm::pow(-rayDir.y, 2.0f));
        }

        // ������
        float sun = glm::pow(glm::max(glm::dot(rayDir, m_skyParams.sunDirection), 0.0f), m_skyParams.sunSize);
        skyColor += m_skyParams.sunColor * sun * m_skyParams.sunIntensity * glm::max(dayFactor, sunsetFactor * 0.7f);

        return skyColor;
    }

    void SkyRenderer::RecreateSwapchainResources() {
        // 1. ���� ��������� ������ GPU
        vkDeviceWaitIdle(m_device->GetDevice());

        // 2. ���������� allocator (����������� ������ descriptor sets)
        m_device->GetDescriptorAllocator()->ResetPools(); 

        // 3. ������� descriptor set ������
        CreateDescriptorSet();
    }


} // namespace Renderer