#pragma once
#include <memory>
#include <string>
#include <vector>

#include "Component.h"

class Entity {
private:
    std::string name;
    bool active = true;
    std::vector<std::unique_ptr<Component>> components;

public:
    explicit Entity(const std::string& entityName) : name(entityName) {}

    const std::string& GetName() const { return name; }
    bool IsActive() const { return active; }
    void SetActive(bool isActive) { active = isActive; }

    void Initialize() {
        for (auto& component : components) {
            component->Initialize();
        }
    }

    void Update(float deltaTime) {
        if (!active) return;

        for (auto& component : components) {
            component->Update(deltaTime);
        }
    }

    void Render() {
        if (!active) return;

        for (auto& component : components) {
            component->Render();
        }
    }

    template<typename T, typename... Args>
    T* AddComponent(Args&&... args) {
        static_assert(std::is_base_of<Component, T>::value, "T must derive from Component");

        // Create new component
        auto component = std::make_unique<T>(std::forward<Args>(args)...);
        T* componentPtr = component.get();
        componentPtr->SetOwner(this);
        components.push_back(std::move(component));
        return componentPtr;
    }

    template<typename T>
    T* GetComponent() {
        for (auto& component : components) {
            if (T* result = dynamic_cast<T*>(component.get())) {
                return result;
            }
        }
        return nullptr;
    }

    template<typename T>
    bool RemoveComponent() {
        for (auto it = components.begin(); it != components.end(); ++it) {
            if (dynamic_cast<T*>(it->get())) {
                components.erase(it);
                return true;
            }
        }
        return false;
    }
};