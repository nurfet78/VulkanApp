// engine/world/meadow_scene.cpp
#include "meadow_scene.h"
#include "fps_player.h"
#include "scene/components.h"
#include "rhi/vulkan/resource.h"
#include "core/input.h"
#include <random>

namespace World {

MeadowScene::MeadowScene(Scene::World* sceneWorld, Physics::PhysicsWorld* physicsWorld,
                       RHI::Vulkan::Device* device, RHI::Vulkan::ResourceManager* resources)
    : m_sceneWorld(sceneWorld), m_physicsWorld(physicsWorld),
      m_device(device), m_resources(resources) {
    m_currentLighting = m_outdoorLighting;
}

MeadowScene::~MeadowScene() {
    if (m_houseTrigger) {
        m_physicsWorld->RemoveCollider(m_houseTrigger);
    }
}

void MeadowScene::Initialize() {
    // Create player
    m_player = std::make_unique<FPSPlayer>(m_sceneWorld, m_physicsWorld);
    m_player->Initialize(glm::vec3(0, 2, 10));
    
    // Build scene
    CreateTerrain();
    CreateHouse();
    CreateVegetation();
    CreateLighting();
    SetupTriggers();
}

void MeadowScene::Update(float deltaTime, Core::Input* input) {
    // Update player
    m_player->Update(deltaTime, input);
    
    // Update time of day
    if (input->IsKeyDown(GLFW_KEY_LEFT_BRACKET)) {
        m_timeOfDay -= deltaTime * 2.0f;
        if (m_timeOfDay < 0) m_timeOfDay += 24.0f;
    }
    if (input->IsKeyDown(GLFW_KEY_RIGHT_BRACKET)) {
        m_timeOfDay += deltaTime * 2.0f;
        if (m_timeOfDay >= 24.0f) m_timeOfDay -= 24.0f;
    }
    
    // Update lighting transition
    LightingState targetLighting = m_isInsideHouse ? m_indoorLighting : m_outdoorLighting;
    float transitionSpeed = 2.0f;
    
    m_currentLighting.ambientColor = glm::mix(m_currentLighting.ambientColor, 
                                              targetLighting.ambientColor, 
                                              deltaTime * transitionSpeed);
    m_currentLighting.ambientIntensity = glm::mix(m_currentLighting.ambientIntensity,
                                                  targetLighting.ambientIntensity,
                                                  deltaTime * transitionSpeed);
    m_currentLighting.sunIntensity = glm::mix(m_currentLighting.sunIntensity,
                                             targetLighting.sunIntensity,
                                             deltaTime * transitionSpeed);
    
    // Update sun light
    if (m_sunLight != Scene::INVALID_ENTITY) {
        auto* light = m_sceneWorld->GetComponent<Scene::Light>(m_sunLight);
        if (light) {
            // Calculate sun direction based on time of day
            float angle = (m_timeOfDay / 24.0f) * glm::two_pi<float>() - glm::half_pi<float>();
            glm::vec3 sunDir(
                std::cos(angle) * 0.5f,
                -std::sin(angle),
                -0.5f
            );
            light->SetDirection(sunDir);
            light->SetIntensity(m_currentLighting.sunIntensity);
        }
    }
    
    // Update physics
    m_physicsWorld->Update(deltaTime);
}

void MeadowScene::CreateTerrain() {
    // Create ground plane
    m_terrain = m_sceneWorld->CreateEntity().GetID();
    
    auto* transform = m_sceneWorld->AddComponent<Scene::Transform>(
        m_terrain, glm::vec3(0, 0, 0), glm::quat(), glm::vec3(50, 1, 50)
    );
    
    auto* mesh = m_sceneWorld->AddComponent<Scene::MeshComponent>(m_terrain, "plane");
    mesh->SetMaterial("grass");
    
    // Add ground collision
    Physics::AABB groundAABB(glm::vec3(-50, -0.5f, -50), glm::vec3(50, 0, 50));
    m_physicsWorld->AddAABB(groundAABB, true, nullptr);
}

void MeadowScene::CreateHouse() {
    // Create simple house from cubes
    m_house = m_sceneWorld->CreateEntity().GetID();
    
    // House base
    auto* transform = m_sceneWorld->AddComponent<Scene::Transform>(
        m_house, glm::vec3(0, 2, 0), glm::quat(), glm::vec3(6, 4, 8)
    );
    
    auto* mesh = m_sceneWorld->AddComponent<Scene::MeshComponent>(m_house, "cube");
    mesh->SetMaterial("wood");
    
    // Add house collision (walls)
    // Front wall with door gap
    m_physicsWorld->AddAABB(Physics::AABB(
        glm::vec3(-3, 0, -4), glm::vec3(-1, 4, -3.5f)
    ), true, nullptr);
    m_physicsWorld->AddAABB(Physics::AABB(
        glm::vec3(1, 0, -4), glm::vec3(3, 4, -3.5f)
    ), true, nullptr);
    
    // Back wall
    m_physicsWorld->AddAABB(Physics::AABB(
        glm::vec3(-3, 0, 3.5f), glm::vec3(3, 4, 4)
    ), true, nullptr);
    
    // Side walls
    m_physicsWorld->AddAABB(Physics::AABB(
        glm::vec3(-3, 0, -4), glm::vec3(-2.5f, 4, 4)
    ), true, nullptr);
    m_physicsWorld->AddAABB(Physics::AABB(
        glm::vec3(2.5f, 0, -4), glm::vec3(3, 4, 4)
    ), true, nullptr);
    
    // Roof (separate entity)
    Scene::EntityID roof = m_sceneWorld->CreateEntity().GetID();
    m_sceneWorld->AddComponent<Scene::Transform>(
        roof, glm::vec3(0, 4, 0), 
        glm::quat(glm::vec3(0, 0, glm::radians(45.0f))),
        glm::vec3(7, 0.3f, 9)
    );
    m_sceneWorld->AddComponent<Scene::MeshComponent>(roof, "cube")->SetMaterial("roof");
}

void MeadowScene::CreateVegetation() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> posDistX(-40, 40);
    std::uniform_real_distribution<float> posDistZ(-40, 40);
    std::uniform_real_distribution<float> scaleVar(0.8f, 1.2f);
    std::uniform_real_distribution<float> rotDist(0, glm::two_pi<float>());
    
    // Create grass patches using instancing
    Scene::EntityID grassInstances = m_sceneWorld->CreateEntity().GetID();
    auto* grassMesh = m_sceneWorld->AddComponent<Scene::InstancedMeshComponent>(grassInstances, "grass");
    
    for (int i = 0; i < 500; i++) {
        float x = posDistX(gen);
        float z = posDistZ(gen);
        
        // Skip grass near house
        if (std::abs(x) < 5 && std::abs(z) < 6) continue;
        
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(x, 0, z));
        transform = glm::rotate(transform, rotDist(gen), glm::vec3(0, 1, 0));
        transform = glm::scale(transform, glm::vec3(scaleVar(gen)));
        
        glm::vec4 color(0.3f + (gen() % 100) / 300.0f, 0.8f, 0.2f, 1.0f);
        grassMesh->AddInstance(transform, color);
    }
    
    // Create trees using instancing
    Scene::EntityID treeInstances = m_sceneWorld->CreateEntity().GetID();
    auto* treeMesh = m_sceneWorld->AddComponent<Scene::InstancedMeshComponent>(treeInstances, "tree");
    
    for (int i = 0; i < 50; i++) {
        float x = posDistX(gen);
        float z = posDistZ(gen);
        
        // Skip trees too close to house
        if (std::abs(x) < 8 && std::abs(z) < 10) continue;
        
        float height = 4.0f + scaleVar(gen) * 3.0f;
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(x, 0, z));
        transform = glm::rotate(transform, rotDist(gen), glm::vec3(0, 1, 0));
        transform = glm::scale(transform, glm::vec3(1.0f, height, 1.0f));
        
        treeMesh->AddInstance(transform);
        
        // Add tree collision
        m_physicsWorld->AddAABB(Physics::AABB(
            glm::vec3(x - 0.5f, 0, z - 0.5f),
            glm::vec3(x + 0.5f, height, z + 0.5f)
        ), true, nullptr);
    }
}

void MeadowScene::CreateLighting() {
    // Create sun (directional light)
    m_sunLight = m_sceneWorld->CreateEntity().GetID();
    
    auto* light = m_sceneWorld->AddComponent<Scene::Light>(m_sunLight, Scene::Light::Type::Directional);
    light->SetDirection(glm::vec3(-0.5f, -1.0f, -0.5f));
    light->SetColor(glm::vec3(1.0f, 0.95f, 0.8f));
    light->SetIntensity(3.0f);
    light->SetCastShadows(true);
    
    // Add some point lights inside house
    Scene::EntityID houseLight = m_sceneWorld->CreateEntity().GetID();
    m_sceneWorld->AddComponent<Scene::Transform>(houseLight, glm::vec3(0, 3, 0));
    auto* pointLight = m_sceneWorld->AddComponent<Scene::Light>(houseLight, Scene::Light::Type::Point);
    pointLight->SetColor(glm::vec3(1.0f, 0.8f, 0.5f));
    pointLight->SetIntensity(2.0f);
    pointLight->SetRange(10.0f);
}

void MeadowScene::SetupTriggers() {
    // Create house entry trigger
    Physics::AABB doorTrigger(glm::vec3(-1, 0, -4.5f), glm::vec3(1, 3, -3));
    m_houseTrigger = m_physicsWorld->AddAABB(doorTrigger, true, this);
    m_houseTrigger->isTrigger = true;
    
    // Setup callbacks
    m_houseTrigger->onTriggerEnter = [this](Physics::PhysicsWorld::Collider* trigger,
                                           Physics::PhysicsWorld::Collider* other) {
        if (other->userData == m_player.get()) {
            OnEnterHouse();
        }
    };
    
    m_houseTrigger->onTriggerExit = [this](Physics::PhysicsWorld::Collider* trigger,
                                          Physics::PhysicsWorld::Collider* other) {
        if (other->userData == m_player.get()) {
            OnExitHouse();
        }
    };
}

void MeadowScene::OnEnterHouse() {
    m_isInsideHouse = true;
    // Could add sound effects, UI notifications, etc.
}

void MeadowScene::OnExitHouse() {
    m_isInsideHouse = false;
}

} // namespace World