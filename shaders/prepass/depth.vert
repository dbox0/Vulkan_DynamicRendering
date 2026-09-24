#version 460
#include "../common/scene.glsl"


invariant gl_Position;

// Opaque buckets. Pos only
void main(){
    gl_Position = clipPosition(loadRenderItem(gl_InstanceIndex), loadPosition(gl_VertexIndex));
}