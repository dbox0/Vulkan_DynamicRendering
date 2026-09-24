#pragma once
// Every struct in this file has a twin in the shaders (scalar block layout).
#include <glm/glm.hpp>
#include <cstddef>
#include <cstdint>

#if defined(GLM_FORCE_DEFAULT_ALIGNED_GENTYPES)
#error "GpuShared.h assumes tightly packed glm types (scalar block layout)"
#endif

struct GpuTable {
    uint64_t positions = 0, attributes = 0, colors = 0;
    uint64_t materials = 0, renderItems = 0, debugLines = 0;
    uint64_t reserved[2]{};
};
static_assert(sizeof(GpuTable) == 64);
static_assert(offsetof(GpuTable, attributes)  == 8);
static_assert(offsetof(GpuTable, colors)      == 16);
static_assert(offsetof(GpuTable, materials)   == 24);
static_assert(offsetof(GpuTable, renderItems) == 32);
static_assert(offsetof(GpuTable, debugLines)  == 40);




constexpr uint32_t MaxShadowCascades = 4;
struct GpuCascade {
    glm::mat4 viewProj{ 1.0f };
    float     normalBias = 0.0f;
    float     depthBias  = 0.0f;
    float     pad[2]{};
};
static_assert(sizeof(GpuCascade) == 80);

// FrameData -> per frame-in-flight, host-visible, read through BDA.
struct FrameData
{
    GpuTable   table;
    glm::mat4  viewProj{ 1.0f };
    GpuCascade cascades[MaxShadowCascades]{};

    glm::vec3 cameraPosition{ 0.0f };
    float     exposure = 1.0f;
    glm::vec3 sunDirection{ 0.0f, -1.0f, -1.0f };
    float     sunIntensity = 3.0f;
    glm::vec3 sunColor{ 1.0f };
    float     ambientIntensity = 1.0f;
    glm::vec3 skyColor{ 0.15f, 0.18f, 0.25f };
    glm::vec3 groundColor{ 0.05f, 0.03f, 0.02f };

    uint  envIrradianceTex;
    uint  envPrefilterTex;
    float envIntensity;
    float envMaxLod;

    float shadowTexelSize = 0.0f;   // 1 / per-cascade resolution
    uint  shadowEnabled   = 0;
    uint  cascadeCount    = 0;
    uint  shadowDebug     = 0;      // 1 = tint by cascade (step 11)
};
static_assert(offsetof(FrameData, viewProj)        == 64);
static_assert(offsetof(FrameData, cascades)        == 128);
static_assert(offsetof(FrameData, cameraPosition)  == 448);
static_assert(offsetof(FrameData, sunDirection)    == 464);
static_assert(offsetof(FrameData, skyColor)        == 496);
static_assert(offsetof(FrameData, shadowTexelSize) == 536);
static_assert(sizeof(FrameData) == 552);


struct PushConstants {
    uint64_t frameData = 0;
    uint32_t viewIndex = 0;
    uint32_t pad       = 0;
};
static_assert(sizeof(PushConstants) == 16);


struct PackedAttributes {
    uint32_t normal = 0;
    uint32_t tangent = 0;
    glm::vec2 uv {0.0f};
};
static_assert(sizeof(PackedAttributes) == 16);
static_assert(offsetof(PackedAttributes, uv) == 8);

// One end of a debug line. Written straight into a host-visible buffer and
// read through BDA
struct DebugVertex
{
    glm::vec3 position{ 0.0f };
    glm::vec3 color{ 1.0f };
};
static_assert(sizeof(DebugVertex) == 24);
static_assert(offsetof(DebugVertex, color) == 12);


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
    uint32_t  materialIndex = 0;        // 0-based; index 0 is the default material
};
static_assert(sizeof(RenderItem) == 68);
static_assert(offsetof(RenderItem, materialIndex) == 64);


