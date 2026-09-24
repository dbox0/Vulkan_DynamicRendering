#ifndef COMMON_SHADING_GLSL
#define COMMON_SHADING_GLSL

#include "bindless.glsl"

layout(set = 1, binding = 0) uniform sampler2DArrayShadow shadowMap;

const float PI = 3.14159265359;

vec3 sampleIrradiance(uint slot, vec3 N)
{
    return texture(cubes[nonuniformEXT(slot - 1u)], N).rgb;
}

vec3 samplePrefiltered(uint slot, vec3 R, float lod)
{
    return textureLod(cubes[nonuniformEXT(slot - 1u)], R, lod).rgb;
}

vec3 envBRDF(vec3 f0, float roughness, float NdotV)
{
    vec2 ab = texture(brdfLut, vec2(NdotV, roughness)).rg;
    return f0 * ab.x + ab.y;
}

float specularOcclusion(float NdotV, float ao, float roughness)
{
    return clamp(pow(NdotV + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao, 0.0, 1.0);
}

float horizonOcclusion(vec3 R, vec3 Ng)
{
    float horizon = min(1.0 + dot(R, Ng), 1.0);
    return horizon * horizon;
}

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

vec3 EnvBRDFApprox(vec3 f0, float roughness, float NdotV)
{
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572,  0.022);
    const vec4 c1 = vec4( 1.0,  0.0425,  1.04,  -0.04);
    vec4  r    = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
    vec2  AB   = vec2(-1.04, 1.04) * a004 + r.zw;
    return f0 * AB.x + AB.y;
}

const vec3 CASCADE_TINT[4] = vec3[](vec3(1.0, 0.4, 0.4), vec3(0.4, 1.0, 0.4),
                                    vec3(0.4, 0.6, 1.0), vec3(1.0, 1.0, 0.4));

int pickCascade(FrameDataBuffer frame, vec3 p)
{
    float margin = 4.0 * frame.shadowTexelSize;

    for (uint k = 0u; k < frame.cascadeCount; ++k) {
        vec3 c = (frame.cascades[k].viewProj * vec4(p, 1.0)).xyz;
        if (all(lessThan(abs(c.xy), vec2(1.0 - margin))) && c.z > 0.0 && c.z < 1.0) {
            return int(k);
        }
    }
    return -1;
}

float sunShadow(FrameDataBuffer frame, vec3 worldPos, vec3 N, float NdotL)
{
    if (frame.shadowEnabled == 0u) {
        return 1.0;
    }

    int k = pickCascade(frame, worldPos);
    if (k < 0) {
        return 1.0;
    }
    GpuCascade cs = frame.cascades[k];

    float slope  = clamp(1.0 - NdotL, 0.0, 1.0);
    vec3  origin = worldPos + N * cs.normalBias * (1.0 + slope * 2.0);

    vec3  proj  = (cs.viewProj * vec4(origin, 1.0)).xyz;
    vec2  uv    = proj.xy * 0.5 + 0.5;
    float ref   = proj.z + cs.depthBias;
    float layer = float(k);

    float sum = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 offset = vec2(x, y) * frame.shadowTexelSize * 0.5;
            sum += texture(shadowMap, vec4(uv + offset, layer, ref));
        }
    }
    return sum * (1.0 / 9.0);
}

struct Surface
{
    vec3  position;
    vec3  N;
    vec3  Ng;
    vec3  albedo;
    float metallic;
    float roughness;
    float alpha;
    float occlusion;
    vec3  emissive;
};

vec3 shadeSurface(Surface s)
{
    FrameDataBuffer frame = frameData();

    vec3  V     = normalize(frame.cameraPosition - s.position);
    float NdotV = max(dot(s.N, V), 1e-4);

    vec3 cDiff = mix(s.albedo, vec3(0.0), s.metallic);
    vec3 F0    = mix(vec3(0.04), s.albedo, s.metallic);

    vec3  L     = normalize(-frame.sunDirection);
    vec3  H     = normalize(V + L);
    float NdotL = clamp(dot(s.N, L), 0.0, 1.0);
    float NdotH = clamp(dot(s.N, H), 0.0, 1.0);
    float VdotH = clamp(dot(V, H), 0.0, 1.0);

    vec3 F        = F_Schlick(F0, VdotH);
    vec3 specular = F * D_GGX(NdotH, s.alpha) * V_SmithGGXCorrelated(NdotV, NdotL, s.alpha);
    vec3 diffuse  = (1.0 - F) * cDiff / PI;
    float shadow  = NdotL > 0.0 ? sunShadow(frame, s.position, s.N, NdotL) : 1.0;
    vec3 direct   = (diffuse + specular) * frame.sunColor * frame.sunIntensity * NdotL * shadow;

    vec3 R = reflect(-V, s.N);
    vec3 irradiance;
    vec3 radiance;
    vec3 envSpec;

    if (frame.envIrradianceTex != 0u && frame.envPrefilterTex != 0u) {
        float lod = s.roughness * frame.envMaxLod;
        irradiance = sampleIrradiance(frame.envIrradianceTex, s.N) * frame.envIntensity;
        radiance   = samplePrefiltered(frame.envPrefilterTex, R, lod) * frame.envIntensity;
        envSpec    = envBRDF(F0, s.roughness, NdotV);
    } else {
        irradiance = mix(frame.groundColor, frame.skyColor, s.N.y * 0.5 + 0.5);
        radiance   = mix(frame.groundColor, frame.skyColor, R.y * 0.5 + 0.5);
        radiance   = mix(radiance, irradiance, s.roughness);
        envSpec    = EnvBRDFApprox(F0, s.roughness, NdotV);
    }

    float so = specularOcclusion(NdotV, s.occlusion, s.roughness) * horizonOcclusion(R, s.Ng);

    vec3 ambientDif = irradiance * cDiff * s.occlusion;
    vec3 ambientSpc = radiance * envSpec * so;
    vec3 ambient    = (ambientDif + ambientSpc) * frame.ambientIntensity;

    vec3 hdr = direct + ambient + s.emissive;
    if (frame.shadowDebug != 0u) {
        int k = pickCascade(frame, s.position);
        if (k >= 0) {
            hdr *= CASCADE_TINT[k];
        }
    }
    if (any(isnan(hdr)) || any(isinf(hdr))) hdr = vec3(0.0);
    return hdr;
}

#endif
