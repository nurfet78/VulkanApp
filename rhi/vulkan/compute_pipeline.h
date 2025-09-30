// rhi/vulkan/compute_pipeline.h
#pragma once

#include "vulkan_common.h"

namespace RHI::Vulkan {
    class Device;
    class ShaderManager;
    class ShaderProgram;

    struct ComputePipelineCreateInfo {
        std::string shaderProgramName;                 // Имя шейдера (SPV через ShaderManager)
        VkDescriptorSetLayout descriptorSetLayout{};  // Дескрипторный сет (если нужен)
        std::vector<VkPushConstantRange> pushConstants; // Push constants для этого шейдера
    };

    class ComputePipeline {
    public:
        ComputePipeline(Device* device, ShaderManager* shaderManager);
        ~ComputePipeline();

        bool Create(const ComputePipelineCreateInfo& info);
        void Destroy();

        void Bind(VkCommandBuffer cmd) const;
        void Dispatch(VkCommandBuffer cmd, uint32_t groupX, uint32_t groupY, uint32_t groupZ) const;

        VkPipeline GetPipeline() const { return m_pipeline; }
        VkPipelineLayout GetLayout() const { return m_pipelineLayout; }


    private:
        Device* m_device{};
        ShaderManager* m_shaderManager{};
        ShaderProgram* m_shaderProgram{};
        VkPipeline m_pipeline{};
        VkPipelineLayout m_pipelineLayout{};
        ComputePipelineCreateInfo m_createInfo{};
    };
}