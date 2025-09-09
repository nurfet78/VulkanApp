// engine/rhi/vulkan/pipeline.h
#pragma once

#include "vulkan_common.h"


namespace RHI::Vulkan {

class Device;
class ShaderManager;
class ReloadablePipeline;

class TrianglePipeline {
public:
    TrianglePipeline(Device* device, ShaderManager* shaderManager, VkFormat colorFormat);
    ~TrianglePipeline() = default;
    
    void Render(VkCommandBuffer cmd, VkImageView targetImageView, VkExtent2D extent);
    
private:
    Device* m_device;
    std::unique_ptr<ReloadablePipeline> m_pipeline;
};

} // namespace RHI::Vulkan