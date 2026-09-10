#include "Camera.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

void Camera::handleInput(const bool *keys, float deltaTime)
{
    constexpr float epsilon    = 0.01f;
    const     float pitchLimit = glm::half_pi<float>() - epsilon;

    if (keys[SDL_SCANCODE_W]) {
        m_distance = std::max(m_distance - m_speed * deltaTime, epsilon);
    }
    if (keys[SDL_SCANCODE_S]) {
        m_distance += m_speed * deltaTime;
    }
    if (keys[SDL_SCANCODE_A]) {
        m_yaw += m_speed * deltaTime;
    }
    if (keys[SDL_SCANCODE_D]) {
        m_yaw -= m_speed * deltaTime;
    }
    if (keys[SDL_SCANCODE_UP]) {
        m_pitch = std::clamp(m_pitch + m_speed * deltaTime, -pitchLimit, pitchLimit);
    }
    if (keys[SDL_SCANCODE_DOWN]) {
        m_pitch = std::clamp(m_pitch - m_speed * deltaTime, -pitchLimit, pitchLimit);
    }
}

glm::vec3 Camera::position() const
{
    return glm::vec3(std::cos(m_yaw) * std::cos(m_pitch),
                     std::sin(m_pitch),
                     std::sin(m_yaw) * std::cos(m_pitch)) * m_distance;
}

glm::mat4 Camera::viewProjection(float aspectRatio) const
{
    const glm::mat4 view = glm::lookAtRH(position(), glm::vec3(0.0f), glm::vec3(0, 1, 0));
    const glm::mat4 proj = glm::perspectiveRH(glm::radians(fovDegrees), aspectRatio, nearPlane, farPlane);
    return proj * view;
}