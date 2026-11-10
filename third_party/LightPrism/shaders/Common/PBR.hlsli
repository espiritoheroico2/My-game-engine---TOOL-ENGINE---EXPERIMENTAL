// Núcleo PBR (Cook-Torrance GGX). Espelha include/LightPrism/Reference/Brdf.hpp.
// A pré-exposição já vem aplicada nas intensidades pela CPU.
#ifndef LP_PBR_HLSLI
#define LP_PBR_HLSLI

#include "LightTypes.hlsli"

static const float LP_PI = 3.14159265359f;
static const float LP_MIN_ROUGHNESS = 0.045f;
static const float LP_MIN_DIST_SQ = 1e-4f;

struct SurfaceData {
    float3 N;          // normal normalizada
    float3 V;          // direção até a câmera, normalizada
    float3 albedo;
    float  metallic;
    float  roughness;  // perceptual [0,1]
};

float D_GGX(float NoH, float roughness) {
    float r  = max(roughness, LP_MIN_ROUGHNESS);
    float a  = r * r;
    float a2 = a * a;
    float d  = max(NoH * NoH * (a2 - 1.0f) + 1.0f, 1e-5f);
    return a2 / (LP_PI * d * d);
}

float V_SmithGGXCorrelated(float NoV, float NoL, float roughness) {
    float r  = max(roughness, LP_MIN_ROUGHNESS);
    float a2 = r * r * r * r;
    float ggxV = NoL * sqrt(NoV * NoV * (1.0f - a2) + a2);
    float ggxL = NoV * sqrt(NoL * NoL * (1.0f - a2) + a2);
    return 0.5f / max(ggxV + ggxL, 1e-5f);
}

float3 F_Schlick(float VoH, float3 F0) {
    float m  = saturate(1.0f - VoH);
    float m2 = m * m;
    return F0 + (1.0f - F0) * (m2 * m2 * m);
}

float DistanceAttenuation(float d, float R) {
    float r  = d / R;
    float r2 = r * r;
    float w  = saturate(1.0f - r2 * r2);
    return (w * w) / max(d * d, LP_MIN_DIST_SQ);
}

float SpotAttenuation(float cosAngle, float scale, float offset) {
    float t = saturate(cosAngle * scale + offset);
    return t * t;
}

// BRDF * NoL para uma luz já com radiância incidente `radiance` (cor * intensidade * atenuação).
float3 EvaluateBRDF(SurfaceData s, float3 L, float3 radiance) {
    float NoL = saturate(dot(s.N, L));
    if (NoL <= 0.0f) return 0.0f;

    float3 H   = normalize(s.V + L);
    float NoV  = max(dot(s.N, s.V), 1e-4f);
    float NoH  = saturate(dot(s.N, H));
    float VoH  = saturate(dot(s.V, H));

    float3 F0  = lerp(float3(0.04f, 0.04f, 0.04f), s.albedo, s.metallic);
    float3 F   = F_Schlick(VoH, F0);

    float3 specular = D_GGX(NoH, s.roughness) * V_SmithGGXCorrelated(NoV, NoL, s.roughness) * F;
    float3 kD       = (1.0f - F) * (1.0f - s.metallic);
    float3 diffuse  = kD * s.albedo / LP_PI;

    return (diffuse + specular) * radiance * NoL;
}

float3 EvaluatePointLight(PointLight l, SurfaceData s, float3 worldPos) {
    float3 toLight = l.position - worldPos;
    float  d = length(toLight);
    float3 L = toLight / max(d, 1e-4f);
    float3 radiance = l.color * l.intensity * DistanceAttenuation(d, l.radius);
    return EvaluateBRDF(s, L, radiance);
}

float3 EvaluateSpotLight(SpotLight l, SurfaceData s, float3 worldPos) {
    float3 toLight = l.position - worldPos;
    float  d = length(toLight);
    float3 L = toLight / max(d, 1e-4f);
    float  cosAngle = dot(-L, l.direction);
    float  att = DistanceAttenuation(d, l.radius) * SpotAttenuation(cosAngle, l.spotScale, l.spotOffset);
    return EvaluateBRDF(s, L, l.color * l.intensity * att);
}

float3 EvaluateDirectionalLight(DirectionalLight l, SurfaceData s) {
    return EvaluateBRDF(s, -l.direction, l.color * l.intensity);
}

#endif
