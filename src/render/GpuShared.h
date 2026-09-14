#pragma once
// Every struct in this file has a twin in the shaders (scalar block layout).
#include <glm/glm.hpp>
#include <cstddef>
#include <cstdint>

#if defined(GLM_FORCE_DEFAULT_ALIGNED_GENTYPES)
#error "GpuShared.h assumes tightly packed glm types (scalar block layout)"
#endif

struct Vertex
{
    glm::vec3 position{ 0.0f };
    glm::vec3 normal{ 0.0f, 0.0f, 1.0f };
    glm::vec4 tangent{ 0.0f };   // xyz = tangent, w = bitangent sign (+1/-1).

    // w == 0 means "mesh supplied no tangents";
    // the shader then derives a frame itself.
    glm::vec2 uv{ 0.0f };        // TEXCOORD_0
    glm::vec4 color{ 1.0f };     // COLOR_0 -- multiplies base colour (glTF spec)
};

static_assert(sizeof(Vertex) == 64);
static_assert(offsetof(Vertex, normal)  == 12);
static_assert(offsetof(Vertex, tangent) == 24);
static_assert(offsetof(Vertex, uv)      == 40);
static_assert(offsetof(Vertex, color)   == 48);


enum MaterialFlags : uint32_t
{
    MaterialFlag_AlphaMask   = 1u << 0,
    MaterialFlag_AlphaBlend  = 1u << 1,
    MaterialFlag_DoubleSided = 1u << 2,
    MaterialFlag_NormalMap   = 1u << 3,
};

// Material - GPU side only
// Editor facing type is Material (assets/Material.h)
// ResourceStore converts one into the other.
// ----------------------------------------------------------------------------

struct GpuMaterial
{
    glm::vec4 baseColorFactor{ 1.0f };
    glm::vec3 emissiveFactor{ 0.0f };   // already multiplied by emissive strength
    float     metallicFactor    = 1.0f;
    float     roughnessFactor   = 1.0f;
    float     normalScale       = 1.0f;
    float     occlusionStrength = 1.0f;
    float     alphaCutoff       = 0.5f;

    // 0-based descriptor-array slots. Never "none": ResourceStore substitutes
    // the white default for missing textures. Slot 0 IS the white texture,
    // so a zero-initialised GpuMaterial is always safe to sample.
    uint32_t baseColorTex         = 0;
    uint32_t metallicRoughnessTex = 0;
    uint32_t normalTex            = 0;
    uint32_t occlusionTex         = 0;
    uint32_t emissiveTex          = 0;

    uint32_t flags = 0;                 // MaterialFlags
};
static_assert(sizeof(GpuMaterial) == 72);
static_assert(offsetof(GpuMaterial, emissiveFactor) == 16);
static_assert(offsetof(GpuMaterial, alphaCutoff)    == 44);
static_assert(offsetof(GpuMaterial, baseColorTex)   == 48);
static_assert(offsetof(GpuMaterial, flags)          == 68);


// RenderItem -- one per indirect draw, indexed by gl_InstanceIndex.
// ----------------------------------------------------------------------------
struct RenderItem
{
    glm::mat4 worldMatrix{ 1.0f };
    glm::mat3 normalMatrix{ 1.0f };
    uint32_t  materialIndex = 0;        // 0-based; index 0 is the default material
};
static_assert(sizeof(RenderItem) == 104);
static_assert(offsetof(RenderItem, normalMatrix)  == 64);
static_assert(offsetof(RenderItem, materialIndex) == 100);


// FrameData -> per frame-in-flight, host-visible, read through BDA.
// This is where lights will go later (a light list address, counts, etc).
// ----------------------------------------------------------------------------
struct FrameData
{
    glm::mat4 viewProj{ 1.0f };
    glm::vec3 cameraPosition{ 0.0f };
    float     exposure = 1.0f;
    glm::vec3 sunDirection{ 0.0f, -1.0f, -1.0f }; // direction light TRAVELS, normalised on the CPU
    float     sunIntensity = 3.0f;
    glm::vec3 sunColor{ 1.0f };
    float     ambientIntensity = 1.0f;
    glm::vec3 skyColor{ 0.15f, 0.18f, 0.25f };
    glm::vec3 groundColor{ 0.05f, 0.03f, 0.02f };

    uint  envTex;        // 0 = no environment, fall back to the hemisphere
    float envIntensity;
    float envMaxLod;     // mipLevels - 1 of the environment image
};

static_assert(sizeof(FrameData) == 148);
static_assert(offsetof(FrameData, cameraPosition) == 64);
static_assert(offsetof(FrameData, sunDirection)   == 80);
static_assert(offsetof(FrameData, skyColor)       == 112);

// Push constants. 32 bytes
// ----------------------------------------------------------------------------
struct FrameConstants
{
    uint64_t vertexBufferAddress     = 0;
    uint64_t materialBufferAddress   = 0;
    uint64_t renderItemBufferAddress = 0;
    uint64_t frameDataAddress        = 0;
};

static_assert(sizeof(FrameConstants) == 32);
