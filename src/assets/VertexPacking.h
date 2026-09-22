#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/packing.hpp>

namespace vpack
{
    inline glm::vec2 signNotZero(glm::vec2 v)
    {
        return { v.x >= 0.0f ? 1.0f : -1.0f, v.y >= 0.0f ? 1.0f : -1.0f };
    }

    //Octahedral encoding
    inline glm::vec2 octEncode(glm::vec3 n)
    {
        n /= std::abs(n.x) + std::abs(n.y) + std::abs(n.z);
        glm::vec2 p(n.x, n.y);
        if (n.z < 0.0f) {
            p = (1.0f - glm::abs(glm::vec2(p.y, p.x))) * signNotZero(p);
        }
        return p;
    }

    // Mirror of octDecode in scene.glsl
    inline glm::vec3 octDecode(glm::vec2 e)
    {
        glm::vec3 n(e.x, e.y, 1.0f - std::abs(e.x) - std::abs(e.y));
        const float t = std::max(-n.z, 0.0f);
        n.x += n.x >= 0.0f ? -t : t;
        n.y += n.y >= 0.0f ? -t : t;
        return glm::normalize(n);
    }

    inline float linearToSrgb(float c)
    {
        c = glm::clamp(c, 0.0f, 1.0f);
        return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
    }

    inline float srgbToLinear(float c)
    {
        return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
    }

    inline uint32_t packNormal(glm::vec3 n) { return glm::packSnorm2x16(octEncode(n)); }

    // Bit 0 carries the bitangent sign: (set = -1)
    inline uint32_t packTangent(glm::vec4 t)
    {
        return (glm::packSnorm2x16(octEncode(glm::vec3(t))) & ~1u) | (t.w < 0.0f ? 1u : 0u);
    }

    inline uint32_t packColor(glm::vec4 c)
    {
        return glm::packUnorm4x8(glm::vec4(linearToSrgb(c.r), linearToSrgb(c.g), linearToSrgb(c.b),
                                           glm::clamp(c.a, 0.0f, 1.0f)));
    }

    inline glm::vec3 unpackNormal(uint32_t p)  { return octDecode(glm::unpackSnorm2x16(p)); }
    inline glm::vec4 unpackTangent(uint32_t p) { return glm::vec4(unpackNormal(p), (p & 1u) ? -1.0f : 1.0f); }
    inline glm::vec4 unpackColor(uint32_t p)
    {
        const glm::vec4 c = glm::unpackUnorm4x8(p);
        return { srgbToLinear(c.r), srgbToLinear(c.g), srgbToLinear(c.b), c.a };
    }
}