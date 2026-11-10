#pragma once
// Fase 6d — Grade de probes de iluminação global (estilo DDGI simplificado), por raios no BVH.
//
// Cada probe lança N raios por atualização (direções de Fibonacci com rotação aleatória por quadro), calcula a radiância que chega
// (céu se o raio escapa, superfície iluminada pelo sol + indireto da própria grade se acerta) e guarda a IRRADIÂNCIA em SH9 já
// convoluída com o cosseno (27 floats). A mistura com histerese (h ≈ 0.9) acumula milhares de raios ao longo dos quadros com poucos
// raios por quadro; com `budget` só uma fatia dos probes é atualizada por quadro (custo previsível, atualização em round-robin).
// Recepção de luz: 8 probes vizinhos com pesos trilineares × face (n·dir) × visibilidade. A visibilidade usa momentos (média e
// média dos quadrados) da distância de acerto num mapa octaédrico 4x4 por probe (raios pesados por cos^32 até o centro do texel) e
// Chebyshev — uma versão compacta do mapa de profundidade do DDGI — para a luz não vazar através de paredes finas.
// Invalidação regional: um objeto que se move marca como inválidos só os probes até `maxDistance` dele (o próximo update os substitui
// sem histerese). Mesma ideia do PRT, mas com iluminação dinâmica (céu/sol) e múltiplos rebatimentos.
// NÃO guarda a luz direta do sol sobre o receptor: essa vem do CSM/RT; o probe cobre céu + luz rebatida (difusa).
#include <cstdint>
#include <functional>
#include <vector>

#include "LightPrism/Ibl/Ibl.hpp"
#include "LightPrism/Rt/Bvh.hpp"

namespace LightPrism::Rt {

struct ProbeGridDesc {
    Vec3 origin;                 // posição do probe (0,0,0)
    float spacing = 2.0f;        // metros
    uint32_t nx = 8, ny = 4, nz = 8;
    uint32_t raysPerProbe = 64;
    float hysteresis = 0.9f;     // peso do histórico [0,1)
    float maxDistance = 60.0f;   // LIMITE DE DISTÂNCIA dos raios; sem acerto = céu
    float normalBias = 0.02f;
    float invalidateRadius = 0.0f;   // alcance da invalidação regional (0 = maxDistance); menor = mais barato, mas ignora mudanças longínquas
};

struct ProbeEnvironment {
    const std::vector<Triangle>* triangles = nullptr;          // ordem ORIGINAL passada ao Bvh::Build
    std::function<Vec3(uint32_t tri)> albedo;                  // albedo difuso do triângulo
    Ibl::EnvFn sky;                                            // radiância do céu (sem o disco do sol)
    Vec3 sunToLight{0, 1, 0};                                  // unitária
    Vec3 sunIrradiance{0, 0, 0};                               // iluminância perpendicular ao sol (rgb)
    bool multiBounce = true;                                   // realimenta o indireto da própria grade
};

struct ProbeStats { uint64_t raysCast = 0; uint32_t updatedLast = 0; };

class ProbeGrid {
public:
    void Init(const ProbeGridDesc& d);
    uint32_t Count() const { return d_.nx * d_.ny * d_.nz; }
    const ProbeGridDesc& Desc() const { return d_; }
    Vec3 Position(uint32_t i) const { return d_.origin + Vec3{float(i % d_.nx), float((i / d_.nx) % d_.ny), float(i / (d_.nx * d_.ny))} * d_.spacing; }

    // Atualiza até `budget` probes (0 = todos), em round-robin; devolve quantos.
    uint32_t Update(const Bvh& bvh, const ProbeEnvironment& env, uint32_t budget = 0);
    // Marca como inválidos (próxima atualização sem histerese) os probes a menos de maxDistance da caixa.
    uint32_t InvalidateRegion(const Aabb& changed);
    void InvalidateAll();

    // Irradiância/pi (multiplique pelo albedo) em `p` com normal `n`.
    Vec3 Irradiance(const Vec3& p, const Vec3& n, bool useVisibility = true) const;

    const ProbeStats& Stats() const { return stats_; }
    uint64_t Bytes(bool fp16 = true) const { return uint64_t(Count()) * ((27 + 32) * (fp16 ? 2u : 4u)); }
    uint32_t InvalidCount() const { uint32_t n = 0; for (uint8_t v : valid_) n += v ? 0u : 1u; return n; }

private:
    static constexpr int kVisTexels = 16;   // mapa octaédrico 4x4 de momentos de distância
    struct Probe { Ibl::Sh9 irr; float m1[16], m2[16]; };
    void UpdateOne(const Bvh& bvh, const ProbeEnvironment& env, uint32_t i);
    ProbeGridDesc d_;
    std::vector<Probe> probes_;
    std::vector<uint8_t> valid_;
    std::vector<Vec3> dirs_;
    uint32_t cursor_ = 0, frame_ = 0;
    ProbeStats stats_;
};

} // namespace LightPrism::Rt
