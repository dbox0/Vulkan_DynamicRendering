#version 460
#include "../common/scene.glsl"

void main()
{
    RenderItem ri = loadRenderItem(gl_InstanceIndex);

    gl_Position = frameData().viewProj * ri.worldMatrix * vec4(loadPosition(gl_VertexIndex), 1.0);
}
