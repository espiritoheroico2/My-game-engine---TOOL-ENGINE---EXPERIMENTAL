#pragma once
// Fase 4b — SSAO (oclusão ambiente em espaço de tela), referência CPU. Espelha shaders/PostProcess/Ssao.compute.hlsl.
//
// Entrada: profundidade LINEAR de view (distância ao longo de -z, 0 = fundo) e normais em view space. No HLSL a
// profundidade reverso-Z do depth buffer é linearizada com  d = P23 / (z + P22).
// Diferenças em relação ao esquema do documento original (Seção 24.3):
//   - sem textura de ruído: a rotação do kernel vem de um padrão 4x4 fixo (o blur de 4x4 o cancela);
//   - kernel com amostras em hemisfério ponderado por cosseno e distância uniforme: o estimador converge para o AO
//     por raios com atenuação linear  1 - mean(hit ? 1 - dist/radius : 0)  (é essa a referência dos testes);
//   - bias e raio em METROS; range check suave para não escurecer por objetos distantes em profundidade.
#include <cstdint>
#include <vector>

#include "LightPrism/Math.hpp"

namespace LightPrism::Ssao {

struct SsaoConfig {
    uint32_t kernelSize = 16;     // até kMaxKernel
    float radius = 0.5f;          // metros
    float bias = 0.025f;          // metros
    float power = 1.0f;           // contraste: ao = pow(ao, power)
    float blurDepthRel = 0.05f;   // blur bilateral: ignora vizinhos cuja profundidade difere mais que isso (relativo)
    float biasPerMeter = 0.01f;   // bias efetivo = bias + biasPerMeter * profundidade (o depth buffer fica grosseiro longe)
    float fadeStart = 20.0f;      // metros: a partir daqui o AO desbota para 1 (SSAO longe é só ruído)
    float fadeEnd = 40.0f;
};

constexpr uint32_t kMaxKernel = 32;

struct SsaoInput {
    uint32_t width = 0, height = 0;
    const float* viewDepth = nullptr;   // width*height, 0 = sem geometria
    const Vec3* viewNormal = nullptr;   // width*height, view space, unitárias
    float proj00 = 1.0f, proj11 = 1.0f; // elementos da projeção (simétrica)
};

// Kernel determinístico: `n` direções no hemisfério z>0 (ponderadas por cosseno), com comprimento em (0,1].
void BuildKernel(uint32_t n, Vec3* out);

// Ângulo de rotação do kernel do pixel (padrão 4x4).
float KernelRotation(uint32_t x, uint32_t y);

// AO bruto (ruidoso) em [0,1]; 1 = livre. Fundo = 1.
void ComputeSsaoRaw(const SsaoConfig& cfg, const SsaoInput& in, std::vector<float>& out);

// Blur bilateral 4x4 (janela -2..+1, a mesma do padrão de rotação).
void BilateralBlur(const SsaoConfig& cfg, const SsaoInput& in, const std::vector<float>& raw, std::vector<float>& out);

// Meia resolução (otimização 5): reduz profundidade/normal 2x2 (escolhe o pixel mais próximo da média, que preserva
// bordas), calcula SSAO em 1/4 dos pixels e sobe com upsample bilinear que respeita a profundidade. Entrada e saída em
// resolução cheia.
void ComputeSsaoHalf(const SsaoConfig& cfg, const SsaoInput& full, std::vector<float>& outFull);

inline void ComputeSsao(const SsaoConfig& cfg, const SsaoInput& in, std::vector<float>& out) {
    std::vector<float> raw;
    ComputeSsaoRaw(cfg, in, raw);
    BilateralBlur(cfg, in, raw, out);
}

// Posição de view do pixel (x,y) com profundidade d.
Vec3 ViewPosFromDepth(const SsaoInput& in, uint32_t x, uint32_t y, float d);

// Layout do cbuffer do SSAO (b4). Kernel em float4.
struct alignas(16) SsaoGPU {
    float kernel[kMaxKernel][4];
    float proj00, proj11, proj22, proj23;
    float invWidth, invHeight;
    uint32_t width, height;
    float radius, bias, power, blurDepthRel;
    uint32_t kernelSize;
    float biasPerMeter, fadeStart, fadeEnd;
};
static_assert(sizeof(SsaoGPU) == 576, "SsaoGPU deve espelhar SSAOData do HLSL");

SsaoGPU MakeSsaoGPU(const SsaoConfig& cfg, uint32_t width, uint32_t height, const float proj[4][4]);

} // namespace LightPrism::Ssao
