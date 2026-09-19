#version 460

#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_nonuniform_qualifier : require

// set 0 is ResourceStore's bindless global set; binding 0 (textures[]) is not
// declared here because this pass does not use it.
layout(set = 0, binding = 1) uniform samplerCube cubes[];

// Must match SkyboxConstants in passes/SkyboxPass.h, and skybox.vert.
layout(push_constant, scalar) uniform SkyboxConstants
{
    mat4  invViewProj;      // offset 0
    vec3  cameraPosition;   // offset 64
    uint  cubeSlot;         // offset 76
    float intensity;        // offset 80
    float lod;              // offset 84
} pc;

layout(location = 0) in vec3 inDir;

layout(location = 0) out vec4 fragColor;

void main()
{
    // Interpolated across the near plane, so it needs renormalising per pixel.
    vec3 dir = normalize(inDir);

    // Cube slots are 1-based so that 0 can mean "none"; the array is 0-based.
    vec3 sky = textureLod(cubes[nonuniformEXT(pc.cubeSlot - 1u)], dir, pc.lod).rgb;

    // Scene-referred HDR. The tonemap pass handles exposure and display.
    fragColor = vec4(sky * pc.intensity, 1.0);
}
