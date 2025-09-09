
#pragma once

#include "rhi/vulkan/vulkan_common.h"


namespace RHI::Vulkan {

class Device;
class ShaderManager;
class ReloadablePipeline;

class TriangleRenderer {
public:
    TriangleRenderer(Device* device, ShaderManager* shaderManager, VkFormat colorFormat);
    ~TriangleRenderer() = default;
    
    void Render(VkCommandBuffer cmd, VkImageView targetImageView, VkExtent2D extent);
    
private:
    Device* m_device;
    std::unique_ptr<ReloadablePipeline> m_pipeline;
};

} // namespace RHI::Vulkan