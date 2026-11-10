// Sombra do sol e AO por ray tracing com limite de distância (espelha include/LightPrism/Rt/RtEffects.hpp).
// NÃO COMPILADO — validar na engine. Requer Bvh.hlsli.
#ifndef LP_RT_EFFECTS_HLSLI
#define LP_RT_EFFECTS_HLSLI
#include "Bvh.hlsli"

float RtRadicalInverse(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10f;
}

void RtBasis(float3 n, out float3 t, out float3 b)
{
    float3 up = abs(n.y) < 0.99f ? float3(0, 1, 0) : float3(1, 0, 0);
    t = normalize(cross(up, n));
    b = cross(n, t);
}

float RtPixelRotation(uint pixelIndex) { return float((pixelIndex * 2654435761u) >> 8) / 16777216.0f; }

// toLight = direção PARA a luz. coneHalfAngle = 0 ou samples = 1: sombra dura.
float RtSunVisibility(float3 pos, float3 n, float3 toLight, float maxDistance, float coneHalfAngle, uint samples, float bias, uint pixelIndex)
{
    if (dot(n, toLight) <= 0.0f) return 0.0f;
    RtRay r; r.origin = pos + n * bias; r.tMin = 1e-4f; r.tMax = maxDistance; r.dir = toLight;
    if (coneHalfAngle <= 0.0f || samples <= 1) return BvhOccluded(r) ? 0.0f : 1.0f;
    float3 t, b; RtBasis(toLight, t, b);
    float rot = RtPixelRotation(pixelIndex), tanA = tan(coneHalfAngle);
    uint vis = 0;
    for (uint k = 0; k < samples; ++k)
    {
        float u1 = frac((float(k) + 0.5f) / float(samples) + rot);
        float rad = sqrt(RtRadicalInverse(k)) * tanA, phi = 6.28318530718f * u1;
        r.dir = normalize(toLight + t * (rad * cos(phi)) + b * (rad * sin(phi)));
        if (!BvhOccluded(r)) ++vis;
    }
    return float(vis) / float(samples);
}

// Mesmo estimador do SSAO de referência: cosseno ponderado, oclusão linear 1 - t/radius, radius = limite de distância.
float RtAmbientOcclusion(float3 pos, float3 n, float radius, uint rays, float bias, uint pixelIndex)
{
    float3 t, b; RtBasis(n, t, b);
    float rot = RtPixelRotation(pixelIndex);
    RtRay r; r.origin = pos + n * bias; r.tMin = 1e-4f; r.tMax = radius; r.dir = n;
    float occ = 0.0f;
    for (uint k = 0; k < rays; ++k)
    {
        float u1 = frac((float(k) + 0.5f) / float(rays) + rot), u2 = RtRadicalInverse(k);
        float phi = 6.28318530718f * u1, s = sqrt(u2), ct = sqrt(1.0f - u2);
        r.dir = t * (s * cos(phi)) + b * (s * sin(phi)) + n * ct;
        float h = BvhIntersect(r);
        if (h >= 0.0f) occ += 1.0f - h / radius;
    }
    return 1.0f - occ / float(rays);
}

// PRT: irradiância/pi do céu a partir do vetor de transferência baked (9 floats por amostra) e do SH de radiância (9 float3).
float3 PrtIrradiance(float t[9], float3 L[9])
{
    float3 e = 0;
    [unroll] for (int k = 0; k < 9; ++k) e += L[k] * t[k];
    return e * 0.31830988618f;
}

#endif
