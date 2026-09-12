#pragma once

// Material.h - Authoring Material
// This is what the glTF loader produces and what the editor inspector edits.
// It is free to hold strings, enums and 1-based IDs because it never touches
// GPU memory directly. ResourceStore::toGpu() turns it into a GpuMaterial

#include <glm/glm.hpp>
#include <cstdint>
#include <string>

enum class AlphaMode : uint8_t { Opaque, Mask, Blend };

struct Material
{
    std::string name;

    glm::vec4 baseColorFactor{ 1.0f };
    glm::vec3 emissiveFactor{ 0.0f };
    float     emissiveStrength  = 1.0f;   // KHR_materials_emissive_strength
    float     metallicFactor    = 1.0f;
    float     roughnessFactor   = 1.0f;
    float     normalScale       = 1.0f;
    float     occlusionStrength = 1.0f;
    float     alphaCutoff       = 0.5f;

    // 1-based ResourceStore texture IDs, 0 = none (-> white default).
    uint32_t baseColorTexture         = 0;   // sRGB
    uint32_t metallicRoughnessTexture = 0;   // linear: G = roughness, B = metallic
    uint32_t normalTexture            = 0;   // linear, tangent space
    uint32_t occlusionTexture         = 0;   // linear: R
    uint32_t emissiveTexture          = 0;   // sRGB

    AlphaMode alphaMode   = AlphaMode::Opaque;
    bool      doubleSided = false;
};
