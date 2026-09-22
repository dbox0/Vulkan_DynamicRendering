#version 460

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

// Same vertex pulling and RenderItem indexing as pbr.vert, so the shadow pass
// replays the indirect commands the scene pass already wrote. Only the matrix
// differs. UV and material go through for alpha-masked casters.

struct Vertex
{
    vec3 position;
    vec3 normal;
    vec4 tangent;
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

// Only the two matrices are read, and they are the first two members of FrameData.
layout(buffer_reference, scalar) readonly buffer FrameDataBuffer
{
    mat4 viewProj;
    mat4 lightViewProj;
};

layout(push_constant, scalar) uniform FrameConstants
{
    uint64_t vertexBufferAddress;
    uint64_t materialBufferAddress;
    uint64_t renderItemBufferAddress;
    uint64_t frameDataAddress;
} pc;

layout(location = 0) out vec2 outUV;
layout(location = 1) out flat uint outMaterialIndex;

void main()
{
    Vertex     v  = VertexBuffer(pc.vertexBufferAddress).vertices[gl_VertexIndex];
    RenderItem ri = RenderItemBuffer(pc.renderItemBufferAddress).items[gl_InstanceIndex];

    gl_Position = FrameDataBuffer(pc.frameDataAddress).lightViewProj
                * ri.worldMatrix
                * vec4(v.position, 1.0);

    outUV            = v.uv;
    outMaterialIndex = ri.materialIndex;
}
