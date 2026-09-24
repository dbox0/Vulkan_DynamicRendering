#version 460
#include "../common/scene.glsl"

// Opaque casters: position only, no fragment stage
void main()
{
    gl_Position = shadowClipPosition(loadRenderItem(gl_InstanceIndex), loadPosition(gl_VertexIndex));
}