#version 460
#include "../common/scene.glsl"


layout(location = 0) out vec3 outColor;

void main()
{

    DebugVertex v = loadDebugVertex(gl_VertexIndex);

    gl_Position = frameData().viewProj * vec4(v.position, 1.0);
    outColor    = v.color;
}
