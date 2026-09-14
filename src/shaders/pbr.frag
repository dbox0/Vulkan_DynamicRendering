#version 460

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : require

// ---- must match render/GpuShared.h

const uint MAT_ALPHA_MASK   = 1u << 0;
const uint MAT_ALPHA_BLEND  = 1u << 1;
const uint MAT_DOUBLE_SIDED = 1u << 2;
const uint MAT_NORMAL_MAP   = 1u << 3;

struct Material
{
    vec4  baseColorFactor;
    vec3  emissiveFactor;
    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    float occlusionStrength;
    float alphaCutoff;
    uint  baseColorTex;
    uint  metallicRoughnessTex;
    uint  normalTex;
    uint  occlusionTex;
    uint  emissiveTex;
    uint  flags;
};

layout(buffer_reference, scalar) readonly buffer MaterialBuffer { Material materials[]; };
layout(buffer_reference, scalar) readonly buffer FrameDataBuffer
{
    mat4  viewProj;
    vec3  cameraPosition;
    float exposure;
    vec3  sunDirection;
    float sunIntensity;
    vec3  sunColor;
    float ambientIntensity;
    vec3  skyColor;
    vec3  groundColor;
};

layout(push_constant, scalar) uniform FrameConstants
{
    uint64_t vertexBufferAddress;
    uint64_t materialBufferAddress;
    uint64_t renderItemBufferAddress;
    uint64_t frameDataAddress;
} pc;

// ---------------------------------------------------------------------------

layout(set = 0, binding = 0) uniform sampler2D textures[];

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inUV;
layout(location = 4) in vec4 inColor;
layout(location = 5) in flat uint inMaterialIndex;

layout(location = 0) out vec4 fragColor;

const float PI = 3.14159265359;


vec4 sampleTex(uint slot, vec2 uv)
{
    return texture(textures[nonuniformEXT(slot)], uv);
}

// ---- BRDF (glTF 2.0 spec, Appendix B) -------------------------------------

float D_GGX(float NdotH, float alpha)
{
    float a2 = alpha * alpha;
    float d  = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

float V_SmithGGXCorrelated(float NdotV, float NdotL, float alpha)
{
    float a2   = alpha * alpha;
    float ggxV = NdotL * sqrt(NdotV * NdotV * (1.0 - a2) + a2);
    float ggxL = NdotV * sqrt(NdotL * NdotL * (1.0 - a2) + a2);
    float denom = ggxV + ggxL;
    return denom > 0.0 ? 0.5 / denom : 0.0;
}

vec3 F_Schlick(vec3 f0, float VdotH)
{
    return f0 + (1.0 - f0) * pow(1.0 - VdotH, 5.0);
}

// Karis' analytic fit of the split-sum environment BRDF. Stands in for the
// BRDF LUT until real IBL exists, so metals aren't black outside the sun.
vec3 EnvBRDFApprox(vec3 f0, float roughness, float NdotV)
{
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572,  0.022);
    const vec4 c1 = vec4( 1.0,  0.0425,  1.04,  -0.04);
    vec4  r    = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
    vec2  AB   = vec2(-1.04, 1.04) * a004 + r.zw;
    return f0 * AB.x + AB.y;
}

// Khronos PBR Neutral:
vec3 PBRNeutralToneMapping(vec3 color)
{
    const float startCompression = 0.8 - 0.04;
    const float desaturation     = 0.15;

    float x = min(color.r, min(color.g, color.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    color -= offset;

    float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression) {
        return color;
    }

    const float d = 1.0 - startCompression;
    float newPeak = 1.0 - d * d / (peak + d - startCompression);
    color *= newPeak / peak;

    float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return mix(color, vec3(newPeak), g);
}

// ---------------------------------------------------------------------------

void main()
{
    Material        mat   = MaterialBuffer(pc.materialBufferAddress).materials[inMaterialIndex];
    FrameDataBuffer frame = FrameDataBuffer(pc.frameDataAddress);

    // Derivatives up front, in uniform control flow.
    vec3 dPdx  = dFdx(inWorldPos);
    vec3 dPdy  = dFdy(inWorldPos);
    vec2 dUVdx = dFdx(inUV);
    vec2 dUVdy = dFdy(inUV);

    // ---- base colour / alpha ------------------------------------------------
    vec4 baseColor = mat.baseColorFactor * inColor * sampleTex(mat.baseColorTex, inUV);

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
    vec3 N = normalize(inNormal);


    if ((mat.flags & MAT_DOUBLE_SIDED) != 0u && !gl_FrontFacing) {
        N = -N;
    }

    if ((mat.flags & MAT_NORMAL_MAP) != 0u) {
        vec3 T, B;
        if (inTangent.w != 0.0) {
            T = normalize(inTangent.xyz - N * dot(N, inTangent.xyz));  // re-orthogonalise
            B = cross(N, T) * sign(inTangent.w);
        } else {
            // No tangents in the mesh: cotangent frame from screen-space
            // derivatives (Schueler). Good enough until MikkTSpace is in.
            vec3 dp2perp = cross(dPdy, N);
            vec3 dp1perp = cross(N, dPdx);
            T = dp2perp * dUVdx.x + dp1perp * dUVdy.x;
            B = dp2perp * dUVdx.y + dp1perp * dUVdy.y;
            float invMax = inversesqrt(max(max(dot(T, T), dot(B, B)), 1e-20));
            T *= invMax;
            B *= invMax;
        }

        vec3 n = sampleTex(mat.normalTex, inUV).xyz * 2.0 - 1.0;
        n.xy *= mat.normalScale;
        N = normalize(mat3(T, B, N) * n);
    }


    // ---- lighting -----------------------------------------------------------
    vec3  V     = normalize(frame.cameraPosition - inWorldPos);
    float NdotV = max(dot(N, V), 1e-4);
    float alpha = roughness * roughness;

    vec3 cDiff = mix(baseColor.rgb, vec3(0.0), metallic);
    vec3 F0    = mix(vec3(0.04),    baseColor.rgb, metallic);

    // Sun (directional).
    vec3  L     = normalize(-frame.sunDirection);
    vec3  H     = normalize(V + L);
    float NdotL = clamp(dot(N, L), 0.0, 1.0);
    float NdotH = clamp(dot(N, H), 0.0, 1.0);
    float VdotH = clamp(dot(V, H), 0.0, 1.0);

    vec3 F        = F_Schlick(F0, VdotH);
    vec3 specular = F * D_GGX(NdotH, alpha) * V_SmithGGXCorrelated(NdotV, NdotL, alpha);
    vec3 diffuse  = (1.0 - F) * cDiff / PI;
    vec3 direct   = (diffuse + specular) * frame.sunColor * frame.sunIntensity * NdotL;

    // Ambient: hemisphere "environment" until real IBL. Diffuse samples it
    // along N, specular along the reflection vector (blurrier when rough).
    vec3 R          = reflect(-V, N);
    vec3 hemiN      = mix(frame.groundColor, frame.skyColor, N.y * 0.5 + 0.5);
    vec3 hemiR      = mix(frame.groundColor, frame.skyColor, R.y * 0.5 + 0.5);
    vec3 ambientDif = hemiN * cDiff;
    vec3 ambientSpc = mix(hemiR, hemiN, roughness) * EnvBRDFApprox(F0, roughness, NdotV);
    vec3 ambient    = (ambientDif + ambientSpc) * ao * frame.ambientIntensity;

    vec3 hdr = direct + ambient + emissive;
    
    fragColor = vec4(PBRNeutralToneMapping(hdr * frame.exposure), baseColor.a);
}
