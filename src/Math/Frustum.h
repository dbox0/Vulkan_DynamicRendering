#pragma once

#include <glm/glm.hpp>
#include <array>


struct Frustum {
    enum Plane { Left, Right, Bottom, Top, Near, Far, Count};
    std::array<glm::vec4,Count> planes{};

    static Frustum fromViewProj(const glm::mat4 &viewProj);

    bool intersectsAABB(const glm::vec3 &lo, const glm::vec3 &hi) const;
    bool intersectsSphere(const glm::vec3 &center, float radius) const;

    static void cornersWorld(const glm::mat4 &viewProj, glm::vec3 (&out)[8]);
};



