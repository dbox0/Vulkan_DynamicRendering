#pragma once
#include <glm/glm.hpp>

struct Vertex {
    glm::vec3 position;
    glm::vec4 color;
    glm::vec3 normal;
    glm::vec2 uv1;
    glm::vec2 uv2;
};
