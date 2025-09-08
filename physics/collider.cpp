// engine/physics/collider.cpp
#include "collider.h"


namespace Physics {

AABB AABB::Transform(const glm::mat4& transform) const {
    AABB result;
    
    // Transform all 8 corners
    glm::vec3 corners[8] = {
        {min.x, min.y, min.z}, {max.x, min.y, min.z},
        {min.x, max.y, min.z}, {max.x, max.y, min.z},
        {min.x, min.y, max.z}, {max.x, min.y, max.z},
        {min.x, max.y, max.z}, {max.x, max.y, max.z}
    };
    
    for (const auto& corner : corners) {
        glm::vec4 transformed = transform * glm::vec4(corner, 1.0f);
        result.Expand(glm::vec3(transformed));
    }
    
    return result;
}

bool Sphere::Intersects(const AABB& aabb) const {
    glm::vec3 closest = glm::clamp(center, aabb.min, aabb.max);
    return glm::distance(center, closest) <= radius;
}

bool Capsule::Intersects(const AABB& aabb) const {
    // Find closest point on capsule line segment to AABB
    glm::vec3 aabbCenter = aabb.GetCenter();
    glm::vec3 closest = GetClosestPoint(aabbCenter);
    
    // Check if sphere at closest point intersects AABB
    Sphere sphere(closest, radius);
    return sphere.Intersects(aabb);
}

bool Capsule::Intersects(const Sphere& sphere) const {
    glm::vec3 closest = GetClosestPoint(sphere.center);
    return glm::distance(closest, sphere.center) <= (radius + sphere.radius);
}

bool Capsule::Intersects(const Capsule& other) const {
    // Find closest points between two line segments
    glm::vec3 axis1 = tip - base;
    glm::vec3 axis2 = other.tip - other.base;
    glm::vec3 r = base - other.base;
    
    float a = glm::dot(axis1, axis1);
    float e = glm::dot(axis2, axis2);
    float f = glm::dot(axis2, r);
    
    float s, t;
    
    if (a <= FLT_EPSILON && e <= FLT_EPSILON) {
        s = t = 0.0f;
    } else if (a <= FLT_EPSILON) {
        s = 0.0f;
        t = glm::clamp(f / e, 0.0f, 1.0f);
    } else if (e <= FLT_EPSILON) {
        t = 0.0f;
        s = glm::clamp(-glm::dot(axis1, r) / a, 0.0f, 1.0f);
    } else {
        float b = glm::dot(axis1, axis2);
        float c = glm::dot(axis1, r);
        float denom = a * e - b * b;
        
        if (denom != 0.0f) {
            s = glm::clamp((b * f - c * e) / denom, 0.0f, 1.0f);
        } else {
            s = 0.0f;
        }
        
        t = (b * s + f) / e;
        
        if (t < 0.0f) {
            t = 0.0f;
            s = glm::clamp(-c / a, 0.0f, 1.0f);
        } else if (t > 1.0f) {
            t = 1.0f;
            s = glm::clamp((b - c) / a, 0.0f, 1.0f);
        }
    }
    
    glm::vec3 p1 = base + axis1 * s;
    glm::vec3 p2 = other.base + axis2 * t;
    
    return glm::distance(p1, p2) <= (radius + other.radius);
}

glm::vec3 Capsule::GetClosestPoint(const glm::vec3& point) const {
    glm::vec3 axis = tip - base;
    float t = glm::dot(point - base, axis) / glm::dot(axis, axis);
    t = glm::clamp(t, 0.0f, 1.0f);
    return base + axis * t;
}

bool Ray::Intersects(const AABB& aabb, float& tMin, float& tMax) const {
    glm::vec3 invDir = 1.0f / direction;
    glm::vec3 t0 = (aabb.min - origin) * invDir;
    glm::vec3 t1 = (aabb.max - origin) * invDir;
    
    glm::vec3 tSmall = glm::min(t0, t1);
    glm::vec3 tBig = glm::max(t0, t1);
    
    tMin = glm::max(tSmall.x, glm::max(tSmall.y, tSmall.z));
    tMax = glm::min(tBig.x, glm::min(tBig.y, tBig.z));
    
    return tMax >= tMin && tMax >= 0.0f;
}

bool Ray::Intersects(const Sphere& sphere, float& t) const {
    glm::vec3 oc = origin - sphere.center;
    float b = glm::dot(oc, direction);
    float c = glm::dot(oc, oc) - sphere.radius * sphere.radius;
    float discriminant = b * b - c;
    
    if (discriminant < 0.0f) {
        return false;
    }
    
    float sqrtD = std::sqrt(discriminant);
    t = -b - sqrtD;
    
    if (t < 0.0f) {
        t = -b + sqrtD;
        if (t < 0.0f) {
            return false;
        }
    }
    
    return true;
}

bool Ray::Intersects(const Plane& plane, float& t) const {
    float denom = glm::dot(plane.normal, direction);
    
    if (std::abs(denom) < FLT_EPSILON) {
        return false;
    }
    
    t = (plane.distance - glm::dot(plane.normal, origin)) / denom;
    return t >= 0.0f;
}

namespace Collision {

bool TestAABBvsAABB(const AABB& a, const AABB& b, CollisionManifold* manifold) {
    if (!a.Intersects(b)) {
        return false;
    }
    
    if (manifold) {
        // Calculate penetration and normal
        glm::vec3 overlap = glm::min(a.max, b.max) - glm::max(a.min, b.min);
        
        float minOverlap = overlap.x;
        manifold->normal = glm::vec3(1, 0, 0);
        
        if (overlap.y < minOverlap) {
            minOverlap = overlap.y;
            manifold->normal = glm::vec3(0, 1, 0);
        }
        
        if (overlap.z < minOverlap) {
            minOverlap = overlap.z;
            manifold->normal = glm::vec3(0, 0, 1);
        }
        
        manifold->penetration = minOverlap;
        
        // Determine normal direction
        glm::vec3 centerDiff = a.GetCenter() - b.GetCenter();
        if (glm::dot(manifold->normal, centerDiff) < 0) {
            manifold->normal = -manifold->normal;
        }
        
        // Add contact points (simplified - just using center)
        manifold->contactPoints.push_back(a.GetCenter());
    }
    
    return true;
}

bool TestCapsuleVsAABB(const Capsule& capsule, const AABB& aabb, CollisionManifold* manifold) {
    if (!capsule.Intersects(aabb)) {
        return false;
    }
    
    if (manifold) {
        // Find closest point on AABB to capsule center
        glm::vec3 capsuleCenter = capsule.GetCenter();
        glm::vec3 closest = glm::clamp(capsuleCenter, aabb.min, aabb.max);
        
        // Calculate normal and penetration
        glm::vec3 diff = capsuleCenter - closest;
        float distance = glm::length(diff);
        
        if (distance > FLT_EPSILON) {
            manifold->normal = diff / distance;
            manifold->penetration = capsule.radius - distance;
        } else {
            // Capsule center is inside AABB
            glm::vec3 extents = aabb.GetExtents();
            glm::vec3 localPoint = capsuleCenter - aabb.GetCenter();
            
            float minDist = FLT_MAX;
            glm::vec3 bestNormal;
            
            // Check all 6 faces
            for (int i = 0; i < 3; i++) {
                float dist = extents[i] - std::abs(localPoint[i]);
                if (dist < minDist) {
                    minDist = dist;
                    bestNormal = glm::vec3(0);
                    bestNormal[i] = localPoint[i] > 0 ? 1.0f : -1.0f;
                }
            }
            
            manifold->normal = bestNormal;
            manifold->penetration = minDist + capsule.radius;
        }
        
        manifold->contactPoints.push_back(closest);
    }
    
    return true;
}

bool SweepCapsuleVsAABB(const Capsule& capsule, const glm::vec3& velocity,
                       const AABB& aabb, float& t, glm::vec3& normal) {
    // Expand AABB by capsule radius
    AABB expanded;
    expanded.min = aabb.min - glm::vec3(capsule.radius);
    expanded.max = aabb.max + glm::vec3(capsule.radius);
    
    // Ray from capsule center
    Ray ray(capsule.GetCenter(), glm::normalize(velocity));
    float tMin, tMax;
    
    if (!ray.Intersects(expanded, tMin, tMax)) {
        return false;
    }
    
    float velocityLength = glm::length(velocity);
    if (tMin > velocityLength) {
        return false;
    }
    
    t = tMin / velocityLength;
    
    // Calculate normal at hit point
    glm::vec3 hitPoint = ray.GetPoint(tMin);
    glm::vec3 aabbCenter = aabb.GetCenter();
    glm::vec3 diff = hitPoint - aabbCenter;
    
    glm::vec3 extents = aabb.GetExtents() + glm::vec3(capsule.radius);
    glm::vec3 localPoint = diff / extents;
    
    float maxAxis = 0.0f;
    int maxIndex = 0;
    
    for (int i = 0; i < 3; i++) {
        float absAxis = std::abs(localPoint[i]);
        if (absAxis > maxAxis) {
            maxAxis = absAxis;
            maxIndex = i;
        }
    }
    
    normal = glm::vec3(0);
    normal[maxIndex] = localPoint[maxIndex] > 0 ? 1.0f : -1.0f;
    
    return true;
}

} // namespace Collision

// PhysicsWorld implementation
PhysicsWorld::Collider* PhysicsWorld::AddAABB(const AABB& aabb, bool isStatic, void* userData) {
    auto collider = std::make_unique<Collider>();
    collider->type = Collider::AABB_TYPE;
    collider->aabb = aabb;
    collider->isStatic = isStatic;
    collider->userData = userData;
    
    Collider* ptr = collider.get();
    m_colliders.push_back(std::move(collider));
    return ptr;
}

PhysicsWorld::Collider* PhysicsWorld::AddCapsule(const Capsule& capsule, bool isStatic, void* userData) {
    auto collider = std::make_unique<Collider>();
    collider->type = Collider::CAPSULE_TYPE;
    collider->capsule = capsule;
    collider->isStatic = isStatic;
    collider->userData = userData;
    
    Collider* ptr = collider.get();
    m_colliders.push_back(std::move(collider));
    return ptr;
}

void PhysicsWorld::RemoveCollider(Collider* collider) {
    m_colliders.erase(
        std::remove_if(m_colliders.begin(), m_colliders.end(),
            [collider](const std::unique_ptr<Collider>& c) {
                return c.get() == collider;
            }),
        m_colliders.end()
    );
}

void PhysicsWorld::Clear() {
    m_colliders.clear();
    m_triggerPairs.clear();
}

bool PhysicsWorld::Raycast(const Ray& ray, RaycastHit& hit, float maxDistance) {
    hit.distance = maxDistance;
    bool hasHit = false;
    
    for (const auto& collider : m_colliders) {
        if (collider->isTrigger) continue;
        
        float t;
        bool intersects = false;
        
        switch (collider->type) {
            case Collider::AABB_TYPE: {
                float tMin, tMax;
                intersects = ray.Intersects(collider->aabb, tMin, tMax);
                t = tMin;
                break;
            }
            case Collider::SPHERE_TYPE:
                intersects = ray.Intersects(collider->sphere, t);
                break;
            default:
                break;
        }
        
        if (intersects && t < hit.distance) {
            hit.distance = t;
            hit.point = ray.GetPoint(t);
            hit.userData = collider->userData;
            hasHit = true;
            
            // Calculate normal (simplified)
            if (collider->type == Collider::AABB_TYPE) {
                glm::vec3 center = collider->aabb.GetCenter();
                glm::vec3 diff = hit.point - center;
                glm::vec3 absD = glm::abs(diff);
                
                if (absD.x > absD.y && absD.x > absD.z) {
                    hit.normal = glm::vec3(glm::sign(diff.x), 0, 0);
                } else if (absD.y > absD.z) {
                    hit.normal = glm::vec3(0, glm::sign(diff.y), 0);
                } else {
                    hit.normal = glm::vec3(0, 0, glm::sign(diff.z));
                }
            }
        }
    }
    
    return hasHit;
}

bool PhysicsWorld::CheckCapsule(const Capsule& capsule, std::vector<Collider*>& overlaps) {
    overlaps.clear();
    
    for (const auto& collider : m_colliders) {
        bool intersects = false;
        
        switch (collider->type) {
            case Collider::AABB_TYPE:
                intersects = capsule.Intersects(collider->aabb);
                break;
            case Collider::CAPSULE_TYPE:
                intersects = capsule.Intersects(collider->capsule);
                break;
            default:
                break;
        }
        
        if (intersects) {
            overlaps.push_back(collider.get());
        }
    }
    
    return !overlaps.empty();
}

glm::vec3 PhysicsWorld::MoveCapsule(const Capsule& capsule, const glm::vec3& velocity,
                                    int maxIterations, float skinWidth) {
    glm::vec3 position = capsule.base;
    glm::vec3 remainingVelocity = velocity;
    
    for (int iteration = 0; iteration < maxIterations && glm::length(remainingVelocity) > FLT_EPSILON; iteration++) {
        // Find closest collision
        float closestT = 1.0f;
        glm::vec3 closestNormal;
        Collider* closestCollider = nullptr;
        
        Capsule currentCapsule = capsule;
        currentCapsule.base = position;
        currentCapsule.tip = position + (capsule.tip - capsule.base);
        
        for (const auto& collider : m_colliders) {
            if (collider->isTrigger || !collider->isStatic) continue;
            
            float t;
            glm::vec3 normal;
            bool hit = false;
            
            if (collider->type == Collider::AABB_TYPE) {
                hit = Collision::SweepCapsuleVsAABB(currentCapsule, remainingVelocity,
                                                   collider->aabb, t, normal);
            }
            
            if (hit && t < closestT) {
                closestT = t;
                closestNormal = normal;
                closestCollider = collider.get();
            }
        }
        
        // Move to collision point
        position += remainingVelocity * (closestT - skinWidth / glm::length(remainingVelocity));
        
        if (closestCollider) {
            // Collision response - slide along surface
            float velocityDotNormal = glm::dot(remainingVelocity, closestNormal);
            remainingVelocity -= closestNormal * velocityDotNormal;
            remainingVelocity *= (1.0f - closestT);
            
            // Callback
            if (closestCollider->onCollision) {
                closestCollider->onCollision(closestCollider, nullptr);
            }
        } else {
            break;
        }
    }
    
    return position;
}

void PhysicsWorld::Update(float deltaTime) {
    // Check for trigger enter/exit events
    std::vector<std::pair<Collider*, Collider*>> currentPairs;
    
    for (size_t i = 0; i < m_colliders.size(); i++) {
        if (!m_colliders[i]->isTrigger) continue;
        
        for (size_t j = 0; j < m_colliders.size(); j++) {
            if (i == j || m_colliders[j]->isStatic) continue;
            
            if (TestCollision(m_colliders[i].get(), m_colliders[j].get())) {
                currentPairs.push_back({m_colliders[i].get(), m_colliders[j].get()});
                
                // Check if this is a new pair
                auto it = std::find(m_triggerPairs.begin(), m_triggerPairs.end(),
                                  std::make_pair(m_colliders[i].get(), m_colliders[j].get()));
                
                if (it == m_triggerPairs.end() && m_colliders[i]->onTriggerEnter) {
                    m_colliders[i]->onTriggerEnter(m_colliders[i].get(), m_colliders[j].get());
                }
            }
        }
    }
    
    // Check for exit events
    for (const auto& oldPair : m_triggerPairs) {
        auto it = std::find(currentPairs.begin(), currentPairs.end(), oldPair);
        if (it == currentPairs.end() && oldPair.first->onTriggerExit) {
            oldPair.first->onTriggerExit(oldPair.first, oldPair.second);
        }
    }
    
    m_triggerPairs = currentPairs;
}

bool PhysicsWorld::TestCollision(Collider* a, Collider* b, CollisionManifold* manifold) {
    if (a->type == Collider::AABB_TYPE && b->type == Collider::AABB_TYPE) {
        return Collision::TestAABBvsAABB(a->aabb, b->aabb, manifold);
    } else if (a->type == Collider::CAPSULE_TYPE && b->type == Collider::AABB_TYPE) {
        return Collision::TestCapsuleVsAABB(a->capsule, b->aabb, manifold);
    }
    // Add more collision type combinations as needed
    return false;
}

} // namespace Physics