
#pragma once

#include "rhi/vulkan/vulkan_common.h"


namespace RHI::Vulkan {
	class Device;
	class ShaderManager;
	class ReloadablePipeline;
} 

namespace Renderer {
    class TriangleRenderer {
    public:
        TriangleRenderer(RHI::Vulkan::Device* device, RHI::Vulkan::ShaderManager* shaderManager, VkFormat colorFormat);
        ~TriangleRenderer() = default;

        void Render(VkCommandBuffer cmd, VkImageView targetImageView, VkExtent2D extent);

    private:
        RHI::Vulkan::Device* m_device;
        std::unique_ptr<RHI::Vulkan::ReloadablePipeline> m_pipeline;
    };
}