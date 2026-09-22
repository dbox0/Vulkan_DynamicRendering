#version 460

// R8_UNORM target: only .r is kept. Coverage, nothing else.
layout(location = 0) out vec4 outMask;

void main()
{
    outMask = vec4(1.0);
}
