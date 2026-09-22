#version 460
#include "../common/scene.glsl"


layout(location = 0) out vec2 outUV;
layout(location = 1) out flat uint outMaterialIndex;

void main()
{
    RenderItem ri = loadRenderItem(gl_InstanceIndex);
    gl_Position = frameData().lightViewProj * ri.worldMatrix * vec4(loadPosition(gl_VertexIndex), 1.0);

    outUV            = loadUV(gl_VertexIndex);
    outMaterialIndex = ri.materialIndex;
}
