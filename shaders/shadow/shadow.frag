#version 460
#include "../common/scene.glsl"

layout(set = 0, binding = 0) uniform sampler2D textures[];
layout(location = 0) in vec2 inUV;
layout(location = 1) in flat uint inMaterialIndex;

void main()
{
    Material mat = loadMaterial(inMaterialIndex);

    if ((mat.flags & MAT_ALPHA_MASK) != 0u) {
        float alpha = mat.baseColorFactor.a
                    * texture(textures[nonuniformEXT(mat.baseColorTex)], inUV).a;
        if (alpha < mat.alphaCutoff) {
            discard;
        }
    }
}
