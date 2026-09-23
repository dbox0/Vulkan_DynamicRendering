#version 460
#include "../common/scene.glsl"


layout(location = 0) out vec2 outUV;
layout(location = 1) out flat uint outMaterialIndex;

void main()
{
    RenderItem ri = loadRenderItem(gl_InstanceIndex);
    gl_Position = shadowClipPosition(ri, loadPosition(gl_VertexIndex));

    outUV            = loadUV(gl_VertexIndex);
    outMaterialIndex = ri.materialIndex;
}
