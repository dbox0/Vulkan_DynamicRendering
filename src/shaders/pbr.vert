#version 460

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

// must match render/GpuShared.h

struct Vertex
{
    vec3 position;
    vec3 normal;
    vec4 tangent;   // w = bitangent sign, 0 = no tangent supplied
    vec2 uv;
    vec4 color;
};

struct RenderItem
{
    mat4 worldMatrix;
    uint materialIndex;
};

layout(buffer_reference, scalar) readonly buffer VertexBuffer     { Vertex vertices[]; };
layout(buffer_reference, scalar) readonly buffer RenderItemBuffer { RenderItem items[]; };
layout(buffer_reference, scalar) readonly buffer FrameDataBuffer
{
    mat4  viewProj;
    vec3  cameraPosition;
    float exposure;
    vec3  sunDirection;
    float sunIntensity;
    vec3  sunColor;
    float ambientIntensity;
    vec3  skyColor;
    vec3  groundColor;
};

layout(push_constant, scalar) uniform FrameConstants
{
    uint64_t vertexBufferAddress;
    uint64_t materialBufferAddress;
    uint64_t renderItemBufferAddress;
    uint64_t frameDataAddress;
} pc;

// ---------------------------------------------------------------------------

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec4 outTangent;
layout(location = 3) out vec2 outUV;
layout(location = 4) out vec4 outColor;
layout(location = 5) out flat uint outMaterialIndex;

mat3 cofactor(mat3 m)
{
    return mat3(cross(m[1], m[2]),
            cross(m[2], m[0]),
            cross(m[0], m[1]));
}

void main()
{
    Vertex          v     = VertexBuffer(pc.vertexBufferAddress).vertices[gl_VertexIndex];
    RenderItem      ri    = RenderItemBuffer(pc.renderItemBufferAddress).items[gl_InstanceIndex];
    FrameDataBuffer frame = FrameDataBuffer(pc.frameDataAddress);

    vec4 worldPos = ri.worldMatrix * vec4(v.position, 1.0);
    gl_Position   = frame.viewProj * worldPos;

    outWorldPos = worldPos.xyz;
    outNormal = cofactor(mat3(ri.worldMatrix)) * v.normal;
    // Tangents lie IN the surface, so they transform with the model matrix,
    // not with the inverse-transpose like normals do!
    outTangent  = vec4(mat3(ri.worldMatrix) * v.tangent.xyz, v.tangent.w);
    outUV       = v.uv;
    outColor    = v.color;
    outMaterialIndex = ri.materialIndex;
}
