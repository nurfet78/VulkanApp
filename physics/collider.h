// engine/physics/collider.h
#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <functional>
#include <memory>

namespace Physics {

// Basic shapes for collision detection
struct AABB {
    glm::vec3 min;
    glm::vec3 max;
    
    AABB() : min(FLT_MAX), max(-FLT_MAX) {}
    AABB(const glm::vec3& min, const glm::vec3& max) : min(min), max(max) {}
    
    glm::vec3 GetCenter() const { return (min + max) * 0.5f; }
    glm::vec3 GetExtents() const { return (max - min) * 0.5f; }
    
    bool Contains(const glm::vec3& point) const {
        return point.x >= min.x && point.x <= max.x &&
               point.y >= min.y && point.y <= max.y &&
               point.z >= min.z && point.z <= max.z;
    }
    
    bool Intersects(const AABB& other) const {
        return min.x <= other.max.x && max.x >= other.min.x &&
               min.y <= other.max.y && max.y >= other.min.y &&
               min.z <= other.max.z && max.z >= other.min.z;
    }
    
    void Expand(const glm::vec3& point) {
        min = glm::min(min, point);
        max = glm::max(max, point);
    }
    
    void Expand(const AABB& other) {
        min = glm::min(min, other.min);
        max = glm::max(max, other.max);
    }
    
    AABB Transform(const glm::mat4& transform) const;
};

struct Sphere {
    glm::vec3 center;
    float radius;
    
    Sphere() : center(0.0f), radius(0.0f) {}
    Sphere(const glm::vec3& center, float radius) : center(center), radius(radius) {}
    
    bool Contains(const glm::vec3& point) const {
        return glm::distance(center, point) <= radius;
    }
    
    bool Intersects(const Sphere& other) const {
        float distance = glm::distance(center, other.center);
        return distance <= (radius + other.radius);
    }
    
    bool Intersects(const AABB& aabb) const;
};

struct Capsule {
    glm::vec3 base;
    glm::vec3 tip;
    float radius;
    
    Capsule() : base(0.0f), tip(0.0f, 1.0f, 0.0f), radius(0.5f) {}
    Capsule(const glm::vec3& base, const glm::vec3& tip, float radius)
        : base(base), tip(tip), radius(radius) {}
    
    glm::vec3 GetCenter() const { return (base + tip) * 0.5f; }
    float GetHeight() const { return glm::distance(base, tip); }
    glm::vec3 GetAxis() const { return glm::normalize(tip - base); }
    
    bool Intersects(const AABB& aabb) const;
    bool Intersects(const Sphere& sphere) const;
    bool Intersects(const Capsule& other) const;
    
    glm::vec3 GetClosestPoint(const glm::vec3& point) const;
};

struct Plane {
    glm::vec3 normal;
    float distance;
    
    Plane() : normal(0.0f, 1.0f, 0.0f), distance(0.0f) {}
    Plane(const glm::vec3& normal, float distance) 
        : normal(glm::normalize(normal)), distance(distance) {}
    Plane(const glm::vec3& normal, const glm::vec3& point)
        : normal(glm::normalize(normal)), distance(glm::dot(normal, point)) {}
    
    float GetSignedDistance(const glm::vec3& point) const {
        return glm::dot(normal, point) - distance;
    }
    
    bool IsAbove(const glm::vec3& point) const {
        return GetSignedDistance(point) > 0.0f;
    }
    
    glm::vec3 Project(const glm::vec3& point) const {
        return point - normal * GetSignedDistance(point);
    }
};

// Ray for raycasting
struct Ray {
    glm::vec3 origin;
    glm::vec3 direction;
    
    Ray() : origin(0.0f), direction(0.0f, 0.0f, -1.0f) {}
    Ray(const glm::vec3& origin, const glm::vec3& direction)
        : origin(origin), direction(glm::normalize(direction)) {}
    
    glm::vec3 GetPoint(float t) const { return origin + direction * t; }
    
    bool Intersects(const AABB& aabb, float& tMin, float& tMax) const;
    bool Intersects(const Sphere& sphere, float& t) const;
    bool Intersects(const Plane& plane, float& t) const;
};

// Hit information for raycasts
struct RaycastHit {
    glm::vec3 point;
    glm::vec3 normal;
    float distance;
    void* userData = nullptr;
    
    bool IsValid() const { return distance < FLT_MAX; }
};

// Collision manifold for detailed collision info
struct CollisionManifold {
    glm::vec3 normal;
    float penetration;
    std::vector<glm::vec3> contactPoints;
    
    bool HasCollision() const { return !contactPoints.empty(); }
};

// Collision detection functions
namespace Collision {
    bool TestAABBvsAABB(const AABB& a, const AABB& b, CollisionManifold* manifold = nullptr);
    bool TestSphereVsSphere(const Sphere& a, const Sphere& b, CollisionManifold* manifold = nullptr);
    bool TestCapsuleVsAABB(const Capsule& capsule, const AABB& aabb, CollisionManifold* manifold = nullptr);
    bool TestCapsuleVsCapsule(const Capsule& a, const Capsule& b, CollisionManifold* manifold = nullptr);
    
    // Sweep tests for continuous collision detection
    bool SweepAABBvsAABB(const AABB& a, const glm::vec3& velocityA,
                        const AABB& b, const glm::vec3& velocityB,
                        float& tFirst, float& tLast);
    
    bool SweepCapsuleVsAABB(const Capsule& capsule, const glm::vec3& velocity,
                           const AABB& aabb, float& t, glm::vec3& normal);
}

// Simple physics world for collision queries
class PhysicsWorld {
public:
    struct Collider {
        enum Type { AABB_TYPE, SPHERE_TYPE, CAPSULE_TYPE };
        
        Type type;
        union {
            AABB aabb;
            Sphere sphere;
            Capsule capsule;
        };
        
        bool isStatic = true;
        bool isTrigger = false;
        void* userData = nullptr;
        std::function<void(Collider*, Collider*)> onCollision;
        std::function<void(Collider*, Collider*)> onTriggerEnter;
        std::function<void(Collider*, Collider*)> onTriggerExit;
    };
    
    PhysicsWorld() = default;
    ~PhysicsWorld() = default;
    
    Collider* AddAABB(const AABB& aabb, bool isStatic = true, void* userData = nullptr);
    Collider* AddSphere(const Sphere& sphere, bool isStatic = true, void* userData = nullptr);
    Collider* AddCapsule(const Capsule& capsule, bool isStatic = true, void* userData = nullptr);
    
    void RemoveCollider(Collider* collider);
    void Clear();
    
    // Collision queries
    bool Raycast(const Ray& ray, RaycastHit& hit, float maxDistance = FLT_MAX);
    bool CheckCapsule(const Capsule& capsule, std::vector<Collider*>& overlaps);
    bool SweepCapsule(const Capsule& capsule, const glm::vec3& direction, float distance,
                     RaycastHit& hit, Collider** hitCollider = nullptr);
    
    // Move capsule with collision response
    glm::vec3 MoveCapsule(const Capsule& capsule, const glm::vec3& velocity,
                         int maxIterations = 3, float skinWidth = 0.01f);
    
    void Update(float deltaTime);
    
private:
    std::vector<std::unique_ptr<Collider>> m_colliders;
    std::vector<std::pair<Collider*, Collider*>> m_triggerPairs;
    
    bool TestCollision(Collider* a, Collider* b, CollisionManifold* manifold = nullptr);
};

} // namespace Physics