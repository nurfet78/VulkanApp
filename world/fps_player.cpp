// engine/world/fps_player.cpp
#include "fps_player.h"
#include "core/input.h"


namespace World {

FPSPlayer::FPSPlayer(Scene::World* world, Physics::PhysicsWorld* physics)
    : m_world(world), m_physics(physics) {
}

FPSPlayer::~FPSPlayer() {
    if (m_collider) {
        m_physics->RemoveCollider(m_collider);
    }
}

void FPSPlayer::Initialize(const glm::vec3& position) {
    m_position = position;
    m_spawnPosition = position;
    
    // Create entity
    m_entity = m_world->CreateEntity().GetID();
    
    // Add transform
    m_transform = m_world->AddComponent<Scene::Transform>(m_entity, position);
    
    // Add camera
    m_camera = m_world->AddComponent<Scene::Camera>(m_entity);
    m_camera->SetPerspective(60.0f, 16.0f/9.0f, 0.1f, 1000.0f);
    
    // Create physics capsule
    Physics::Capsule capsule(
        position - glm::vec3(0, m_settings.height * 0.5f - m_settings.radius, 0),
        position + glm::vec3(0, m_settings.height * 0.5f - m_settings.radius, 0),
        m_settings.radius
    );
    
    m_collider = m_physics->AddCapsule(capsule, false, this);
}

void FPSPlayer::Update(float deltaTime, Core::Input* input) {
    UpdateRotation(deltaTime, input);
    UpdateMovement(deltaTime, input);
    UpdatePhysics(deltaTime);
    
    // Update transform
    m_transform->SetPosition(m_position + glm::vec3(0, m_settings.height * 0.5f, 0));
    m_transform->SetRotation(glm::quat(glm::vec3(m_pitch, m_yaw, 0)));
    
    // Update camera view matrix
    m_camera->UpdateViewMatrix(*m_transform);
}

void FPSPlayer::UpdateMovement(float deltaTime, Core::Input* input) {
    // Get input
    glm::vec3 wishDir = GetWishDirection(input);
    
    // Check for running
    m_isRunning = input->IsKeyDown(GLFW_KEY_LEFT_SHIFT) && m_isGrounded;
    float wishSpeed = m_isRunning ? m_settings.runSpeed : m_settings.moveSpeed;
    
    // Jump
    if (input->IsKeyPressed(GLFW_KEY_SPACE) && m_isGrounded && m_canJump) {
        Jump();
    }
    
    // Apply movement
    if (m_isGrounded) {
        // Ground movement
        ApplyFriction(deltaTime);
        Accelerate(wishDir, wishSpeed, m_settings.friction, deltaTime);
    } else {
        // Air movement
        Accelerate(wishDir, wishSpeed, m_settings.airControl, deltaTime);
    }
}

void FPSPlayer::UpdateRotation(float deltaTime, Core::Input* input) {
    if (!input->IsCursorLocked()) {
        return;
    }
    
    glm::vec2 mouseDelta = input->GetMouseDelta();
    
    m_yaw -= mouseDelta.x * m_settings.mouseSensitivity;
    m_pitch -= mouseDelta.y * m_settings.mouseSensitivity;
    
    // Clamp pitch
    m_pitch = glm::clamp(m_pitch, -1.5f, 1.5f);
    
    // Wrap yaw
    if (m_yaw > glm::pi<float>()) {
        m_yaw -= glm::two_pi<float>();
    } else if (m_yaw < -glm::pi<float>()) {
        m_yaw += glm::two_pi<float>();
    }
}

void FPSPlayer::UpdatePhysics(float deltaTime) {
    // Check ground
    m_isGrounded = CheckGround();
    
    // Apply gravity
    if (!m_isGrounded) {
        m_velocity.y -= m_settings.gravity * deltaTime;
    } else if (m_velocity.y < 0) {
        m_velocity.y = 0;
    }
    
    // Move with collision
    Physics::Capsule capsule(
        m_position - glm::vec3(0, m_settings.height * 0.5f - m_settings.radius, 0),
        m_position + glm::vec3(0, m_settings.height * 0.5f - m_settings.radius, 0),
        m_settings.radius
    );
    
    glm::vec3 newPosition = m_physics->MoveCapsule(capsule, m_velocity * deltaTime);
    m_position = newPosition;
    
    // Update collider position
    if (m_collider) {
        m_collider->capsule.base = m_position - glm::vec3(0, m_settings.height * 0.5f - m_settings.radius, 0);
        m_collider->capsule.tip = m_position + glm::vec3(0, m_settings.height * 0.5f - m_settings.radius, 0);
    }
    
    // Check if fell off the world
    if (m_position.y < -50.0f) {
        Respawn();
    }
}

bool FPSPlayer::CheckGround() {
    // Cast ray downward from player position
    Physics::Ray ray(
        m_position,
        glm::vec3(0, -1, 0)
    );
    
    Physics::RaycastHit hit;
    float checkDistance = m_settings.height * 0.5f + m_settings.stepHeight;
    
    if (m_physics->Raycast(ray, hit, checkDistance)) {
        float groundDistance = hit.distance - m_settings.height * 0.5f;
        return groundDistance <= m_settings.stepHeight;
    }
    
    return false;
}

glm::vec3 FPSPlayer::GetWishDirection(Core::Input* input) const {
    glm::vec3 wishDir(0.0f);
    
    if (input->IsKeyDown(GLFW_KEY_W)) wishDir.z -= 1.0f;
    if (input->IsKeyDown(GLFW_KEY_S)) wishDir.z += 1.0f;
    if (input->IsKeyDown(GLFW_KEY_A)) wishDir.x -= 1.0f;
    if (input->IsKeyDown(GLFW_KEY_D)) wishDir.x += 1.0f;
    
    if (glm::length(wishDir) > 0.0f) {
        wishDir = glm::normalize(wishDir);
        
        // Transform to world space
        float cosYaw = std::cos(m_yaw);
        float sinYaw = std::sin(m_yaw);
        
        glm::vec3 forward(-sinYaw, 0, -cosYaw);
        glm::vec3 right(cosYaw, 0, -sinYaw);
        
        wishDir = forward * wishDir.z + right * wishDir.x;
    }
    
    return wishDir;
}

void FPSPlayer::ApplyFriction(float deltaTime) {
    float speed = glm::length(glm::vec2(m_velocity.x, m_velocity.z));
    
    if (speed < 0.1f) {
        m_velocity.x = 0;
        m_velocity.z = 0;
        return;
    }
    
    float drop = speed * m_settings.friction * deltaTime;
    float newSpeed = std::max(0.0f, speed - drop);
    float ratio = newSpeed / speed;
    
    m_velocity.x *= ratio;
    m_velocity.z *= ratio;
}

void FPSPlayer::Accelerate(const glm::vec3& wishDir, float wishSpeed, float acceleration, float deltaTime) {
    float currentSpeed = glm::dot(m_velocity, wishDir);
    float addSpeed = wishSpeed - currentSpeed;
    
    if (addSpeed <= 0) {
        return;
    }
    
    float accelSpeed = acceleration * wishSpeed * deltaTime;
    accelSpeed = std::min(accelSpeed, addSpeed);
    
    m_velocity += wishDir * accelSpeed;
}

void FPSPlayer::Jump() {
    if (!m_isGrounded || !m_canJump) {
        return;
    }
    
    // Calculate jump velocity from desired height
    m_velocity.y = std::sqrt(2.0f * m_settings.gravity * m_settings.jumpHeight);
    m_canJump = false;
    
    // Reset jump when grounded
    if (m_isGrounded && m_velocity.y <= 0) {
        m_canJump = true;
    }
}

void FPSPlayer::Respawn() {
    m_position = m_spawnPosition;
    m_velocity = glm::vec3(0.0f);
    m_yaw = 0.0f;
    m_pitch = 0.0f;
}

void FPSPlayer::SetPosition(const glm::vec3& position) {
    m_position = position;
}

void FPSPlayer::SetRotation(float yaw, float pitch) {
    m_yaw = yaw;
    m_pitch = pitch;
}

glm::vec3 FPSPlayer::GetViewDirection() const {
    float cosYaw = std::cos(m_yaw);
    float sinYaw = std::sin(m_yaw);
    float cosPitch = std::cos(m_pitch);
    float sinPitch = std::sin(m_pitch);
    
    return glm::vec3(-sinYaw * cosPitch, sinPitch, -cosYaw * cosPitch);
}

glm::mat4 FPSPlayer::GetViewMatrix() const {
    glm::vec3 eye = m_position + glm::vec3(0, m_settings.height * 0.5f, 0);
    glm::vec3 center = eye + GetViewDirection();
    return glm::lookAt(eye, center, glm::vec3(0, 1, 0));
}

} // namespace World