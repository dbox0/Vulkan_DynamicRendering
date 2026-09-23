#version 460
#include "../common/scene.glsl"

invariant gl_Position;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec4 outTangent;
layout(location = 3) out vec2 outUV;
layout(location = 4) out vec4 outColor;
layout(location = 5) out flat uint outMaterialIndex;

mat3 cofactor(mat3 m)
{
    return mat3(cross(m[1], m[2]),
            cross(m[2], m[0]),
            cross(m[0], m[1]));
}

void main()
{
    Vertex     v  = loadVertex(gl_VertexIndex);
    RenderItem ri = loadRenderItem(gl_InstanceIndex);

    gl_Position   = clipPosition(ri, v.position);
    vec4 worldPos = ri.worldMatrix * vec4(v.position, 1.0);

    outWorldPos = worldPos.xyz;
    outNormal = cofactor(mat3(ri.worldMatrix)) * v.normal;
    // Tangents lie IN the surface, so they transform with the model matrix,
    // not with the inverse-transpose like normals do!
    outTangent  = vec4(mat3(ri.worldMatrix) * v.tangent.xyz, v.tangent.w);
    outUV       = v.uv;
    outColor    = v.color;
    outMaterialIndex = ri.materialIndex;
}
