#pragma once
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

class Camera
{
public:
    void handleInput(const bool *keyboardState, float deltaTime);

    glm::vec3 position() const;
    glm::mat4 viewProjection(float aspectRatio) const;

    float fovDegrees = 75.0f;
    float nearPlane  = 0.01f;
    float farPlane   = 1000.0f;

private:
    float m_distance = 3.0f;
    float m_yaw      = 1.5707963f;   // glm::radians(90)
    float m_pitch    = 0.0f;
    float m_speed    = 1.0f;
};