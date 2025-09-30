#include "triangle_renderer.h"
#include "rhi/vulkan/device.h"
#include "rhi/vulkan/shader_manager.h"

namespace Renderer {


    TriangleRenderer::TriangleRenderer(RHI::Vulkan::Device* device, RHI::Vulkan::ShaderManager* shaderManager, VkFormat colorFormat) 
        : m_device(device) {

    // 1. Описываем конфигурацию нашего пайплайна
        RHI::Vulkan::ReloadablePipeline::CreateInfo pipelineInfo{};

    // Указываем имя шейдерной программы, которую мы загрузили в MeadowApp::LoadShaders()
    pipelineInfo.shaderProgram = "Triangle";

    // Настройки пайплайна (взяты из старого кода)
    pipelineInfo.colorFormat = colorFormat;
    pipelineInfo.depthFormat = VK_FORMAT_UNDEFINED;
    pipelineInfo.depthTestEnable = false;
    pipelineInfo.depthWriteEnable = false;
    pipelineInfo.cullMode = VK_CULL_MODE_NONE;
    pipelineInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // Поскольку у нас нет вершинных буферов, эти массивы пустые
    pipelineInfo.vertexBindings = {};
    pipelineInfo.vertexAttributes = {};

    // 2. Создаем ReloadablePipeline
    m_pipeline = std::make_unique<RHI::Vulkan::ReloadablePipeline>(m_device, shaderManager);
    if (!m_pipeline->Create(pipelineInfo)) {
        throw std::runtime_error("Failed to create triangle pipeline!");
    }
}


void TriangleRenderer::Render(VkCommandBuffer cmd, VkImageView targetImageView, VkExtent2D extent) {
    // Begin dynamic rendering
    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = targetImageView;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
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
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->GetPipeline()); // Получаем VkPipeline
    vkCmdDraw(cmd, 3, 1, 0, 0);

    vkCmdEndRendering(cmd);
}

} // namespace RHI::Vulkan