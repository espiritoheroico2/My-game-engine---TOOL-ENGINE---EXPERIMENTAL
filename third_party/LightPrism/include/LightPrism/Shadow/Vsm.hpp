#pragma once
// Fase 6a — Variance Shadow Maps (VSM), referência CPU. Espelha shaders/Common/Vsm.hlsli.
//
// Em vez de profundidade, o mapa guarda os momentos (E[d], E[d²]). Momentos podem ser filtrados (blur, mipmap, bilinear,
// anisotrópico) como uma textura qualquer; a desigualdade de Chebyshev de um lado dá a fração iluminada:
//       P(d_oclusor >= t) <= variância / (variância + (t - média)²)        (t > média)
// => sombra suave "de graça" com UMA amostra por pixel, sem PCF de 9–25 taps; custo = blur separável do mapa.
// Problema clássico: vazamento de luz (light bleeding) onde oclusores de profundidades diferentes se sobrepõem;
// `bleedReduction` remapeia a probabilidade (p - a)/(1 - a) e corta o vazamento (troca por sombras um pouco mais duras).
// Formato GPU: RG16F (4 B/texel) já basta com a profundidade em [0,1]; ao contrário do PCF, não é um depth buffer: precisa de
// um passe de conversão (depth -> momentos) e de 2 passes de blur.
#include <cstdint>
#include <vector>

#include "LightPrism/Shadow/Csm.hpp"

namespace LightPrism::Vsm {

struct VsmConfig {
    float minVariance = 1e-7f;     // piso da variância (em unidades de clip z²): evita acne onde o mapa é plano
    float bleedReduction = 0.3f;   // [0,1): 0 = Chebyshev puro (limite superior exato)
    uint32_t blurRadius = 2;       // texels (caixa de 2r+1)
};

struct VsmMap {
    uint32_t size = 0;
    std::vector<float> m1, m2;     // size*size
};

// Profundidade (1 = vazio, convenção do CSM) -> momentos. `slopeBias` soma 0.25*(dx²+dy²) ao 2º momento (derivadas por
// diferenças centrais): faz um plano inclinado ter variância suficiente e some com a acne sem bias de profundidade.
inline VsmMap VsmFromDepth(const float* depth, uint32_t size, bool slopeBias = true) {
    VsmMap m;
    m.size = size;
    m.m1.resize(size_t(size) * size);
    m.m2.resize(m.m1.size());
    auto D = [&](int x, int y) {
        x = x < 0 ? 0 : (x >= int(size) ? int(size) - 1 : x);
        y = y < 0 ? 0 : (y >= int(size) ? int(size) - 1 : y);
        return depth[size_t(y) * size + size_t(x)];
    };
    for (int y = 0; y < int(size); ++y)
        for (int x = 0; x < int(size); ++x) {
            const float d = D(x, y);
            float v = d * d;
            if (slopeBias) {
                const float dx = 0.5f * (D(x + 1, y) - D(x - 1, y)), dy = 0.5f * (D(x, y + 1) - D(x, y - 1));
                v += 0.25f * (dx * dx + dy * dy);
            }
            m.m1[size_t(y) * size + size_t(x)] = d;
            m.m2[size_t(y) * size + size_t(x)] = v;
        }
    return m;
}

// Blur separável por caixa de raio r (borda replicada). Soma acumulada: custo independente de r.
inline void VsmBlur(VsmMap& m, uint32_t r) {
    if (r == 0) return;
    const int n = int(m.size), R = int(r);
    std::vector<float> tmp(size_t(n) * n);
    auto pass = [&](std::vector<float>& a, bool horizontal) {
        for (int line = 0; line < n; ++line) {
            auto at = [&](int i) -> float& { i = i < 0 ? 0 : (i >= n ? n - 1 : i); return horizontal ? a[size_t(line) * n + i] : a[size_t(i) * n + line]; };
            auto out = [&](int i) -> float& { return horizontal ? tmp[size_t(line) * n + i] : tmp[size_t(i) * n + line]; };
            double acc = 0;
            for (int k = -R; k <= R; ++k) acc += at(k);
            for (int i = 0; i < n; ++i) {
                out(i) = float(acc / double(2 * R + 1));
                acc += double(at(i + R + 1)) - double(at(i - R));
            }
        }
        a = tmp;
    };
    for (std::vector<float>* a : {&m.m1, &m.m2}) { pass(*a, true); pass(*a, false); }
}

// Probabilidade de estar iluminado dado os momentos filtrados e a profundidade do receptor.
inline float VsmChebyshev(float mean, float meanSq, float t, const VsmConfig& c) {
    if (t <= mean) return 1.0f;                                   // receptor na frente (ou na) média: iluminado
    float var = meanSq - mean * mean;
    var = var < c.minVariance ? c.minVariance : var;
    const float d = t - mean;
    float p = var / (var + d * d);
    if (c.bleedReduction > 0.0f) p = Saturate((p - c.bleedReduction) / (1.0f - c.bleedReduction));
    return p;
}

// Amostra bilinear dos momentos (o hardware faz isso de graça) e avalia. (u,v) em texels contínuos.
inline float VsmSample(const VsmMap& m, float tx, float ty, float t, const VsmConfig& c) {
    const int n = int(m.size);
    const float fx = tx - 0.5f, fy = ty - 0.5f;
    const int x0 = int(std::floor(fx)), y0 = int(std::floor(fy));
    const float ax = fx - float(x0), ay = fy - float(y0);
    auto at = [&](const std::vector<float>& a, int x, int y) {
        x = x < 0 ? 0 : (x >= n ? n - 1 : x); y = y < 0 ? 0 : (y >= n ? n - 1 : y);
        return a[size_t(y) * size_t(n) + size_t(x)];
    };
    auto bil = [&](const std::vector<float>& a) {
        return (at(a, x0, y0) * (1 - ax) + at(a, x0 + 1, y0) * ax) * (1 - ay) + (at(a, x0, y0 + 1) * (1 - ax) + at(a, x0 + 1, y0 + 1) * ax) * ay;
    };
    return VsmChebyshev(bil(m.m1), bil(m.m2), t, c);
}

// Visibilidade de uma cascata do CSM (mesma projeção/bias de Reference::CsmSampleCascade).
inline float VsmSampleCascade(const CsmGPU& csm, const VsmMap& m, uint32_t k, const Vec3& worldPos, const Vec3& N, const VsmConfig& c) {
    const CsmCascadeGPU& cas = csm.cascades[k];
    const Vec3 p = worldPos + N * (csm.normalBias * cas.texelWorldSize);
    const Vec3 clip = cas.viewProj.TransformPoint(p);
    if (clip.x < -1.0f || clip.x > 1.0f || clip.y < -1.0f || clip.y > 1.0f || clip.z > 1.0f) return 1.0f;
    const float depth = clip.z - csm.depthBias / cas.depthRange;
    const float size = float(m.size);
    return VsmSample(m, (clip.x * 0.5f + 0.5f) * size, (0.5f - clip.y * 0.5f) * size, depth, c);
}

inline uint64_t VsmBytes(uint32_t size, bool rg16f = true) { return uint64_t(size) * size * (rg16f ? 4u : 8u); }

} // namespace LightPrism::Vsm
