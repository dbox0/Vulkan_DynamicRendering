#version 460

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require


struct DebugVertex
{
    vec3 position;
    vec3 color;
};

layout(buffer_reference, scalar) readonly buffer DebugVertexBuffer { DebugVertex vertices[]; };
layout(buffer_reference, scalar) readonly buffer FrameDataBuffer   { mat4 viewProj; };

layout(push_constant, scalar) uniform FrameConstants
{
    uint64_t vertexBufferAddress;
    uint64_t materialBufferAddress;
    uint64_t renderItemBufferAddress;
    uint64_t frameDataAddress;
} pc;

layout(location = 0) out vec3 outColor;

void main()
{
    DebugVertex v = DebugVertexBuffer(pc.vertexBufferAddress).vertices[gl_VertexIndex];

    gl_Position = FrameDataBuffer(pc.frameDataAddress).viewProj * vec4(v.position, 1.0);
    outColor    = v.color;
}
