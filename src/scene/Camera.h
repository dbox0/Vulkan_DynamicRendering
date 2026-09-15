#pragma once
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>
#include <glm/gtx/quaternion.hpp>

class Camera
{
private:
    float fovDegrees = 75.0f;
    float aspectRatio = 16.0f / 9.0f;
    float nearPlane  = 0.01f;
    float farPlane   = 1000.0f;

    glm::mat4 viewMatrix = glm::mat4(1.0f);
    glm::mat4 projectionMatrix = glm::mat4(1.0f);
    bool projectionDirty = true;
    bool rightMouseHeld = false;
    bool middleMouseHeld = false;

public:
    void handleInput(const SDL_Event&e, float deltaTime);


    glm::vec3 position = glm::vec3(5.0f, 5.0f, 0.0f);
    glm::vec3 velocity = glm::vec3(0.0f, 0.0f, 0.0f);

    float yaw      = 1.5707963f;   // glm::radians(90)
    float pitch    = 0.0f;


    glm::mat4 viewProjection(float aspectRatio) const;

    // Split out for ImGuizmo, which wants view and projection separately.
    // REVERSE Z: near maps to 1, far to 0.
    glm::mat4 projection(float aspectRatio) const;

    // The 8 world-space corners of the slice of this camera's frustum between
    // nearDist and farDist. Shadow fitting is the only caller.
    void frustumCornersWorld(float aspectRatio, float nearDist, float farDist,
                             glm::vec3 (&out)[8]) const;

    float fov()       const { return fovDegrees; }
    float nearClip()  const { return nearPlane; }
    float farClip()   const { return farPlane; }
    glm::mat4 getRotationMatrix() const;
    glm::mat4 getViewMatrix() const;

    void Update(float deltaTime);

private:
    float m_speed    = 6.0f;
};
