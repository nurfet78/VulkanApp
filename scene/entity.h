// engine/scene/entity.h
#pragma once

#include <typeindex>
#include <unordered_map>
#include <memory>
#include <vector>
#include <algorithm>
#include <cassert>

namespace Scene {

using EntityID = uint32_t;
constexpr EntityID INVALID_ENTITY = 0;

// Base component class
class Component {
public:
    virtual ~Component() = default;
    EntityID GetEntity() const { return m_entity; }
    void SetEntity(EntityID entity) { m_entity = entity; }
    
protected:
    EntityID m_entity = INVALID_ENTITY;
};

// Entity class
class Entity {
public:
    Entity() : m_id(GenerateID()) {}
    explicit Entity(EntityID id) : m_id(id) {}
    
    EntityID GetID() const { return m_id; }
    bool IsValid() const { return m_id != INVALID_ENTITY; }
    
    bool operator==(const Entity& other) const { return m_id == other.m_id; }
    bool operator!=(const Entity& other) const { return m_id != other.m_id; }
    
private:
    static EntityID GenerateID() {
        static EntityID s_nextID = 1;
        return s_nextID++;
    }
    
    EntityID m_id;
};

// Component storage
template<typename T>
class ComponentArray {
public:
    void Add(EntityID entity, T component) {
        assert(m_entityToIndex.find(entity) == m_entityToIndex.end());
        
        size_t newIndex = m_components.size();
        m_components.push_back(std::move(component));
        m_components.back().SetEntity(entity);
        
        m_entityToIndex[entity] = newIndex;
        m_indexToEntity[newIndex] = entity;
    }
    
    void Remove(EntityID entity) {
        auto it = m_entityToIndex.find(entity);
        if (it == m_entityToIndex.end()) return;
        
        size_t indexToRemove = it->second;
        size_t lastIndex = m_components.size() - 1;
        
        // Swap with last element
        if (indexToRemove != lastIndex) {
            m_components[indexToRemove] = std::move(m_components[lastIndex]);
            
            EntityID lastEntity = m_indexToEntity[lastIndex];
            m_entityToIndex[lastEntity] = indexToRemove;
            m_indexToEntity[indexToRemove] = lastEntity;
        }
        
        m_components.pop_back();
        m_entityToIndex.erase(entity);
        m_indexToEntity.erase(lastIndex);
    }
    
    T* Get(EntityID entity) {
        auto it = m_entityToIndex.find(entity);
        if (it == m_entityToIndex.end()) return nullptr;
        return &m_components[it->second];
    }
    
    const T* Get(EntityID entity) const {
        auto it = m_entityToIndex.find(entity);
        if (it == m_entityToIndex.end()) return nullptr;
        return &m_components[it->second];
    }
    
    std::vector<T>& GetAll() { return m_components; }
    const std::vector<T>& GetAll() const { return m_components; }
    
    bool Has(EntityID entity) const {
        return m_entityToIndex.find(entity) != m_entityToIndex.end();
    }

private:
    std::vector<T> m_components;
    std::unordered_map<EntityID, size_t> m_entityToIndex;
    std::unordered_map<size_t, EntityID> m_indexToEntity;
};

// ECS World
class World {
public:
    World() = default;
    ~World() = default;
    
    Entity CreateEntity() {
        Entity entity;
        m_entities.push_back(entity);
        return entity;
    }
    
    void DestroyEntity(EntityID entity) {
        // Remove all components
        for (auto& [type, removeFunc] : m_componentRemovers) {
            removeFunc(entity);
        }
        
        // Remove from entity list
        m_entities.erase(
            std::remove_if(m_entities.begin(), m_entities.end(),
                [entity](const Entity& e) { return e.GetID() == entity; }),
            m_entities.end()
        );
    }
    
    template<typename T, typename... Args>
    T* AddComponent(EntityID entity, Args&&... args) {
        auto& array = GetComponentArray<T>();
        array.Add(entity, T(std::forward<Args>(args)...));
        return array.Get(entity);
    }
    
    template<typename T>
    void RemoveComponent(EntityID entity) {
        GetComponentArray<T>().Remove(entity);
    }
    
    template<typename T>
    T* GetComponent(EntityID entity) {
        return GetComponentArray<T>().Get(entity);
    }
    
    template<typename T>
    const T* GetComponent(EntityID entity) const {
        return GetComponentArray<T>().Get(entity);
    }
    
    template<typename T>
    bool HasComponent(EntityID entity) const {
        return GetComponentArray<T>().Has(entity);
    }
    
    template<typename T>
    std::vector<T>& GetComponents() {
        return GetComponentArray<T>().GetAll();
    }
    
    template<typename T>
    const std::vector<T>& GetComponents() const {
        return GetComponentArray<T>().GetAll();
    }
    
    const std::vector<Entity>& GetEntities() const { return m_entities; }
    
    // Get entities with specific component
    template<typename T>
    std::vector<EntityID> GetEntitiesWithComponent() const {
        std::vector<EntityID> result;
        const auto& components = GetComponents<T>();
        result.reserve(components.size());
        
        for (const auto& comp : components) {
            result.push_back(comp.GetEntity());
        }
        
        return result;
    }

private:
    template<typename T>
    ComponentArray<T>& GetComponentArray() {
        std::type_index typeIndex = std::type_index(typeid(T));
        
        if (m_componentArrays.find(typeIndex) == m_componentArrays.end()) {
            m_componentArrays[typeIndex] = std::make_unique<ComponentArray<T>>();
            
            // Register remover
            m_componentRemovers[typeIndex] = [this](EntityID entity) {
                GetComponentArray<T>().Remove(entity);
            };
        }
        
        return *static_cast<ComponentArray<T>*>(m_componentArrays[typeIndex].get());
    }
    
    template<typename T>
    const ComponentArray<T>& GetComponentArray() const {
        std::type_index typeIndex = std::type_index(typeid(T));
        auto it = m_componentArrays.find(typeIndex);
        assert(it != m_componentArrays.end());
        return *static_cast<ComponentArray<T>*>(it->second.get());
    }
    
    std::vector<Entity> m_entities;
    std::unordered_map<std::type_index, std::unique_ptr<void, void(*)(void*)>> m_componentArrays;
    std::unordered_map<std::type_index, std::function<void(EntityID)>> m_componentRemovers;
};

} // namespace Scene