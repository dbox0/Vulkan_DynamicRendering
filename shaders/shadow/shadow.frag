#version 460
#include "../common/bindless.glsl"

layout(location = 0) in vec2 inUV;
layout(location = 1) in float inAlpha;
layout(location = 2) in flat uint inMaterialIndex;

// Only masked buckets use this pipeline
// no flag test.
void main()
{
    Material mat = loadMaterial(inMaterialIndex);
    if (baseAlpha(mat, inUV, inAlpha) < mat.alphaCutoff) {
        discard;
    }
}