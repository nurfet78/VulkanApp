// engine/rhi/vulkan/pipeline.h
#pragma once

#include "vulkan_common.h"


namespace RHI::Vulkan {

class Device;

struct PipelineConfig {
    // Shaders
    std::vector<uint32_t> vertexShaderCode;
    std::vector<uint32_t> fragmentShaderCode;
    
    // Vertex input
    std::vector<VkVertexInputBindingDescription> vertexBindings;
    std::vector<VkVertexInputAttributeDescription> vertexAttributes;
    
    // Rasterization
    VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;
    VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
    VkFrontFace frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    float lineWidth = 1.0f;
    
    // Depth stencil
    bool depthTestEnable = true;
    bool depthWriteEnable = true;
    VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS;
    
    // Blending
    bool blendEnable = false;
    VkBlendFactor srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    VkBlendFactor dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    VkBlendOp colorBlendOp = VK_BLEND_OP_ADD;
    
    // Dynamic rendering
    VkFormat colorFormat = VK_FORMAT_B8G8R8A8_SRGB;
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
    
    // Push constants
    uint32_t pushConstantSize = 0;
    VkShaderStageFlags pushConstantStages = VK_SHADER_STAGE_VERTEX_BIT;
};

class Pipeline {
public:
    Pipeline(Device* device, const PipelineConfig& config, VkPipelineLayout layout);
    ~Pipeline();
    
    void Bind(VkCommandBuffer cmd);
    
    VkPipeline GetHandle() const { return m_pipeline; }
    VkPipelineLayout GetLayout() const { return m_layout; }

private:
    VkShaderModule CreateShaderModule(const std::vector<uint32_t>& code);
    
    Device* m_device;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    
    std::vector<VkShaderModule> m_shaderModules;
};

// Simple triangle pipeline for testing
class TrianglePipeline {
public:
    TrianglePipeline(Device* device, VkFormat colorFormat);
    ~TrianglePipeline();
    
    void Render(VkCommandBuffer cmd, VkImageView targetImageView, VkExtent2D extent);
    
private:
    Device* m_device;
    std::unique_ptr<Pipeline> m_pipeline;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
};

} // namespace RHI::Vulkan