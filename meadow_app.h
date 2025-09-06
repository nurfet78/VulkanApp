// Refactored MeadowApp
// meadow_app.h - REFACTORED VERSION
#pragma once

#include "core/application.h"
#include <memory>

namespace Renderer {
    class RenderContext;
    class FrameManager;
    class SceneRenderer;
}

namespace Scene {
    class World;
}

namespace Physics {
    class PhysicsWorld;
}

namespace World {
    class MeadowScene;
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
    // Core systems
    std::unique_ptr<Renderer::RenderContext> m_renderContext;
    std::unique_ptr<Renderer::FrameManager> m_frameManager;
    std::unique_ptr<Renderer::SceneRenderer> m_sceneRenderer;
    
    // Game systems
    std::unique_ptr<Scene::World> m_sceneWorld;
    std::unique_ptr<Physics::PhysicsWorld> m_physicsWorld;
    std::unique_ptr<World::MeadowScene> m_meadowScene;
    
    // Statistics
    float m_totalTime = 0.0f;
    uint32_t m_frameCounter = 0;
    float m_fpsTimer = 0.0f;
};