#version 460
#extension GL_EXT_scalar_block_layout : require

layout(set = 0, binding = 0) uniform sampler2D uSelectionMask;

layout(push_constant, scalar) uniform OutlineConstants
{
    vec4 color;
    int  thickness;     // radius in pixels
} pc;

layout(location = 0) out vec4 outColor;

void main()
{
    ivec2 texel = ivec2(gl_FragCoord.xy);
    ivec2 size  = textureSize(uSelectionMask, 0);

    // Inside the silhouette, leave the pixel alone. An outline that paints
    // over the surface hides the shading you selected the object to look at.
    if (texelFetch(uSelectionMask, texel, 0).r > 0.5) {
        discard;
    }

    int radius   = pc.thickness;
    int radiusSq = radius * radius;

    // Dilate: if any selected texel is within `radius`, this pixel is part of
    // the border. Working outward from the silhouette rather than detecting
    // gradients is what keeps the width constant and gap-free -- a Sobel on
    // the mask would thin out wherever the edge runs diagonally.
    for (int y = -radius; y <= radius; ++y) {
        for (int x = -radius; x <= radius; ++x) {
            if (x * x + y * y > radiusSq) {
                continue;                   // round cap, not a square one
            }
            ivec2 sampleTexel = clamp(texel + ivec2(x, y), ivec2(0), size - ivec2(1));
            if (texelFetch(uSelectionMask, sampleTexel, 0).r > 0.5) {
                outColor = pc.color;
                return;
            }
        }
    }

    discard;
}
