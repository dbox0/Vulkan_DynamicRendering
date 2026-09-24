#ifndef AO_GTAO_COMMON_GLSL
#define AO_GTAO_COMMON_GLSL

#extension GL_EXT_scalar_block_layout : require


const float GTAO_PI         = 3.1415926535897932;
const float GTAO_HALF_PI    = 1.5707963267948966;
const float GTAO_TERM_SCALE = 1.5;
const float GTAO_MAX_DEPTH  = 1.0e6;   // sky


#ifndef GTAO_OWN_PUSH_CONSTANTS

// Match GtaoConstants in render/passes/ao/GtaoPass.h.
// View space inside these shaders is XeGTAO's: +x right, +y up, +z forward (depth positive)
layout(push_constant, scalar) uniform GtaoConstants
{
    ivec2 viewportSize;
    vec2  viewportPixelSize;
    vec2  depthUnpack;         // viewDepth = x / (ndcDepth + y), reverse Z
    vec2  uvToViewMul;
    vec2  uvToViewAdd;
    vec2  uvToViewMulPixel;    // uvToViewMul * viewportPixelSize
    float effectRadius;        // world units, radius multiplier applied
    float falloffRange;        // fraction of effectRadius
    float finalPower;
    float denoiseBeta;
    float mipSamplingOffset;
    uint  sliceCount;
    uint  stepsPerSlice;
    uint  noiseIndex;
    uint  finalPass;
} gtao;

float linearViewDepth(float ndcDepth)
{
    return ndcDepth <= 0.0 ? GTAO_MAX_DEPTH
    : min(gtao.depthUnpack.x / (ndcDepth + gtao.depthUnpack.y), GTAO_MAX_DEPTH);
}
vec3 viewPosition(vec2 uv, float viewDepth)
{
    return vec3((gtao.uvToViewMul * uv + gtao.uvToViewAdd) * viewDepth, viewDepth);
}

#endif


#endif