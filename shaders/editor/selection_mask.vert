#version 460
#include "../common/scene.glsl"

void main()
{
    RenderItem ri = loadRenderItem(gl_InstanceIndex);

    gl_Position = frameData().viewProj * ri.worldMatrix * vec4(loadVertex(gl_VertexIndex).position,1.0);
}
