#pragma once
// Referência CPU da BRDF e atenuações. Espelha shaders/Common/PBR.hlsli e serve de "verdade
// matemática" para golden tests. Qualquer mudança no HLSL deve ser refletida aqui.
#include <algorithm>
#include <cmath>

#include "LightPrism/LightTypes.hpp"
#include "LightPrism/Math.hpp"

namespace LightPrism::Reference {

constexpr float kMinRoughness = 0.045f;
constexpr float kMinDistSq = 1e-4f;

// alpha = roughness²; NDF usa alpha² (= roughness⁴).
inline float D_GGX(float NoH, float roughness) {
    const float r = std::max(roughness, kMinRoughness);
    const float a = r * r;
    const float a2 = a * a;
    const float d = NoH * NoH * (a2 - 1.0f) + 1.0f;
    return a2 / (kPi * d * d);
}

// Smith-GGX correlacionado, já dividido por (4 NoL NoV): V = G / (4 NoL NoV).
inline float V_SmithGGXCorrelated(float NoV, float NoL, float roughness) {
    const float r = std::max(roughness, kMinRoughness);
    const float a2 = r * r * r * r;
    const float ggxV = NoL * std::sqrt(NoV * NoV * (1.0f - a2) + a2);
    const float ggxL = NoV * std::sqrt(NoL * NoL * (1.0f - a2) + a2);
    return 0.5f / std::max(ggxV + ggxL, 1e-5f);
}

inline Vec3 F_Schlick(float VoH, const Vec3& F0) {
    const float m = Saturate(1.0f - VoH);
    const float m2 = m * m;
    const float f = m2 * m2 * m;
    return F0 + (Vec3{1, 1, 1} - F0) * f;
}

// Janela de raio (zero exato em d = R) e piso de distância de 1 cm.
inline float DistanceAttenuation(float d, float R) {
    const float r = d / R;
    const float r2 = r * r;
    const float w = Saturate(1.0f - r2 * r2);
    return (w * w) / std::max(d * d, kMinDistSq);
}

inline float SpotAttenuation(float cosAngle, float scale, float offset) {
    const float t = Saturate(cosAngle * scale + offset);
    return t * t;
}

// ---------------------------------------------------------------------------------------------
// Avaliação completa de uma luz (espelha EvaluateBRDF / Evaluate*Light de PBR.hlsli).
// ---------------------------------------------------------------------------------------------
struct SurfaceData {
    Vec3 N;          // normal normalizada
    Vec3 V;          // direção até a câmera, normalizada
    Vec3 albedo;
    float metallic = 0.0f;
    float roughness = 0.5f;
};

// BRDF * NoL para radiância incidente `radiance` (cor * intensidade * atenuação).
inline Vec3 EvaluateBRDF(const SurfaceData& s, const Vec3& L, const Vec3& radiance) {
    const float NoL = Saturate(Dot(s.N, L));
    if (NoL <= 0.0f) return {0, 0, 0};
    const Vec3 H = Normalize(s.V + L);
    const float NoV = std::max(Dot(s.N, s.V), 1e-4f);
    const float NoH = Saturate(Dot(s.N, H));
    const float VoH = Saturate(Dot(s.V, H));

    const Vec3 F0 = Lerp(Vec3{0.04f, 0.04f, 0.04f}, s.albedo, s.metallic);
    const Vec3 F = F_Schlick(VoH, F0);
    const Vec3 specular = F * (D_GGX(NoH, s.roughness) * V_SmithGGXCorrelated(NoV, NoL, s.roughness));
    const Vec3 kD = (Vec3{1, 1, 1} - F) * (1.0f - s.metallic);
    const Vec3 diffuse = kD * s.albedo * (1.0f / kPi);
    return (diffuse + specular) * radiance * NoL;
}

inline Vec3 EvaluatePointLight(const PointLightGPU& l, const SurfaceData& s, const Vec3& worldPos) {
    const Vec3 toLight = l.position - worldPos;
    const float d = Length(toLight);
    const Vec3 L = toLight * (1.0f / std::max(d, 1e-4f));
    return EvaluateBRDF(s, L, l.color * (l.intensity * DistanceAttenuation(d, l.radius)));
}

inline Vec3 EvaluateSpotLight(const SpotLightGPU& l, const SurfaceData& s, const Vec3& worldPos) {
    const Vec3 toLight = l.position - worldPos;
    const float d = Length(toLight);
    const Vec3 L = toLight * (1.0f / std::max(d, 1e-4f));
    const float cosAngle = Dot(-L, l.direction);
    const float att = DistanceAttenuation(d, l.radius) * SpotAttenuation(cosAngle, l.spotScale, l.spotOffset);
    return EvaluateBRDF(s, L, l.color * (l.intensity * att));
}

inline Vec3 EvaluateDirectionalLight(const DirectionalLightGPU& l, const SurfaceData& s) {
    return EvaluateBRDF(s, -l.direction, l.color * l.intensity);
}

} // namespace LightPrism::Reference
