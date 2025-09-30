// engine/rhi/vulkan/shader_manager.h
#pragma once

#include "vulkan_common.h"

namespace RHI::Vulkan {

    class Device;

    // Shader stage info
    struct ShaderStageInfo {
        VkShaderStageFlagBits stage;
        std::string path;
        std::string entryPoint = "main";
        std::vector<uint32_t> spirv;
        VkShaderModule module = VK_NULL_HANDLE;
    };

    // Shader program (collection of stages)
    class ShaderProgram {
    public:
        ShaderProgram(Device* device, const std::string& name);
        ~ShaderProgram();

        // Add shader stages
        void AddStage(VkShaderStageFlagBits stage, const std::string& path,
            const std::string& entryPoint = "main");

        // Compile all stages
        bool Compile();

        // Get compiled modules
        const std::vector<ShaderStageInfo>& GetStages() const { return m_stages; }
        std::vector<VkPipelineShaderStageCreateInfo> GetStageCreateInfos() const;

        const std::string& GetName() const { return m_name; }
        bool IsValid() const { return m_valid; }

    private:
        bool CompileStage(ShaderStageInfo& stage);

        Device* m_device;
        std::string m_name;
        std::vector<ShaderStageInfo> m_stages;
        bool m_valid = false;
    };

    // Shader manager with hot-reload support
    class ShaderManager {
    public:
        ShaderManager(Device* device);
        ~ShaderManager();

        // Shader management
        ShaderProgram* CreateProgram(const std::string& name);
        ShaderProgram* GetProgram(const std::string& name);
        void RemoveProgram(const std::string& name);

        VkShaderModule LoadShader(const std::string& path, VkShaderStageFlagBits stage);

    private:
        Device* m_device;
        std::unordered_map<std::string, std::unique_ptr<ShaderProgram>> m_programs;
    };

    // Pipeline with automatic shader reload
    class ReloadablePipeline {
    public:
        ReloadablePipeline(Device* device, ShaderManager* shaderManager);
        ~ReloadablePipeline();

        struct CreateInfo {
            std::string shaderProgram;

            // Vertex input
            std::vector<VkVertexInputBindingDescription> vertexBindings;
            std::vector<VkVertexInputAttributeDescription> vertexAttributes;

            // Pipeline state
            VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;
            VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
            VkFrontFace frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

            // Depth state
            bool depthTestEnable = true;
            bool depthWriteEnable = true;
            VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS;

            // Blend state
            bool blendEnable = false;

            VkBlendFactor srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            VkBlendFactor dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            VkBlendOp colorBlendOp = VK_BLEND_OP_ADD;

            VkBlendFactor srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            VkBlendFactor dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            VkBlendOp alphaBlendOp = VK_BLEND_OP_ADD;

            // Render state
            VkFormat colorFormat = VK_FORMAT_B8G8R8A8_SRGB;
            VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;

            // Layout
            VkDescriptorSetLayout* descriptorLayouts = nullptr;
            uint32_t descriptorLayoutCount = 0;
            VkPipelineLayout externalLayout = VK_NULL_HANDLE;

            // Push constants
            std::vector<VkPushConstantRange> pushConstants;
        };

        bool Create(const CreateInfo& info);
        void Destroy();

        // Bind pipeline
        void Bind(VkCommandBuffer cmd);

        // Get handles
        VkPipeline GetPipeline() const { return m_pipeline; }
        VkPipelineLayout GetLayout() const { return m_pipelineLayout; }

        static CreateInfo MakePipelineInfo(
            const std::string& shaderName,
            VkFormat colorFormat,
            VkDescriptorSetLayout* layouts,
            uint32_t layoutCount,
            bool blendEnable,
            VkBlendFactor srcColorBlend = VK_BLEND_FACTOR_SRC_ALPHA,
            VkBlendFactor dstColorBlend = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            VkBlendOp colorOp = VK_BLEND_OP_ADD,
            VkBlendFactor srcAlphaBlend = VK_BLEND_FACTOR_ONE,
            VkBlendFactor dstAlphaBlend = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            VkBlendOp alphaOp = VK_BLEND_OP_ADD
        );

        static CreateInfo Make3DPipelineInfo(
            const std::string& shaderName,
            VkFormat colorFormat,
            VkFormat depthFormat,
            VkDescriptorSetLayout* layouts,
            uint32_t layoutCount,
            const std::vector<VkVertexInputBindingDescription>& vertexBindings,
            const std::vector<VkVertexInputAttributeDescription>& vertexAttributes,
            const std::vector<VkPushConstantRange>& pushConstants = {},
            VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT,
            bool blendEnable = false
        );

    private:
        bool CreatePipeline();

        Device* m_device;
        ShaderManager* m_shaderManager;

        CreateInfo m_createInfo;
        ShaderProgram* m_shaderProgram = nullptr;

        VkPipeline m_pipeline = VK_NULL_HANDLE;
        VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
        bool m_ownsLayout = true;
    };
}