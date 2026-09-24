#version 460
#include "../common/shading.glsl"

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inUV;
layout(location = 4) in vec4 inColor;
layout(location = 5) in flat uint inMaterialIndex;

layout(location = 0) out vec4 fragColor;

float geometricSpecularAA(vec3 N, float alpha)
{
    const float sigma2 = 0.25;
    const float kappa  = 0.18;
    vec3  dNdx = dFdx(N);
    vec3  dNdy = dFdy(N);
    float variance = sigma2 * (dot(dNdx, dNdx) + dot(dNdy, dNdy));
    return clamp(alpha + min(2.0 * variance, kappa), 0.0, 1.0);
}

// ---------------------------------------------------------------------------

void main()
{
    Material mat = loadMaterial(inMaterialIndex);

    // ---- base colour / alpha ------------------------------------------------
    vec4 baseTex   = sampleTex(mat.baseColorTex, inUV);
    vec4 baseColor = vec4(mat.baseColorFactor.rgb * inColor.rgb * baseTex.rgb,
                          materialAlpha(mat, inColor.a, baseTex.a));

    if ((mat.flags & MAT_ALPHA_MASK) != 0u) {
        if (baseColor.a < mat.alphaCutoff) {
            discard;
        }
        baseColor.a = 1.0;
    } else if ((mat.flags & MAT_ALPHA_BLEND) == 0u) {
        baseColor.a = 1.0;                       // OPAQUE ignores alpha
    }

    // ---- metallic / roughness / occlusion / emissive ------------------------
    vec4  mrSample  = sampleTex(mat.metallicRoughnessTex, inUV);
    float roughness = clamp(mat.roughnessFactor * mrSample.g, 0.045, 1.0);
    float metallic  = clamp(mat.metallicFactor  * mrSample.b, 0.0,   1.0);
    float ao        = mix(1.0, sampleTex(mat.occlusionTex, inUV).r, mat.occlusionStrength);
    vec3  emissive  = mat.emissiveFactor * sampleTex(mat.emissiveTex, inUV).rgb;

    // ---- normal -------------------------------------------------------------
    // MikkTSpace reconstruction: unnormalised interpolants, bitangent from the
    // cross product, one normalize at the end. Matches the baker.
    vec3 vN = inNormal;
    vec3 vT = inTangent.xyz;
    vec3 vB = inTangent.w * cross(vN, vT);

    vec3 N = normalize(vN);
    if ((mat.flags & MAT_NORMAL_MAP) != 0u) {
        vec3 n = sampleTex(mat.normalTex, inUV).xyz * 2.0 - 1.0;
        n.xy *= mat.normalScale;
        N = normalize(n.x * vT + n.y * vB + n.z * vN);
    }

    // Back face of a double-sided material: the whole frame mirrors, which is
    // the same as negating the result.
    vec3 Ng = normalize(vN);
    if ((mat.flags & MAT_DOUBLE_SIDED) != 0u && !gl_FrontFacing) {
        N  = -N;
        Ng = -Ng;
    }

    Surface s;
    s.position  = inWorldPos;
    s.N         = N;
    s.Ng        = Ng;
    s.albedo    = baseColor.rgb;
    s.metallic  = metallic;
    s.roughness = roughness;
    s.alpha     = geometricSpecularAA(N, max(roughness * roughness, 0.008));
    s.occlusion = ao;

    if ((mat.flags & MAT_ALPHA_BLEND) == 0u) {
        s.occlusion = min(ao, texelFetch(screenAo, ivec2(gl_FragCoord.xy), 0).r);
    }
    s.emissive  = emissive;
    fragColor = vec4(vec3(s.occlusion), 1.0); return;
    fragColor = vec4(shadeSurface(s), baseColor.a);
}
