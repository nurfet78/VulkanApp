// engine/world/meadow_scene.h
#pragma once

#include "scene/entity.h"
#include "physics/collider.h"
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace Scene { class World; }
namespace RHI::Vulkan { class Device; class ResourceManager; }
namespace Renderer { class ShadowMapper; class SkyRenderer; }
namespace Core { class Input; }

namespace World {

class FPSPlayer;

class MeadowScene {
public:
    MeadowScene(Scene::World* sceneWorld, Physics::PhysicsWorld* physicsWorld,
               RHI::Vulkan::Device* device, RHI::Vulkan::ResourceManager* resources);
    ~MeadowScene();
    
    void Initialize();
    void Update(float deltaTime, Core::Input* input);
    void Render(VkCommandBuffer cmd);
    
    FPSPlayer* GetPlayer() { return m_player.get(); }
    Scene::EntityID GetSunLight() const { return m_sunLight; }
    
    // Scene state
    bool IsInsideHouse() const { return m_isInsideHouse; }
    float GetTimeOfDay() const { return m_timeOfDay; }
    void SetTimeOfDay(float time) { m_timeOfDay = time; }
    
private:
    void CreateTerrain();
    void CreateHouse();
    void CreateVegetation();
    void CreateLighting();
    void SetupTriggers();
    
    void OnEnterHouse();
    void OnExitHouse();
    
    Scene::EntityID CreateGrass(const glm::vec3& position, float scale = 1.0f);
    Scene::EntityID CreateTree(const glm::vec3& position, float height = 5.0f);
    
    Scene::World* m_sceneWorld;
    Physics::PhysicsWorld* m_physicsWorld;
    RHI::Vulkan::Device* m_device;
    RHI::Vulkan::ResourceManager* m_resources;
    
    std::unique_ptr<FPSPlayer> m_player;
    
    // Scene entities
    Scene::EntityID m_terrain;
    Scene::EntityID m_house;
    Scene::EntityID m_sunLight;
    std::vector<Scene::EntityID> m_trees;
    std::vector<Scene::EntityID> m_grassPatches;
    
    // House trigger
    Physics::PhysicsWorld::Collider* m_houseTrigger = nullptr;
    
    // Scene state
    bool m_isInsideHouse = false;
    float m_timeOfDay = 12.0f; // Noon
    
    // Lighting states
    struct LightingState {
        glm::vec3 ambientColor;
        float ambientIntensity;
        glm::vec3 sunColor;
        float sunIntensity;
        float fogDensity;
    };
    
    LightingState m_outdoorLighting{
        glm::vec3(0.3f, 0.4f, 0.5f), 0.5f,
        glm::vec3(1.0f, 0.95f, 0.8f), 3.0f,
        0.01f
    };
    
    LightingState m_indoorLighting{
        glm::vec3(0.2f, 0.2f, 0.15f), 0.3f,
        glm::vec3(0.8f, 0.7f, 0.5f), 0.5f,
        0.0f
    };
    
    LightingState m_currentLighting;
    float m_lightTransition = 0.0f;
};

} // namespace World