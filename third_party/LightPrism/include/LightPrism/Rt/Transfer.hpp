#pragma once
// Ray tracing COM CACHE (Fase 5c): PRT — Precomputed Radiance Transfer com vetor de transferência SH9 por amostra.
//
// Ideia: a iluminação do céu/ambiente em um ponto é  E/pi = (1/pi) * integral V(w) L(w) max(0,n.w) dw. V (visibilidade,
// custa raios) só depende da GEOMETRIA; L (o céu) muda com hora do dia, nuvens, rotação do HDRI. Projetando V*cos em
// SH9 uma vez (vetor T de 9 floats), a luz do céu vira  E/pi = (1/pi) * sum_k T_k * L_k  : 9 MADs por canal, ZERO raios.
//
//   - mudou o céu / a hora do dia ........ custo zero em raios (só os 9 L_k mudam);
//   - moveu um objeto .................... só as amostras a menos de `maxDistance` da caixa antiga/nova ficam sujas;
//   - o orçamento por quadro limita a re-bake (Bake(budget)), então o custo é previsível e amortizado em vários quadros;
//   - limite de distância: oclusores além de `maxDistance` não existem (o céu é visto); combine com o CSM/RT de sombras para longe.
// O bake é DETERMINÍSTICO por amostra: re-bake incremental == bake do zero (testado bit a bit).
#include <cstdint>
#include <vector>

#include "LightPrism/Ibl/Ibl.hpp"
#include "LightPrism/Rt/Bvh.hpp"

namespace LightPrism::Rt {

struct TransferSample {
    Vec3 position;
    Vec3 normal;  // unitária
};

struct TransferConfig {
    float maxDistance = 4.0f;   // metros: alcance dos raios de visibilidade (e raio de invalidação)
    uint32_t rays = 256;        // direções por amostra (Fibonacci na esfera)
    float bias = 2e-3f;         // metros ao longo da normal
};

// Direções quase uniformes na esfera (espiral de Fibonacci), n >= 1.
std::vector<Vec3> FibonacciSphere(uint32_t n);

struct TransferStats {
    uint32_t samples = 0, dirty = 0;
    uint64_t raysCast = 0;      // acumulado
    uint64_t bytes = 0;
};

class TransferCache {
public:
    void Init(std::vector<TransferSample> samples, const TransferConfig& cfg);

    // Re-bake de até `budget` amostras sujas (0 = todas). Devolve quantas foram processadas.
    uint32_t Bake(const Bvh& bvh, uint32_t budget = 0);

    // Marca como sujas as amostras cuja esfera de alcance toca `changed` (use a união das caixas ANTIGA e NOVA do objeto).
    uint32_t InvalidateRegion(const Aabb& changed);
    void InvalidateAll();

    // E/pi do céu (multiplique pelo albedo). `radiance` = ProjectToSh(céu) (SEM convolução).
    Vec3 Irradiance(uint32_t i, const Ibl::Sh9& radiance) const;

    const float* Transfer(uint32_t i) const { return &m_t[size_t(i) * 9]; }
    bool IsDirty(uint32_t i) const { return m_dirty[i] != 0; }
    uint32_t Count() const { return uint32_t(m_samples.size()); }
    const TransferSample& Sample(uint32_t i) const { return m_samples[i]; }
    const TransferConfig& Config() const { return m_cfg; }
    TransferStats Stats() const;

private:
    void BakeOne(const Bvh& bvh, uint32_t i);

    TransferConfig m_cfg;
    std::vector<TransferSample> m_samples;
    std::vector<float> m_t;          // 9 por amostra
    std::vector<uint8_t> m_dirty;
    std::vector<Vec3> m_dirs;
    std::vector<float> m_basis;      // 9 por direção (Y_k(w)), pré-calculado
    uint32_t m_cursor = 0;
    uint32_t m_dirtyCount = 0;
    uint64_t m_raysCast = 0;
};

} // namespace LightPrism::Rt
