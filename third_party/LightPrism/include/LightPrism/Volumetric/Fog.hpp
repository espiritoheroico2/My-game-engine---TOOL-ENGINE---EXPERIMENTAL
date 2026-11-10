#pragma once
// Fase 6c — Neblina volumétrica em froxels (referência CPU). Espelha shaders/Volumetric/FogScatter.compute.hlsl.
//
// O frustum da câmera vira uma grade 3D de "froxels" (x,y de tela × profundidade EXPONENCIAL, como os slices dos clusters). Para cada
// coluna (x,y) o shader percorre os slices de perto para longe:
//   S      = luz espalhada no froxel = σs · Σ_luzes fase(cosθ) · radiância · visibilidade        (sol com sombra, pontuais, ambiente)
//   L     += T · (S − S·e^{−σt·Δt}) / σt            integral analítica no froxel: exata para S, σt constantes, sem passos extras
//   T     *= e^{−σt·Δt}
// e guarda (L, T) acumulados. O pixel só faz UMA leitura trilinear:  cor = cor·T + L. Custo independe da resolução da tela.
// LIMITE DE DISTÂNCIA: o volume cobre [near, maxDistance]; além disso a neblina é constante (nenhum froxel é avaliado).
#include <cstdint>
#include <functional>
#include <vector>

#include "LightPrism/Math.hpp"
#include "LightPrism/Mat4.hpp"
#include "LightPrism/Reference/Brdf.hpp"

namespace LightPrism::Fog {

struct FogConfig {
    float sigmaT = 0.02f;            // extinção (1/m) ao nível do solo (y = 0)
    Vec3 scatterAlbedo{0.9f, 0.9f, 0.9f};   // σs = σt · albedo (por canal)
    float heightFalloff = 0.0f;      // 1/m: σ(y) = σt · exp(−falloff · max(y,0)); 0 = neblina homogênea
    float anisotropy = 0.3f;         // g de Henyey–Greenstein (>0: espalha para frente: raios de luz visíveis contra o sol)
    Vec3 ambient{0, 0, 0};           // radiância ambiente uniforme (céu), espalhada isotropicamente
    float nearDistance = 0.1f;
    float maxDistance = 60.0f;       // metros: LIMITE DE DISTÂNCIA do volume
    uint32_t gridX = 80, gridY = 45, gridZ = 32;
};

struct FogCamera {
    Mat4 invView;                    // view -> mundo
    float proj00 = 1.0f, proj11 = 1.0f;
};

struct FogSun { Vec3 toLight; Vec3 radiance; };                    // toLight unitária (PARA o sol); radiância = cor·intensidade
struct FogPoint { Vec3 position; Vec3 radiance; float radius = 10.0f; };
using VisibilityFn = std::function<float(const Vec3& worldPos)>;   // visibilidade do sol num ponto do espaço (CSM, RT, SDF...)

// Fase de Henyey–Greenstein, normalizada (integra 1 na esfera).
inline float HenyeyGreenstein(float cosTheta, float g) {
    const float g2 = g * g;
    return (1.0f - g2) / (4.0f * kPi * std::pow(std::max(1.0f + g2 - 2.0f * g * cosTheta, 1e-4f), 1.5f));
}

inline float SliceEdge(const FogConfig& c, float k) {   // profundidade (view z) da borda k em [0, gridZ]
    return c.nearDistance * std::pow(c.maxDistance / c.nearDistance, k / float(c.gridZ));
}
inline float DensityAt(const FogConfig& c, const Vec3& p) {
    return c.sigmaT * (c.heightFalloff > 0.0f ? std::exp(-c.heightFalloff * std::max(p.y, 0.0f)) : 1.0f);
}

struct FogStats { uint64_t froxels = 0, sunVisibilityEvals = 0; };

struct FogVolume {
    FogConfig cfg;
    std::vector<Vec3> inscatter;    // (L) acumulado até a borda FAR de cada slice; x + gx*(y + gy*z)
    std::vector<float> trans;       // (T) idem
    uint64_t Bytes() const { return uint64_t(inscatter.size()) * 8; }   // RGBA16F na GPU: 8 B/froxel (L.rgb, T)
};

// Direção do raio da coluna (x,y) em view space (z = -1) e comprimento relativo.
inline Vec3 FogViewDir(const FogCamera& cam, float ndcX, float ndcY) { return {ndcX / cam.proj00, ndcY / cam.proj11, -1.0f}; }

inline void BuildFog(const FogConfig& cfg, const FogCamera& cam, const FogSun* sun, const VisibilityFn& sunVis, const std::vector<FogPoint>& points,
                     FogVolume& out, FogStats* stats = nullptr) {
    out.cfg = cfg;
    const size_t n = size_t(cfg.gridX) * cfg.gridY * cfg.gridZ;
    out.inscatter.assign(n, Vec3{});
    out.trans.assign(n, 1.0f);
    for (uint32_t y = 0; y < cfg.gridY; ++y)
        for (uint32_t x = 0; x < cfg.gridX; ++x) {
            const float ndcX = (float(x) + 0.5f) / float(cfg.gridX) * 2.0f - 1.0f, ndcY = 1.0f - (float(y) + 0.5f) / float(cfg.gridY) * 2.0f;
            const Vec3 dv = FogViewDir(cam, ndcX, ndcY);
            const float rayLen = Length(dv);                                   // t = d * rayLen
            const Vec3 worldDir = Normalize(cam.invView.TransformDirection(dv));
            Vec3 L{}; float T = 1.0f;
            for (uint32_t z = 0; z < cfg.gridZ; ++z) {
                const float d0 = SliceEdge(cfg, float(z)), d1 = SliceEdge(cfg, float(z + 1)), dm = std::sqrt(d0 * d1);
                const Vec3 p = cam.invView.TransformPoint(dv * dm);
                const float sigmaT = DensityAt(cfg, p), dt = (d1 - d0) * rayLen;
                const Vec3 sigmaS = cfg.scatterAlbedo * sigmaT;
                Vec3 radiance = cfg.ambient;                                    // fase isotrópica: ∫ L(ω)/(4π) dω = ambiente
                if (sun) {
                    const float vis = sunVis ? sunVis(p) : 1.0f;
                    if (stats) ++stats->sunVisibilityEvals;
                    radiance = radiance + sun->radiance * (HenyeyGreenstein(Dot(sun->toLight, worldDir), cfg.anisotropy) * vis);
                }
                for (const FogPoint& pl : points) {
                    const Vec3 d = pl.position - p;
                    const float dist = Length(d);
                    if (dist >= pl.radius) continue;
                    radiance = radiance + pl.radiance * (Reference::DistanceAttenuation(dist, pl.radius) * HenyeyGreenstein(Dot(d * (1.0f / std::max(dist, 1e-4f)), worldDir), cfg.anisotropy));
                }
                const Vec3 S = sigmaS * radiance;
                const float e = std::exp(-sigmaT * dt);
                if (sigmaT > 1e-9f) L = L + S * (T * (1.0f - e) / sigmaT);     // integral analítica de S·e^{-σt s} no froxel
                T *= e;
                const size_t idx = size_t(x) + size_t(cfg.gridX) * (y + size_t(cfg.gridY) * z);
                out.inscatter[idx] = L; out.trans[idx] = T;
                if (stats) ++stats->froxels;
            }
        }
}

// (L, T) para o pixel com `uv` em [0,1]² (origem no canto superior esquerdo) e profundidade de view `depth`.
// Interpola trilinear entre os froxels vizinhos (nós = borda FAR dos slices; a borda 0 vale (0, 1)).
struct FogSample { Vec3 L; float T = 1.0f; };
inline FogSample SampleFog(const FogVolume& v, float u, float vcoord, float depth) {
    const FogConfig& c = v.cfg;
    const float zc = std::log(std::max(depth, c.nearDistance) / c.nearDistance) / std::log(c.maxDistance / c.nearDistance) * float(c.gridZ);   // 0..gridZ
    const float zi = std::min(std::max(zc, 0.0f), float(c.gridZ));
    const float fx = u * float(c.gridX) - 0.5f, fy = vcoord * float(c.gridY) - 0.5f;
    const int x0 = int(std::floor(fx)), y0 = int(std::floor(fy));
    const float ax = fx - float(x0), ay = fy - float(y0);
    auto node = [&](int x, int y, int zEdge, Vec3& L, float& T) {   // zEdge em 0..gridZ (0 = antes do 1º slice)
        x = x < 0 ? 0 : (x >= int(c.gridX) ? int(c.gridX) - 1 : x);
        y = y < 0 ? 0 : (y >= int(c.gridY) ? int(c.gridY) - 1 : y);
        if (zEdge <= 0) { L = {}; T = 1.0f; return; }
        const size_t idx = size_t(x) + size_t(c.gridX) * (size_t(y) + size_t(c.gridY) * size_t(zEdge - 1));
        L = v.inscatter[idx]; T = v.trans[idx];
    };
    const int z0 = std::min(int(std::floor(zi)), int(c.gridZ) - 1);
    const float az = zi - float(z0);
    FogSample r; r.T = 0;
    for (int dz = 0; dz < 2; ++dz) for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) {
        const float w = (dx ? ax : 1 - ax) * (dy ? ay : 1 - ay) * (dz ? az : 1 - az);
        Vec3 L; float T; node(x0 + dx, y0 + dy, z0 + dz, L, T);
        r.L = r.L + L * w; r.T += T * w;
    }
    return r;
}

} // namespace LightPrism::Fog
