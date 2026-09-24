#version 460
#include "../common/scene.glsl"

invariant gl_Position;

layout(location = 0) out vec2 outUV;
layout(location = 1) out float outAlpha;
layout(location = 2) out flat uint outMaterialIndex;

void main(){
    RenderItem ri = loadRenderItem(gl_InstanceIndex);
    gl_Position = clipPosition(ri,loadPosition(gl_VertexIndex));

    outUV = loadUV(gl_VertexIndex);
    outAlpha = loadColor(gl_VertexIndex).a;
    outMaterialIndex = ri.materialIndex;
}