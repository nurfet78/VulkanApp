// engine/rhi/vulkan/pipeline.cpp
#include "pipeline.h"
#include "device.h"

namespace RHI::Vulkan {

Pipeline::Pipeline(Device* device, const PipelineConfig& config, VkPipelineLayout layout)
    : m_device(device), m_layout(layout) {
    
    // Create shader modules
    VkShaderModule vertShader = CreateShaderModule(config.vertexShaderCode);
    VkShaderModule fragShader = CreateShaderModule(config.fragmentShaderCode);
    
    m_shaderModules.push_back(vertShader);
    m_shaderModules.push_back(fragShader);
    
    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShader;
    vertShaderStageInfo.pName = "main";
    
    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShader;
    fragShaderStageInfo.pName = "main";
    
    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};
    
    // Vertex input
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(config.vertexBindings.size());
    vertexInputInfo.pVertexBindingDescriptions = config.vertexBindings.data();
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(config.vertexAttributes.size());
    vertexInputInfo.pVertexAttributeDescriptions = config.vertexAttributes.data();
    
    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;
    
    // Viewport state
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    
    // Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = config.polygonMode;
    rasterizer.lineWidth = config.lineWidth;
    rasterizer.cullMode = config.cullMode;
    rasterizer.frontFace = config.frontFace;
    rasterizer.depthBiasEnable = VK_FALSE;
    
    // Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    
    // Depth stencil
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = config.depthTestEnable;
    depthStencil.depthWriteEnable = config.depthWriteEnable;
    depthStencil.depthCompareOp = config.depthCompareOp;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;
    
    // Color blending
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = config.blendEnable;
    colorBlendAttachment.srcColorBlendFactor = config.srcColorBlendFactor;
    colorBlendAttachment.dstColorBlendFactor = config.dstColorBlendFactor;
    colorBlendAttachment.colorBlendOp = config.colorBlendOp;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    
    // Dynamic state
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();
    
    // Dynamic rendering info (Vulkan 1.3)
    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &config.colorFormat;
    renderingInfo.depthAttachmentFormat = config.depthFormat;
    
    // Create pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &renderingInfo;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = config.depthTestEnable ? &depthStencil : nullptr;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_layout;
    pipelineInfo.renderPass = VK_NULL_HANDLE; // Dynamic rendering
    pipelineInfo.subpass = 0;
    
    VK_CHECK(vkCreateGraphicsPipelines(m_device->GetDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline));
}

Pipeline::~Pipeline() {
    VkDevice device = m_device->GetDevice();
    
    for (auto module : m_shaderModules) {
        vkDestroyShaderModule(device, module, nullptr);
    }
    
    if (m_pipeline) {
        vkDestroyPipeline(device, m_pipeline, nullptr);
    }
}

void Pipeline::Bind(VkCommandBuffer cmd) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
}

VkShaderModule Pipeline::CreateShaderModule(const std::vector<uint32_t>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size() * sizeof(uint32_t);
    createInfo.pCode = code.data();
    
    VkShaderModule shaderModule;
    VK_CHECK(vkCreateShaderModule(m_device->GetDevice(), &createInfo, nullptr, &shaderModule));
    
    return shaderModule;
}

// Simple triangle pipeline implementation
TrianglePipeline::TrianglePipeline(Device* device, VkFormat colorFormat) : m_device(device) {
    // Hardcoded triangle vertex shader SPIR-V
    std::vector<uint32_t> vertShaderCode = {
        0x07230203,0x00010000,0x000d000a,0x00000024,
        0x00000000,0x00020011,0x00000001,0x0006000b,
        0x00000001,0x4c534c47,0x6474732e,0x3035342e,
        0x00000000,0x0003000e,0x00000000,0x00000001,
        0x0008000f,0x00000000,0x00000004,0x6e69616d,
        0x00000000,0x0000000d,0x00000016,0x00000020,
        0x00030003,0x00000002,0x000001c2,0x000a0004,
        0x475f4c47,0x4c474f4f,0x70635f45,0x74735f70,
        0x5f656c79,0x656e696c,0x7269645f,0x69746365,
        0x00006576,0x00080004,0x475f4c47,0x4c474f4f,
        0x6e695f45,0x64756c63,0x69645f65,0x74636572,
        0x00657669,0x00040005,0x00000004,0x6e69616d,
        0x00000000,0x00060005,0x0000000b,0x505f6c67,
        0x65567265,0x78657472,0x00000000,0x00060006,
        0x0000000b,0x00000000,0x505f6c67,0x7469736f,
        0x006e6f69,0x00070006,0x0000000b,0x00000001,
        0x505f6c67,0x746e696f,0x657a6953,0x00000000,
        0x00070006,0x0000000b,0x00000002,0x435f6c67,
        0x4470696c,0x61747369,0x0065636e,0x00070006,
        0x0000000b,0x00000003,0x435f6c67,0x446c6c75,
        0x61747369,0x0065636e,0x00030005,0x0000000d,
        0x00000000,0x00050005,0x00000016,0x6f6c6f63,
        0x00000072,0x00060005,0x00000020,0x565f6c67,
        0x65747265,0x646e4978,0x00007865,0x00050048,
        0x0000000b,0x00000000,0x0000000b,0x00000000,
        0x00050048,0x0000000b,0x00000001,0x0000000b,
        0x00000001,0x00050048,0x0000000b,0x00000002,
        0x0000000b,0x00000003,0x00050048,0x0000000b,
        0x00000003,0x0000000b,0x00000004,0x00030047,
        0x0000000b,0x00000002,0x00040047,0x00000016,
        0x0000001e,0x00000000,0x00040047,0x00000020,
        0x0000000b,0x0000002a,0x00020013,0x00000002,
        0x00030021,0x00000003,0x00000002,0x00030016,
        0x00000006,0x00000020,0x00040017,0x00000007,
        0x00000006,0x00000004,0x00040017,0x00000008,
        0x00000006,0x00000002,0x0004001c,0x00000009,
        0x00000006,0x00000001,0x0006001e,0x0000000b,
        0x00000007,0x00000006,0x00000009,0x00000009,
        0x00040020,0x0000000c,0x00000003,0x0000000b,
        0x0004003b,0x0000000c,0x0000000d,0x00000003,
        0x00040015,0x0000000e,0x00000020,0x00000001,
        0x0004002b,0x0000000e,0x0000000f,0x00000000,
        0x00040017,0x00000010,0x00000006,0x00000003,
        0x00040020,0x00000015,0x00000003,0x00000010,
        0x0004003b,0x00000015,0x00000016,0x00000003,
        0x00040015,0x0000001f,0x00000020,0x00000000,
        0x00040020,0x00000021,0x00000001,0x0000001f,
        0x0004003b,0x00000021,0x00000020,0x00000001,
        0x00040020,0x00000022,0x00000003,0x00000007,
        0x00050036,0x00000002,0x00000004,0x00000000,
        0x00000003,0x000200f8,0x00000005,0x0004003d,
        0x0000001f,0x00000023,0x00000020,0x00040070,
        0x00000006,0x00000024,0x00000023,0x00050041,
        0x00000022,0x00000025,0x0000000d,0x0000000f,
        0x0003003e,0x00000025,0x00000024,0x000100fd,
        0x00010038
    };
    
    // Hardcoded fragment shader SPIR-V
    std::vector<uint32_t> fragShaderCode = {
        0x07230203,0x00010000,0x000d000a,0x00000013,
        0x00000000,0x00020011,0x00000001,0x0006000b,
        0x00000001,0x4c534c47,0x6474732e,0x3035342e,
        0x00000000,0x0003000e,0x00000000,0x00000001,
        0x0007000f,0x00000004,0x00000004,0x6e69616d,
        0x00000000,0x00000009,0x0000000c,0x00030010,
        0x00000004,0x00000007,0x00030003,0x00000002,
        0x000001c2,0x000a0004,0x475f4c47,0x4c474f4f,
        0x70635f45,0x74735f70,0x5f656c79,0x656e696c,
        0x7269645f,0x69746365,0x00006576,0x00080004,
        0x475f4c47,0x4c474f4f,0x6e695f45,0x64756c63,
        0x69645f65,0x74636572,0x00657669,0x00040005,
        0x00000004,0x6e69616d,0x00000000,0x00050005,
        0x00000009,0x4674754f,0x43676172,0x00726f6c,
        0x00050005,0x0000000c,0x6f6c6f63,0x00000072,
        0x00040047,0x00000009,0x0000001e,0x00000000,
        0x00040047,0x0000000c,0x0000001e,0x00000000,
        0x00020013,0x00000002,0x00030021,0x00000003,
        0x00000002,0x00030016,0x00000006,0x00000020,
        0x00040017,0x00000007,0x00000006,0x00000004,
        0x00040020,0x00000008,0x00000003,0x00000007,
        0x0004003b,0x00000008,0x00000009,0x00000003,
        0x00040017,0x0000000a,0x00000006,0x00000003,
        0x00040020,0x0000000b,0x00000001,0x0000000a,
        0x0004003b,0x0000000b,0x0000000c,0x00000001,
        0x0004002b,0x00000006,0x0000000e,0x3f800000,
        0x00050036,0x00000002,0x00000004,0x00000000,
        0x00000003,0x000200f8,0x00000005,0x0004003d,
        0x0000000a,0x0000000d,0x0000000c,0x00050051,
        0x00000006,0x0000000f,0x0000000d,0x00000000,
        0x00050051,0x00000006,0x00000010,0x0000000d,
        0x00000001,0x00050051,0x00000006,0x00000011,
        0x0000000d,0x00000002,0x00070050,0x00000007,
        0x00000012,0x0000000f,0x00000010,0x00000011,
        0x0000000e,0x0003003e,0x00000009,0x00000012,
        0x000100fd,0x00010038
    };
    
    // Create pipeline layout
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    
    VK_CHECK(vkCreatePipelineLayout(m_device->GetDevice(), &layoutInfo, nullptr, &m_pipelineLayout));
    
    // Create pipeline
    PipelineConfig config;
    config.vertexShaderCode = vertShaderCode;
    config.fragmentShaderCode = fragShaderCode;
    config.colorFormat = colorFormat;
    config.depthTestEnable = false;
    config.cullMode = VK_CULL_MODE_NONE;
    
    m_pipeline = std::make_unique<Pipeline>(m_device, config, m_pipelineLayout);
}

TrianglePipeline::~TrianglePipeline() {
    if (m_pipelineLayout) {
        vkDestroyPipelineLayout(m_device->GetDevice(), m_pipelineLayout, nullptr);
    }
}

void TrianglePipeline::Render(VkCommandBuffer cmd, VkImageView targetImageView, VkExtent2D extent) {
    // Begin dynamic rendering
    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = targetImageView;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = {{0.1f, 0.1f, 0.2f, 1.0f}};
    
    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = {0, 0};
    renderingInfo.renderArea.extent = extent;
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    
    vkCmdBeginRendering(cmd, &renderingInfo);
    
    // Set viewport and scissor
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    
    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    
    // Bind pipeline and draw
    m_pipeline->Bind(cmd);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    
    vkCmdEndRendering(cmd);
}

} // namespace RHI::Vulkan