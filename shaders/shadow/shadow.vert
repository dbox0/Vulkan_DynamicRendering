#version 460
#include "../common/scene.glsl"


layout(location = 0) out vec2 outUV;
layout(location = 1) out flat uint outMaterialIndex;

void main()
{
    Vertex     v  = loadVertex(gl_VertexIndex);
    RenderItem ri = loadRenderItem(gl_InstanceIndex);

    gl_Position = frameData().lightViewProj
                * ri.worldMatrix
                * vec4(v.position, 1.0);

    outUV            = v.uv;
    outMaterialIndex = ri.materialIndex;
}
