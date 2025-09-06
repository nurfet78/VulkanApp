// engine/renderer/scene_renderer.h
#pragma once

#include <memory>
#include <vector>

namespace Scene { class World; }
namespace RHI::Vulkan { class Pipeline; }

namespace Renderer {

class RenderContext;
class FrameManager;
class ShadowMapper;
class SkyRenderer;

// Handles scene rendering
class SceneRenderer {
public:
    SceneRenderer(RenderContext* context);
    ~SceneRenderer();
    
    // Render a scene
    void RenderScene(Scene::World* world, VkCommandBuffer cmd, uint32_t imageIndex);
    
    // Update rendering settings
    void SetShadowsEnabled(bool enabled) { m_shadowsEnabled = enabled; }
    void SetVSyncEnabled(bool enabled) { m_vsyncEnabled = enabled; }
    
private:
    void RenderShadowPass(VkCommandBuffer cmd);
    void RenderOpaquePass(VkCommandBuffer cmd);
    void RenderTransparentPass(VkCommandBuffer cmd);
    void RenderSkyPass(VkCommandBuffer cmd);
    void RenderPostProcessPass(VkCommandBuffer cmd);
    
    RenderContext* m_context;
    
    // Render systems
    std::unique_ptr<ShadowMapper> m_shadowMapper;
    std::unique_ptr<SkyRenderer> m_skyRenderer;
    
    // Pipelines
    std::unique_ptr<RHI::Vulkan::Pipeline> m_opaquePipeline;
    std::unique_ptr<RHI::Vulkan::Pipeline> m_transparentPipeline;
    std::unique_ptr<RHI::Vulkan::Pipeline> m_shadowPipeline;
    
    // Settings
    bool m_shadowsEnabled = true;
    bool m_vsyncEnabled = true;
};