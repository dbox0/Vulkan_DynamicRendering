#version 460

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : require

// Depth-only pass, so this exists for one reason: alpha-masked casters. Without
// it every leaf card throws a solid quad of shadow.

const uint MAT_ALPHA_MASK = 1u << 0;

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

layout(buffer_reference, scalar) readonly buffer MaterialBuffer { Material materials[]; };

layout(push_constant, scalar) uniform FrameConstants
{
    uint64_t vertexBufferAddress;
    uint64_t materialBufferAddress;
    uint64_t renderItemBufferAddress;
    uint64_t frameDataAddress;
} pc;

layout(set = 0, binding = 0) uniform sampler2D textures[];

layout(location = 0) in vec2 inUV;
layout(location = 1) in flat uint inMaterialIndex;

void main()
{
    Material mat = MaterialBuffer(pc.materialBufferAddress).materials[inMaterialIndex];

    if ((mat.flags & MAT_ALPHA_MASK) != 0u) {
        float alpha = mat.baseColorFactor.a
                    * texture(textures[nonuniformEXT(mat.baseColorTex)], inUV).a;
        if (alpha < mat.alphaCutoff) {
            discard;
        }
    }
}
