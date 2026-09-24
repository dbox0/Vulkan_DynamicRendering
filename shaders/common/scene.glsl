#ifndef COMMON_SCENE_GLSL
#define COMMON_SCENE_GLSL

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

const uint MAT_ALPHA_MASK   = 1u << 0;
const uint MAT_ALPHA_BLEND  = 1u << 1;
const uint MAT_DOUBLE_SIDED = 1u << 2;
const uint MAT_NORMAL_MAP   = 1u << 3;

struct Vertex           { vec3 position; vec3 normal; vec4 tangent; vec2 uv; vec4 color; };
struct PackedAttributes { uint normal; uint tangent; vec2 uv; };
struct DebugVertex      { vec3 position; vec3 color; };
struct RenderItem       { mat4 worldMatrix; uint materialIndex; };
struct Material
{
    vec4  baseColorFactor;
    vec3  emissiveFactor;
    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    float occlusionStrength;
    float alphaCutoff;
    uint  baseColorTex;
    uint  metallicRoughnessTex;
    uint  normalTex;
    uint  occlusionTex;
    uint  emissiveTex;
    uint  flags;
};

layout(buffer_reference, scalar) readonly buffer PositionBuffer    { vec3 positions[]; };
layout(buffer_reference, scalar) readonly buffer AttributeBuffer   { PackedAttributes attributes[]; };
layout(buffer_reference, scalar) readonly buffer ColorBuffer       { uint colors[]; };
layout(buffer_reference, scalar) readonly buffer DebugVertexBuffer { DebugVertex vertices[]; };
layout(buffer_reference, scalar) readonly buffer MaterialBuffer    { Material materials[]; };
layout(buffer_reference, scalar) readonly buffer RenderItemBuffer  { RenderItem items[]; };

struct GpuTable
{
    uint64_t positions;
    uint64_t attributes;
    uint64_t colors;
    uint64_t materials;
    uint64_t renderItems;
    uint64_t debugLines;
    uint64_t reserved[2];
};

//Chadows CSM
struct GpuCascade { mat4 viewProj; float normalBias; float depthBias; float pad0; float pad1; };
const uint MAX_SHADOW_CASCADES = 4u;

layout(buffer_reference, scalar) readonly buffer FrameDataBuffer
{
    GpuTable   table;
    mat4       viewProj;
    GpuCascade cascades[MAX_SHADOW_CASCADES];
    vec3  cameraPosition;
    float exposure;
    vec3  sunDirection;
    float sunIntensity;
    vec3  sunColor;
    float ambientIntensity;
    vec3  skyColor;
    vec3  groundColor;
    uint  envIrradianceTex;
    uint  envPrefilterTex;
    float envIntensity;
    float envMaxLod;
    float shadowTexelSize;
    uint  shadowEnabled;
    uint  cascadeCount;
    uint  shadowDebug;
};

layout(push_constant, scalar) uniform PushConstants
{
    FrameDataBuffer frame;
    uint            viewIndex;
    uint            pad;
} pc;


vec3 octDecode(vec2 e)
{
    vec3 n = vec3(e, 1.0 - abs(e.x) - abs(e.y));
    float t = max(-n.z, 0.0);
    n.xy += mix(vec2(t), vec2(-t), greaterThanEqual(n.xy, vec2(0.0)));
    return normalize(n);
}

vec3 srgbToLinear(vec3 c)
{
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), greaterThan(c, vec3(0.04045)));
}



FrameDataBuffer frameData()             { return pc.frame; }
vec3            loadPosition(uint i)    { return PositionBuffer(pc.frame.table.positions).positions[i]; }
vec2            loadUV(uint i)          { return AttributeBuffer(pc.frame.table.attributes).attributes[i].uv; }
RenderItem      loadRenderItem(uint i)  { return RenderItemBuffer(pc.frame.table.renderItems).items[i]; }
Material        loadMaterial(uint i)    { return MaterialBuffer(pc.frame.table.materials).materials[i]; }
DebugVertex     loadDebugVertex(uint i) { return DebugVertexBuffer(pc.frame.table.debugLines).vertices[i]; }

vec4 clipPosition(RenderItem ri, vec3 localPos)
{
    return pc.frame.viewProj * (ri.worldMatrix * vec4(localPos, 1.0));
}

vec4 shadowClipPosition(RenderItem ri, vec3 localPos)
{
    // viewIndex pushed per cascade by ShadowPass::recordCascade
    return pc.frame.cascades[pc.viewIndex].viewProj * (ri.worldMatrix * vec4(localPos, 1.0));
}

vec4 loadColor(uint i)
{
    vec4 c = unpackUnorm4x8(ColorBuffer(pc.frame.table.colors).colors[i]);
    return vec4(srgbToLinear(c.rgb), c.a);
}

Vertex loadVertex(uint i)
{
    PackedAttributes a = AttributeBuffer(pc.frame.table.attributes).attributes[i];
    vec4 c = unpackUnorm4x8(ColorBuffer(pc.frame.table.colors).colors[i]);
    Vertex v;
    v.position = loadPosition(i);
    v.normal   = octDecode(unpackSnorm2x16(a.normal));
    v.tangent  = vec4(octDecode(unpackSnorm2x16(a.tangent)), (a.tangent & 1u) != 0u ? -1.0 : 1.0);
    v.uv       = a.uv;
    v.color    = loadColor(i);
    return v;
}
#endif