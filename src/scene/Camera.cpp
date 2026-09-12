#include "Camera.h"

#include <iostream>
#include <ostream>

void Camera::handleInput(const SDL_Event& e, float deltaTime)
{
    if (e.type == SDL_EVENT_KEY_DOWN) {
        if (e.key.key == SDLK_W) velocity.z = -1.0f;
        if (e.key.key == SDLK_S) velocity.z =  1.0f;
        if (e.key.key == SDLK_A) velocity.x = -1.0f;
        if (e.key.key == SDLK_D) velocity.x =  1.0f;
    }

    if (e.type == SDL_EVENT_KEY_UP) {
        if (e.key.key == SDLK_W) velocity.z = 0.0f;
        if (e.key.key == SDLK_S) velocity.z = 0.0f;
        if (e.key.key == SDLK_A) velocity.x = 0.0f;
        if (e.key.key == SDLK_D) velocity.x = 0.0f;
    }

    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        if (e.button.button == SDL_BUTTON_RIGHT) {
            rightMouseHeld = true;
        }
    }

    if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        if (e.button.button == SDL_BUTTON_RIGHT) {
            rightMouseHeld = false;
        }
    }

    if (e.type == SDL_EVENT_MOUSE_MOTION && rightMouseHeld) {
        yaw   += static_cast<float>(e.motion.xrel) / 200.0f;
        pitch -= static_cast<float>(e.motion.yrel) / 200.0f;
    }
}


void Camera::Update(float deltaTime)
{
    glm::vec3 movement{0.0f};

    if (glm::length2(velocity) > 0.0f) movement = glm::normalize(velocity);

    glm::mat4 rotation = getRotationMatrix();

    position += glm::vec3(
        rotation * glm::vec4(
            movement * m_speed * deltaTime,
            0.0f
        )
    );
}




glm::mat4 Camera::getViewMatrix() const
{

    glm::mat4 cameraTranslation = glm::translate(glm::mat4(1.f), position);
    glm::mat4 cameraRotation = getRotationMatrix();
    return glm::inverse(cameraTranslation * cameraRotation);
}

glm::mat4 Camera::getRotationMatrix() const

{

    glm::quat pitchRotation = glm::angleAxis(pitch, glm::vec3 { 1.f, 0.f, 0.f });
    glm::quat yawRotation = glm::angleAxis(yaw, glm::vec3 { 0.f, -1.f, 0.f });

    return glm::toMat4(yawRotation) * glm::toMat4(pitchRotation);
}

glm::mat4 Camera::viewProjection(float aspectRatio) const
{
    const glm::mat4 view = getViewMatrix();

    const glm::mat4 proj =
        glm::perspectiveRH(
            glm::radians(fovDegrees),
            aspectRatio,
            nearPlane,
            farPlane
        );

    return proj * view;
}
