#version 460

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

// Position-only twin of pbr.vert. Same vertex pulling, same RenderItem
// indexing, same push constants -- so the mask can be drawn straight from the
// indirect commands the scene pass already wrote. Nothing to keep in sync.

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
    mat3 normalMatrix;
    uint materialIndex;
};

layout(buffer_reference, scalar) readonly buffer VertexBuffer     { Vertex vertices[]; };
layout(buffer_reference, scalar) readonly buffer RenderItemBuffer { RenderItem items[]; };

// Only viewProj is read, and it sits at offset 0 of FrameData.
layout(buffer_reference, scalar) readonly buffer FrameDataBuffer  { mat4 viewProj; };

layout(push_constant, scalar) uniform FrameConstants
{
    uint64_t vertexBufferAddress;
    uint64_t materialBufferAddress;
    uint64_t renderItemBufferAddress;
    uint64_t frameDataAddress;
} pc;

void main()
{
    Vertex     v  = VertexBuffer(pc.vertexBufferAddress).vertices[gl_VertexIndex];
    RenderItem ri = RenderItemBuffer(pc.renderItemBufferAddress).items[gl_InstanceIndex];

    gl_Position = FrameDataBuffer(pc.frameDataAddress).viewProj
                * ri.worldMatrix
                * vec4(v.position, 1.0);
}
