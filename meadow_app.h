// meadow_app.h - FIXED VERSION
#pragma once

#include "core/application.h"
#include <memory>
#include <array>

// Forward declarations
namespace RHI::Vulkan {
    class Device;
    class Swapchain;
    class CommandPoolManager;
    class ResourceManager;
    class ShaderManager;
    class Image;
}


namespace Renderer {
    class TriangleRenderer;
    class CubeRenderer;
    class MaterialSystem;
}

namespace Core {
    class CoreContext;
}

namespace Scene {
    class Camera;
    class Transform;
    class Light;
}

class MeadowApp : public Core::Application {
public:
    MeadowApp();
    ~MeadowApp() noexcept;

protected:
    void OnInitialize() override;
    void OnShutdown() override;
    void OnUpdate(float deltaTime) override;
    void OnRender() override;

private:
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
    
    struct FrameData {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
        VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;
        VkFence inFlightFence = VK_NULL_HANDLE;
    };

    void CreateSyncObjects();
    void CreateDepthBuffer();
    void InitializeScene();
    void DestroySyncObjects();
    void RecreateSwapchain();
    void RecordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex);
    void DrawFrame();

    void CollectLightData(float currentTime);

    void LoadShaders();

    void Update();
    void UpdateCamera();

    void HandleSkyControls();

    GLFWwindow* m_window = nullptr;

    // Core Vulkan objects
    std::unique_ptr<RHI::Vulkan::Device> m_device;
    std::unique_ptr<RHI::Vulkan::ShaderManager> m_shaderManager;
    std::unique_ptr<RHI::Vulkan::Swapchain> m_swapchain;
    std::unique_ptr<RHI::Vulkan::CommandPoolManager> m_commandPoolManager;
    std::unique_ptr<RHI::Vulkan::ResourceManager> m_resourceManager;
    std::unique_ptr<Renderer::TriangleRenderer> m_trianglePipeline;
    std::unique_ptr<Core::CoreContext> m_coreContext;

    std::unique_ptr<Renderer::MaterialSystem> m_materialSystem;
    std::unique_ptr<Renderer::CubeRenderer> m_cubeRenderer;
    std::unique_ptr<RHI::Vulkan::Image> m_depthBuffer;
    std::unique_ptr<Scene::Camera> m_camera;
    std::unique_ptr<Scene::Transform> m_cubeTransform;

    // Источники света
    std::unique_ptr<Scene::Light> m_sunLight;      // Солнце
    std::unique_ptr<Scene::Transform> m_sunTransform;

    std::unique_ptr<Scene::Light> m_pointLight;    // Точечный свет
    std::unique_ptr<Scene::Transform> m_pointLightTransform;

    // Camera control variables
    glm::vec3 m_cameraPos{ 5.0f, 3.0f, 5.0f };  // Initial camera position
    float m_cameraRotationX = 0.0f;
    float m_cameraRotationY = 0.0f;

    // Время
    float m_deltaTime = 0.0f;
    float m_lastFrameTime = 0.0f;
    
    // Frame data
    std::array<FrameData, MAX_FRAMES_IN_FLIGHT> m_frames{};
    uint32_t m_currentFrame = 0;
    uint32_t m_swapchainImageCount = 0;
    bool m_framebufferResized = false;
    
    // Statistics
    float m_totalTime = 0.0f;
    uint32_t m_frameCounter = 0;
    float m_fpsTimer = 0.0f;
};