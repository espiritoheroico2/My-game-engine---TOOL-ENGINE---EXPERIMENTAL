#pragma once
// Efeitos por ray tracing sobre o BVH (Fase 5b): sombra do sol e AO, ambos com LIMITE DE DISTÂNCIA.
// Espelha shaders/Rt/RtEffects.hlsli. Independente de API: recebe posição/normal do mundo (do G-buffer).
//
// Limite de distância: `maxDistance` vira Ray::tMax. Subárvores além dele nunca são visitadas, então o custo por raio
// cai com o limite; além do limite a sombra/oclusão some (use o shadow map/CSM para o resto: híbrido).
#include <cmath>
#include <cstdint>

#include "LightPrism/Math.hpp"
#include "LightPrism/Rt/Bvh.hpp"

namespace LightPrism::Rt {

struct RtShadowConfig {
    float maxDistance = 30.0f;   // metros: oclusores além disso são ignorados
    float coneHalfAngle = 0.0f;  // rad; > 0 = sombra suave (disco solar ~0.0047 rad real; use 0.02–0.08 p/ ficar visível)
    uint32_t samples = 1;        // raios por pixel (1 = sombra dura)
    float bias = 2e-3f;          // metros ao longo da normal
};

struct RtAoConfig {
    float radius = 0.6f;         // metros = limite de distância do AO
    uint32_t rays = 16;
    float bias = 1e-3f;
};

namespace detail {
inline float RadicalInverse(uint32_t bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10f;
}
inline void Basis(const Vec3& n, Vec3& t, Vec3& b) {
    const Vec3 up = std::fabs(n.y) < 0.99f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    t = Normalize(Cross(up, n));
    b = Cross(n, t);
}
inline float PixelRotation(uint32_t pixelIndex) { return float((pixelIndex * 2654435761u) >> 8) / float(1u << 24); }
} // namespace detail

// Visibilidade [0,1] da luz direcional. `toLight` = direção PARA a luz (unitária). `pixelIndex` decorrelaciona o padrão.
inline float RtSunVisibility(const Bvh& bvh, const Vec3& pos, const Vec3& n, const Vec3& toLight, const RtShadowConfig& c,
                             uint32_t pixelIndex = 0, TraversalStats* st = nullptr) {
    if (Dot(n, toLight) <= 0.0f) return 0.0f;  // de costas para a luz: o N.L já zera
    Ray r;
    r.origin = pos + n * c.bias;
    r.tMax = c.maxDistance;
    if (c.coneHalfAngle <= 0.0f || c.samples <= 1) {
        r.dir = toLight;
        return bvh.Occluded(r, st) ? 0.0f : 1.0f;
    }
    Vec3 t, b;
    detail::Basis(toLight, t, b);
    const float rot = detail::PixelRotation(pixelIndex), tanA = std::tan(c.coneHalfAngle);
    uint32_t vis = 0;
    for (uint32_t k = 0; k < c.samples; ++k) {
        const float u1 = std::fmod((float(k) + 0.5f) / float(c.samples) + rot, 1.0f);
        const float rad = std::sqrt(detail::RadicalInverse(k)) * tanA, phi = 2.0f * kPi * u1;
        r.dir = Normalize(toLight + t * (rad * std::cos(phi)) + b * (rad * std::sin(phi)));
        if (!bvh.Occluded(r, st)) ++vis;
    }
    return float(vis) / float(c.samples);
}

// AO com o MESMO estimador da referência do SSAO: raios ponderados por cosseno, oclusão linear 1 - t/radius.
inline float RtAmbientOcclusion(const Bvh& bvh, const Vec3& pos, const Vec3& n, const RtAoConfig& c, uint32_t pixelIndex = 0,
                                TraversalStats* st = nullptr) {
    Vec3 t, b;
    detail::Basis(n, t, b);
    const float rot = detail::PixelRotation(pixelIndex);
    Ray r;
    r.origin = pos + n * c.bias;
    r.tMax = c.radius;
    float occ = 0.0f;
    for (uint32_t k = 0; k < c.rays; ++k) {
        const float u1 = std::fmod((float(k) + 0.5f) / float(c.rays) + rot, 1.0f), u2 = detail::RadicalInverse(k);
        const float phi = 2.0f * kPi * u1, s = std::sqrt(u2), ct = std::sqrt(1.0f - u2);
        r.dir = t * (s * std::cos(phi)) + b * (s * std::sin(phi)) + n * ct;
        Hit h;
        if (bvh.Intersect(r, h, st)) occ += 1.0f - h.t / c.radius;
    }
    return 1.0f - occ / float(c.rays);
}

} // namespace LightPrism::Rt
