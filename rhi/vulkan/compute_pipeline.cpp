// rhi/vulkan/compute_pipeline.cpp
#include "compute_pipeline.h"
#include "rhi/vulkan/device.h"
#include "rhi/vulkan/shader_manager.h"
#include "shader_manager.h"

namespace RHI::Vulkan {

    ComputePipeline::ComputePipeline(Device* device, ShaderManager* shaderManager)
        : m_device(device), m_shaderManager(shaderManager) {
    }

    ComputePipeline::~ComputePipeline() {
        Destroy();
    }

    bool ComputePipeline::Create(const ComputePipelineCreateInfo& info) {
        m_createInfo = info;

        // Получаем шейдер из ShaderManager
        m_shaderProgram = m_shaderManager->GetProgram(info.shaderProgramName);
        if (!m_shaderProgram) {
            std::cerr << "Compute shader program not found: " << info.shaderProgramName << std::endl;
            return false;
        }

        // Компилируем шейдер (если еще не скомпилирован)
        if (!m_shaderProgram->Compile()) {
            std::cerr << "Failed to compile compute shader: " << info.shaderProgramName << std::endl;
            return false;
        }

        // Создаем pipeline layout
        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = info.descriptorSetLayout != VK_NULL_HANDLE ? 1 : 0;
        layoutInfo.pSetLayouts = info.descriptorSetLayout != VK_NULL_HANDLE ? &info.descriptorSetLayout : nullptr;

        layoutInfo.pushConstantRangeCount = static_cast<uint32_t>(info.pushConstants.size());
        layoutInfo.pPushConstantRanges = info.pushConstants.empty() ? nullptr : info.pushConstants.data();

        if (vkCreatePipelineLayout(m_device->GetDevice(), &layoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
            std::cerr << "Failed to create compute pipeline layout" << std::endl;
            return false;
        }

        // Create compute pipeline
        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;

        auto stages = m_shaderProgram->GetStageCreateInfos();
        if (stages.empty()) {
            std::cerr << "No valid stages in compute shader program: " << info.shaderProgramName << std::endl;
            return false;
        }

        // У нас должен быть только один stage — compute
        pipelineInfo.stage = stages[0];
        pipelineInfo.layout = m_pipelineLayout;

        if (vkCreateComputePipelines(m_device->GetDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline) != VK_SUCCESS) {
            std::cerr << "Failed to create compute pipeline: " << info.shaderProgramName << std::endl;
            return false;
        }

        return true;
    }

    void ComputePipeline::Destroy() {
        if (m_pipeline) {
            vkDestroyPipeline(m_device->GetDevice(), m_pipeline, nullptr);
            m_pipeline = VK_NULL_HANDLE;
        }
        if (m_pipelineLayout) {
            vkDestroyPipelineLayout(m_device->GetDevice(), m_pipelineLayout, nullptr);
            m_pipelineLayout = VK_NULL_HANDLE;
        }
    }

    void ComputePipeline::Dispatch(VkCommandBuffer cmd, uint32_t groupX, uint32_t groupY, uint32_t groupZ) const {
        Bind(cmd);
        vkCmdDispatch(cmd, groupX, groupY, groupZ);
    }

    void ComputePipeline::Bind(VkCommandBuffer cmd) const {
        if (m_pipeline) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
        }
    }
}