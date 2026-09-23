#version 460
#include "../common/scene.glsl"



layout(location = 0) out vec2 outUV;
layout(location = 1) out float outAlpha;
layout(location = 2) out flat uint outMaterialIndex;

void main()
{
    RenderItem ri = loadRenderItem(gl_InstanceIndex);
    gl_Position = shadowClipPosition(ri, loadPosition(gl_VertexIndex));

    outUV            = loadUV(gl_VertexIndex);
    outAlpha         = loadColor(gl_VertexIndex).a;
    outMaterialIndex = ri.materialIndex;
}

