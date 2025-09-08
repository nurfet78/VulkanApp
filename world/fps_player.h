// engine/world/fps_player.h
#pragma once

#include "scene/entity.h"
#include "scene/components.h"
#include "physics/collider.h"

namespace Core { class Input; }

namespace World {

class FPSPlayer {
public:
    struct Settings {
        float moveSpeed = 5.0f;
        float runSpeed = 10.0f;
        float jumpHeight = 2.0f;
        float mouseSensitivity = 0.002f;
        float height = 1.8f;
        float radius = 0.3f;
        float stepHeight = 0.3f;
        float gravity = 9.81f;
        float friction = 10.0f;
        float airControl = 0.1f;
    };
    
    FPSPlayer(Scene::World* world, Physics::PhysicsWorld* physics);
    ~FPSPlayer();
    
    void Initialize(const glm::vec3& position);
    void Update(float deltaTime, Core::Input* input);
    
    // Getters
    glm::vec3 GetPosition() const { return m_position; }
    glm::vec3 GetViewDirection() const;
    glm::mat4 GetViewMatrix() const;
    Scene::EntityID GetEntity() const { return m_entity; }
    Scene::Camera* GetCamera() { return m_camera; }
    
    // Setters
    void SetPosition(const glm::vec3& position);
    void SetRotation(float yaw, float pitch);
    void SetSettings(const Settings& settings) { m_settings = settings; }
    
    // State
    bool IsGrounded() const { return m_isGrounded; }
    bool IsRunning() const { return m_isRunning; }
    bool IsJumping() const { return m_velocity.y > 0.1f; }
    
    // Actions
    void Jump();
    void Respawn();

private:
    void UpdateMovement(float deltaTime, Core::Input* input);
    void UpdateRotation(float deltaTime, Core::Input* input);
    void UpdatePhysics(float deltaTime);
    bool CheckGround();
    glm::vec3 GetWishDirection(Core::Input* input) const;
    void ApplyFriction(float deltaTime);
    void Accelerate(const glm::vec3& wishDir, float wishSpeed, float acceleration, float deltaTime);
    
    Scene::World* m_world;
    Physics::PhysicsWorld* m_physics;
    
    Scene::EntityID m_entity;
    Scene::Transform* m_transform;
    Scene::Camera* m_camera;
    Physics::PhysicsWorld::Collider* m_collider;
    
    Settings m_settings;
    
    // Movement state
    glm::vec3 m_position;
    glm::vec3 m_velocity{0.0f};
    float m_yaw = 0.0f;
    float m_pitch = 0.0f;
    
    bool m_isGrounded = false;
    bool m_isRunning = false;
    bool m_canJump = true;
    
    glm::vec3 m_spawnPosition;
};

} // namespace World