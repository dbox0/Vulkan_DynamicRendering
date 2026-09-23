#ifndef COMMON_BINDLESS_GLSL
#define COMMON_BINDLESS_GLSL

#extension GL_EXT_nonuniform_qualifier : require

#include "scene.glsl"

// Set 0 owned by ResourceStore. Layout: Fragment Stage only
layout(set = 0, binding = 0) uniform sampler2D   textures[];
layout(set = 0, binding = 1) uniform samplerCube cubes[];
layout(set = 0, binding = 2) uniform sampler2D   brdfLut;

vec4 sampleTex(uint slot, vec2 uv)
{
    return texture(textures[nonuniformEXT(slot)],uv);
}
float materialAlpha(Material mat, float vertexAlpha, float texAlpha){
    return mat.baseColorFactor.a * vertexAlpha * texAlpha;
}
float baseAlpha(Material mat, vec2 uv, float vertexAlpha)
{
    return materialAlpha(mat, vertexAlpha, sampleTex(mat.baseColorTex, uv).a);
}
#endif