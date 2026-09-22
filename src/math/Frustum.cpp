#include "Frustum.h"


namespace {
    glm::vec4 row(const glm::mat4 &m, int i) {
        return { m[0][i], m[1][i], m[2][i], m[3][i] };
    }
    glm::vec4 normalized(const glm::vec4 &p) {
        const float len = glm::length(glm::vec3(p));
        return len > 0.0f ? p / len : p;
    }
}

Frustum Frustum::fromViewProj(const glm::mat4 &viewProj)
{
    const glm::vec4 r0 = row(viewProj, 0);
    const glm::vec4 r1 = row(viewProj, 1);
    const glm::vec4 r2 = row(viewProj, 2);
    const glm::vec4 r3 = row(viewProj, 3);

    Frustum f;
    f.planes[Left]   = normalized(r3 + r0);
    f.planes[Right]  = normalized(r3 - r0);
    f.planes[Bottom] = normalized(r3 + r1);
    f.planes[Top]    = normalized(r3 - r1);

    // Vulkan clip depth is 0 <= z <= w, so these two are row2 and row3 - row2
    // regardless of reverse-Z. Reverse-Z only swaps which one you'd *call* near.
    f.planes[Near]   = normalized(r2);
    f.planes[Far]    = normalized(r3 - r2);
    return f;
}

bool Frustum::intersectsAABB(const glm::vec3 &lo, const glm::vec3 &hi) const
{
    for (const glm::vec4 &p : planes) {
        const glm::vec3 n{ p };

        // Positive vertex: the corner furthest along the normal. If even that
        // is outside, the whole box is.
        const glm::vec3 v = glm::mix(lo, hi, glm::greaterThan(n, glm::vec3(0.0f)));
        if (glm::dot(n, v) + p.w < 0.0f) {
            return false;
        }
    }
    return true;
}

bool Frustum::intersectsSphere(const glm::vec3 &center, float radius) const
{
    for (const glm::vec4 &p : planes) {
        if (glm::dot(glm::vec3(p), center) + p.w < -radius) {
            return false;
        }
    }
    return true;
}

void Frustum::cornersWorld(const glm::mat4 &viewProj, glm::vec3 (&out)[8])
{
    const glm::mat4 inv = glm::inverse(viewProj);
    int i = 0;
    for (float z : { 0.0f, 1.0f }) {
        for (float y : { -1.0f, 1.0f }) {
            for (float x : { -1.0f, 1.0f }) {
                const glm::vec4 p = inv * glm::vec4(x, y, z, 1.0f);
                out[i++] = glm::vec3(p) / p.w;
            }
        }
    }
}