#ifndef COMMON_SCENE_GLSL
#define COMMON_SCENE_GLSL

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

const uint MAT_ALPHA_MASK   = 1u << 0;
const uint MAT_ALPHA_BLEND  = 1u << 1;
const uint MAT_DOUBLE_SIDED = 1u << 2;
const uint MAT_NORMAL_MAP   = 1u << 3;

struct Vertex      { vec3 position; vec3 normal; vec4 tangent; vec2 uv; vec4 color; };
struct DebugVertex { vec3 position; vec3 color; };
struct RenderItem  { mat4 worldMatrix; uint materialIndex; };
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

layout(buffer_reference, scalar) readonly buffer VertexBuffer      { Vertex vertices[]; };
layout(buffer_reference, scalar) readonly buffer DebugVertexBuffer { DebugVertex vertices[]; };
layout(buffer_reference, scalar) readonly buffer MaterialBuffer    { Material materials[]; };
layout(buffer_reference, scalar) readonly buffer RenderItemBuffer  { RenderItem items[]; };

struct GpuTable
{
    uint64_t vertices;
    uint64_t materials;
    uint64_t renderItems;
    uint64_t debugLines;
    uint64_t reserved[4];
};

layout(buffer_reference, scalar) readonly buffer FrameDataBuffer
{
    GpuTable table;
    mat4  viewProj;
    mat4  lightViewProj;
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
    float shadowNormalBias;
    float shadowDepthBias;
    uint  shadowEnabled;
};

layout(push_constant, scalar) uniform PushConstants
{
    FrameDataBuffer frame;
    uint            viewIndex;
    uint            pad;
} pc;

FrameDataBuffer frameData()             { return pc.frame; }
Vertex          loadVertex(uint i)      { return VertexBuffer(pc.frame.table.vertices).vertices[i]; }
RenderItem      loadRenderItem(uint i)  { return RenderItemBuffer(pc.frame.table.renderItems).items[i]; }
Material        loadMaterial(uint i)    { return MaterialBuffer(pc.frame.table.materials).materials[i]; }
DebugVertex     loadDebugVertex(uint i) { return DebugVertexBuffer(pc.frame.table.debugLines).vertices[i]; }
#endif