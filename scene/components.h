// engine/scene/components.h
#pragma once

#include "entity.h"


namespace Scene {

// Transform Component
class Transform : public Component {
public:
    Transform() = default;
    
    Transform(const glm::vec3& position, 
              const glm::quat& rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
              const glm::vec3& scale = glm::vec3(1.0f))
        : m_position(position), m_rotation(rotation), m_scale(scale) {
        UpdateMatrix();
    }
    
    // Position
    void SetPosition(const glm::vec3& position) {
        m_position = position;
        UpdateMatrix();
    }
    glm::vec3 GetPosition() const { return m_position; }
    
    // Rotation
    void SetRotation(const glm::quat& rotation) {
        m_rotation = rotation;
        UpdateMatrix();
    }
    void SetRotationEuler(const glm::vec3& euler) {
        m_rotation = glm::quat(euler);
        UpdateMatrix();
    }
    glm::quat GetRotation() const { return m_rotation; }
    glm::vec3 GetRotationEuler() const { return glm::eulerAngles(m_rotation); }
    
    // Scale
    void SetScale(const glm::vec3& scale) {
        m_scale = scale;
        UpdateMatrix();
    }
    void SetScale(float uniformScale) {
        m_scale = glm::vec3(uniformScale);
        UpdateMatrix();
    }
    glm::vec3 GetScale() const { return m_scale; }
    
    // Matrix
    const glm::mat4& GetMatrix() const { return m_matrix; }
    
    // Helpers
    glm::vec3 GetForward() const {
        return glm::rotate(m_rotation, glm::vec3(0.0f, 0.0f, -1.0f));
    }
    glm::vec3 GetRight() const {
        return glm::rotate(m_rotation, glm::vec3(1.0f, 0.0f, 0.0f));
    }
    glm::vec3 GetUp() const {
        return glm::rotate(m_rotation, glm::vec3(0.0f, 1.0f, 0.0f));
    }
    
    void LookAt(const glm::vec3& target, const glm::vec3& up = glm::vec3(0.0f, 1.0f, 0.0f)) {
        glm::vec3 direction = glm::normalize(target - m_position);
        m_rotation = glm::quatLookAt(direction, up);
        UpdateMatrix();
    }

private:
    void UpdateMatrix() {
        glm::mat4 t = glm::translate(glm::mat4(1.0f), m_position);
        glm::mat4 r = glm::mat4_cast(m_rotation);
        glm::mat4 s = glm::scale(glm::mat4(1.0f), m_scale);
        m_matrix = t * r * s;
    }
    
    glm::vec3 m_position{0.0f};
    glm::quat m_rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 m_scale{1.0f};
    glm::mat4 m_matrix{1.0f};
};

// Mesh Component
class MeshComponent : public Component {
public:
    MeshComponent() = default;
    explicit MeshComponent(const std::string& meshName) : m_meshName(meshName) {}
    
    void SetMesh(const std::string& meshName) { m_meshName = meshName; }
    const std::string& GetMesh() const { return m_meshName; }
    
    void SetMaterial(const std::string& materialName) { m_materialName = materialName; }
    const std::string& GetMaterial() const { return m_materialName; }
    
    void SetVisible(bool visible) { m_visible = visible; }
    bool IsVisible() const { return m_visible; }
    
    void SetCastShadows(bool cast) { m_castShadows = cast; }
    bool CastsShadows() const { return m_castShadows; }

private:
    std::string m_meshName;
    std::string m_materialName = "default";
    bool m_visible = true;
    bool m_castShadows = true;
};

// Instance data for GPU instancing
struct InstanceData {
    glm::mat4 modelMatrix;
    glm::vec4 color = glm::vec4(1.0f);
    uint32_t textureIndex = 0;
    uint32_t pad[3] = {0, 0, 0};
};

// Instanced Mesh Component (for grass, trees, etc.)
class InstancedMeshComponent : public Component {
public:
    InstancedMeshComponent() = default;
    explicit InstancedMeshComponent(const std::string& meshName) : m_meshName(meshName) {}
    
    void SetMesh(const std::string& meshName) { m_meshName = meshName; }
    const std::string& GetMesh() const { return m_meshName; }
    
    void AddInstance(const glm::mat4& transform, const glm::vec4& color = glm::vec4(1.0f)) {
        InstanceData instance;
        instance.modelMatrix = transform;
        instance.color = color;
        m_instances.push_back(instance);
        m_dirty = true;
    }
    
    void ClearInstances() {
        m_instances.clear();
        m_dirty = true;
    }
    
    void UpdateInstance(size_t index, const glm::mat4& transform) {
        if (index < m_instances.size()) {
            m_instances[index].modelMatrix = transform;
            m_dirty = true;
        }
    }
    
    const std::vector<InstanceData>& GetInstances() const { return m_instances; }
    size_t GetInstanceCount() const { return m_instances.size(); }
    
    bool IsDirty() const { return m_dirty; }
    void ClearDirty() { m_dirty = false; }
    
    void SetLOD(uint32_t lod) { m_currentLOD = lod; }
    uint32_t GetLOD() const { return m_currentLOD; }

private:
    std::string m_meshName;
    std::vector<InstanceData> m_instances;
    bool m_dirty = false;
    uint32_t m_currentLOD = 0;
};

// Camera Component
class Camera : public Component {
public:
    enum class ProjectionType {
        Perspective,
        Orthographic
    };
    
    Camera() { UpdateProjection(); }
    
    // Projection settings
    void SetPerspective(float fov, float aspectRatio, float nearPlane, float farPlane) {
        m_projectionType = ProjectionType::Perspective;
        m_fov = fov;
        m_aspectRatio = aspectRatio;
        m_nearPlane = nearPlane;
        m_farPlane = farPlane;
        UpdateProjection();
    }
    
    void SetOrthographic(float left, float right, float bottom, float top, float nearPlane, float farPlane) {
        m_projectionType = ProjectionType::Orthographic;
        m_orthoLeft = left;
        m_orthoRight = right;
        m_orthoBottom = bottom;
        m_orthoTop = top;
        m_nearPlane = nearPlane;
        m_farPlane = farPlane;
        UpdateProjection();
    }
    
    void SetAspectRatio(float aspectRatio) {
        m_aspectRatio = aspectRatio;
        if (m_projectionType == ProjectionType::Perspective) {
            UpdateProjection();
        }
    }
    
    void SetFOV(float fov) {
        m_fov = fov;
        if (m_projectionType == ProjectionType::Perspective) {
            UpdateProjection();
        }
    }
    
    const glm::mat4& GetProjectionMatrix() const { return m_projectionMatrix; }
    const glm::mat4& GetViewMatrix() const { return m_viewMatrix; }
    glm::mat4 GetViewProjectionMatrix() const { return m_projectionMatrix * m_viewMatrix; }
    
    void UpdateViewMatrix(const Transform& transform) {
        m_viewMatrix = glm::inverse(transform.GetMatrix());
    }
    
    float GetNearPlane() const { return m_nearPlane; }
    float GetFarPlane() const { return m_farPlane; }
    float GetFOV() const { return m_fov; }
    float GetAspectRatio() const { return m_aspectRatio; }
    
    // Frustum for culling
    struct Frustum {
        glm::vec4 planes[6]; // left, right, bottom, top, near, far
    };
    
    Frustum GetFrustum() const {
        Frustum frustum;
        glm::mat4 vp = GetViewProjectionMatrix();
        
        // Extract frustum planes
        for (int i = 0; i < 3; i++) {
            frustum.planes[i * 2] = glm::vec4(
                vp[0][3] + vp[0][i],
                vp[1][3] + vp[1][i],
                vp[2][3] + vp[2][i],
                vp[3][3] + vp[3][i]
            );
            frustum.planes[i * 2 + 1] = glm::vec4(
                vp[0][3] - vp[0][i],
                vp[1][3] - vp[1][i],
                vp[2][3] - vp[2][i],
                vp[3][3] - vp[3][i]
            );
        }
        
        // Normalize planes
        for (int i = 0; i < 6; i++) {
            float length = glm::length(glm::vec3(frustum.planes[i]));
            frustum.planes[i] /= length;
        }
        
        return frustum;
    }

private:
    void UpdateProjection() {
        if (m_projectionType == ProjectionType::Perspective) {
            m_projectionMatrix = glm::perspective(glm::radians(m_fov), m_aspectRatio, m_nearPlane, m_farPlane);
        } else {
            m_projectionMatrix = glm::ortho(m_orthoLeft, m_orthoRight, m_orthoBottom, m_orthoTop, m_nearPlane, m_farPlane);
        }
        m_projectionMatrix[1][1] *= -1; // Flip Y for Vulkan
    }
    
    ProjectionType m_projectionType = ProjectionType::Perspective;
    
    // Perspective params
    float m_fov = 60.0f;
    float m_aspectRatio = 16.0f / 9.0f;
    
    // Orthographic params
    float m_orthoLeft = -10.0f;
    float m_orthoRight = 10.0f;
    float m_orthoBottom = -10.0f;
    float m_orthoTop = 10.0f;
    
    // Common params
    float m_nearPlane = 0.1f;
    float m_farPlane = 1000.0f;
    
    glm::mat4 m_projectionMatrix{1.0f};
    glm::mat4 m_viewMatrix{1.0f};
};

// Light Component
class Light : public Component {
public:
    enum class Type {
        Directional,
        Point,
        Spot
    };

    // Добавляем enum для типов освещения
    enum class ShadingModel {
        Lambert,        // Обычное освещение max(dot(N,L), 0)
        HalfLambert,    // Мягкое освещение (dot(N,L) * 0.5 + 0.5)^2
        OrenNayar,      // Для шероховатых поверхностей
        Minnaert,       // Для бархатистых поверхностей
        Toon           // Cartoon-style освещение
    };
    
    Light() = default;
    explicit Light(Type type) : m_type(type) {}

    // Existing methods...
    void SetType(Type type) { m_type = type; }
    Type GetType() const { return m_type; }

    void SetColor(const glm::vec3& color) { m_color = color; }
    glm::vec3 GetColor() const { return m_color; }

    void SetIntensity(float intensity) { m_intensity = intensity; }
    float GetIntensity() const { return m_intensity; }

    void SetDirection(const glm::vec3& direction) { m_direction = glm::normalize(direction); }
    glm::vec3 GetDirection() const { return m_direction; }

    void SetRange(float range) { m_range = range; }
    float GetRange() const { return m_range; }

    void SetInnerCone(float angle) { m_innerCone = angle; }
    float GetInnerCone() const { return m_innerCone; }

    void SetOuterCone(float angle) { m_outerCone = angle; }
    float GetOuterCone() const { return m_outerCone; }

    void SetCastShadows(bool cast) { m_castShadows = cast; }
    bool CastsShadows() const { return m_castShadows; }

    void SetShadowBias(float bias) { m_shadowBias = bias; }
    float GetShadowBias() const { return m_shadowBias; }

    // === НОВЫЕ МЕТОДЫ для shading model ===
    void SetShadingModel(ShadingModel model) { m_shadingModel = model; }
    ShadingModel GetShadingModel() const { return m_shadingModel; }

    // Параметры для Half-Lambert
    void SetWrapFactor(float wrap) { m_wrapFactor = wrap; }
    float GetWrapFactor() const { return m_wrapFactor; }

    // Параметр для Toon shading (количество уровней)
    void SetToonSteps(int steps) { m_toonSteps = steps; }
    int GetToonSteps() const { return m_toonSteps; }

    // Параметр мягкости для различных моделей
    void SetSoftness(float softness) { m_softness = softness; }
    float GetSoftness() const { return m_softness; }

private:
    Type m_type = Type::Directional;
    glm::vec3 m_color{ 1.0f };
    float m_intensity = 1.0f;
    glm::vec3 m_direction{ 0.0f, -1.0f, 0.0f };
    float m_range = 10.0f;
    float m_innerCone = 30.0f;
    float m_outerCone = 45.0f;
    bool m_castShadows = true;
    float m_shadowBias = 0.005f;

    ShadingModel m_shadingModel = ShadingModel::Lambert;
    float m_wrapFactor = 0.5f;      // Для Half-Lambert: насколько "заворачивать" освещение
    int m_toonSteps = 3;            // Для Toon shading: количество ступеней освещения
    float m_softness = 1.0f;        // Общий параметр мягкости
};

// Tag Component for entity identification
class Tag : public Component {
public:
    Tag() = default;
    explicit Tag(const std::string& name) : m_name(name) {}
    
    void SetName(const std::string& name) { m_name = name; }
    const std::string& GetName() const { return m_name; }

private:
    std::string m_name;
};

} // namespace Scene