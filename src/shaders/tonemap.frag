#version 460

// Must match TonemapConstants in TonemapPass.h.
const uint TONEMAP_NONE     = 0u;
const uint TONEMAP_REINHARD = 1u;
const uint TONEMAP_ACES     = 2u;

layout(push_constant) uniform TonemapConstants
{
    float exposure;
    uint  tonemapper;
    float bloomStrength;
    int bloomDebugMip;
} pc;

layout(set = 0, binding = 0) uniform sampler2D hdrTexture;
layout(set = 0, binding = 1) uniform sampler2D bloomTexture;

layout(location = 0) in  vec2 inUV;
layout(location = 0) out vec4 fragColor;

// Extended Reinhard.
vec3 reinhard(vec3 color)
{
    const float W = 4.0;
    vec3 numerator = color * (1.0 + color / (W * W));
    return numerator / (1.0 + color);
}


// Narkowicz 2015, an approximation of the ACES filmic curve.
vec3 acesNarkowicz(vec3 color)
{
    color *= 0.6;
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
}
void main()
{
    vec3 bloom = textureLod(bloomTexture, inUV, 0.0).rgb;
    if (pc.bloomDebugMip >= 0) {
        fragColor = vec4(textureLod(bloomTexture, inUV, float(pc.bloomDebugMip)).rgb, 1.0);
        return;
    }
    vec3 hdr = (texture(hdrTexture, inUV).rgb + bloom * pc.bloomStrength) * pc.exposure;

    vec3 color;
    switch (pc.tonemapper) {
        case TONEMAP_REINHARD: color = reinhard(hdr);       break;
        case TONEMAP_ACES:     color = acesNarkowicz(hdr);  break;
        default:               color = hdr;                 break;
    }
    fragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}