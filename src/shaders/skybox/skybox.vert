#version 460

#extension GL_EXT_scalar_block_layout : require

// Must match SkyboxConstants in passes/SkyboxPass.h, and skybox.frag.
layout(push_constant, scalar) uniform SkyboxConstants
{
    mat4  invViewProj;      // offset 0
    vec3  cameraPosition;   // offset 64
    uint  cubeSlot;         // offset 76
    float intensity;        // offset 80
    float lod;              // offset 84
} pc;

layout(location = 0) out vec3 outDir;

void main()
{
    // Fullscreen triangle from the vertex index: (0,0) (2,0) (0,2) in UV,
    // which covers NDC [-1,3] on both axes. No vertex buffer, no cube mesh.
    vec2 uv  = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vec2 ndc = uv * 2.0 - 1.0;

    // Unproject the NEAR plane, which is z = 1 under reverse Z. The far plane
    // would be the more obvious choice but sits at infinity for an infinite
    // projection, so w collapses to 0 and the divide falls apart.
    vec4 nearPoint = pc.invViewProj * vec4(ndc, 1.0, 1.0);
    outDir = nearPoint.xyz / nearPoint.w - pc.cameraPosition;

    // z = 0 is the far plane under reverse Z, and equals the depth clear, so
    // the sky survives only where no geometry was drawn.
    gl_Position = vec4(ndc, 0.0, 1.0);
}
