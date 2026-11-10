#pragma once
// Fase 4a — Iluminação baseada em imagem (IBL), referência CPU. Espelha shaders/Common/Ibl.hlsli.
//
//   difuso   : SH de 3ª ordem (9 coeficientes por canal) já convoluído com o cosseno; shader = polinômio.
//   especular: split-sum (Karis 2013) = ambiente pré-filtrado por rugosidade (cubemap com mips) * (F0*A + B),
//              onde (A,B) vem de uma LUT 2D da BRDF (ou da aproximação analítica para GPUs fracas).
//
// Tudo aqui é independente de API gráfica. A engine só precisa subir `PrefilteredEnv` (cubemap com mips) e
// `BrdfLut` (textura RG) e preencher o cbuffer `IblGPU`.
#include <cstdint>
#include <functional>
#include <vector>

#include "LightPrism/Math.hpp"
#include "LightPrism/Reference/Brdf.hpp"

namespace LightPrism::Ibl {

// Radiância do ambiente (linear HDR) numa direção unitária do mundo.
using EnvFn = std::function<Vec3(const Vec3& dir)>;

// ---------------------------------------------------------------------------------------------
// Harmônicos esféricos de grau 2
// ---------------------------------------------------------------------------------------------
// Ordem: 0:Y00  1:Y1-1(y)  2:Y10(z)  3:Y11(x)  4:Y2-2(xy)  5:Y2-1(yz)  6:Y20(3z²-1)  7:Y21(xz)  8:Y22(x²-y²)
struct Sh9 {
    Vec3 c[9];
};

void ShBasis(const Vec3& d, float out[9]);
Sh9 ProjectToSh(const EnvFn& env, uint32_t resolution = 128);  // integra em res x 2*res células de mesma área
Sh9 ConvolveCosine(const Sh9& radianceSh);                     // devolve SH de E(n)/pi (multiplique por albedo)
Vec3 EvalSh(const Sh9& sh, const Vec3& n, int bands = 3);       // max(0, soma); bands = 2 usa só L0+L1 (4 coeficientes, mobile)

// Layout GPU: coeficientes já multiplicados pelas constantes da base, para o shader só somar polinômios.
constexpr uint32_t IblFlag_None = 0;
constexpr uint32_t IblFlag_ShL1 = 1u << 0;  // o shader só soma as bandas L0+L1 (4 coeficientes): menos ALU no mobile

struct alignas(16) IblGPU {
    float sh[9][4];          // rgb + 1 float livre
    float intensity;         // multiplicador do ambiente (exposição já aplicada pelo chamador)
    float specularMaxMip;    // mipCount - 1
    uint32_t flags;
    float pad;
};
static_assert(sizeof(IblGPU) == 160, "IblGPU deve ter 160 bytes (espelha IBLData no HLSL)");

IblGPU MakeIblGPU(const Sh9& convolved, float intensity, uint32_t mipCount, uint32_t flags = 0);
Vec3 EvaluateIrradiancePacked(const IblGPU& g, const Vec3& n);  // mesma conta do HLSL

// ---------------------------------------------------------------------------------------------
// Cubemap (convenção D3D: +X -X +Y -Y +Z -Z; linha de texel cresce para baixo)
// ---------------------------------------------------------------------------------------------
struct Cubemap {
    uint32_t size = 0;
    std::vector<Vec3> face[6];
    const Vec3& Texel(uint32_t f, uint32_t x, uint32_t y) const { return face[f][size_t(y) * size + x]; }
};

Vec3 CubeFaceUVToDir(uint32_t face, float a, float b);                       // a,b em [-1,1], b para cima
void CubeDirToFaceUV(const Vec3& dir, uint32_t& face, float& a, float& b);
Vec3 SampleCubemap(const Cubemap& cm, const Vec3& dir);                      // bilinear dentro da face

// Mip i guarda o ambiente filtrado com rugosidade perceptual i/(mipCount-1). Mip 0 = ambiente original.
struct PrefilteredEnv {
    uint32_t mipCount = 0;
    std::vector<Cubemap> mips;
};

PrefilteredEnv BuildPrefilteredEnv(const EnvFn& env, uint32_t baseSize = 64, uint32_t mipCount = 6, uint32_t samples = 256);
Vec3 SamplePrefiltered(const PrefilteredEnv& pe, const Vec3& dir, float roughness);

// ---------------------------------------------------------------------------------------------
// LUT da BRDF (split-sum): A = escala de F0, B = bias. Eixo x = NoV, eixo y = rugosidade perceptual.
// ---------------------------------------------------------------------------------------------
struct BrdfLut {
    uint32_t size = 0;
    std::vector<float> ab;  // 2 floats por texel
};

void IntegrateBrdf(float NoV, float roughness, uint32_t samples, float& A, float& B);
BrdfLut BuildBrdfLut(uint32_t size = 32, uint32_t samples = 512);
void SampleBrdfLut(const BrdfLut& lut, float NoV, float roughness, float& A, float& B);  // bilinear

// ---------------------------------------------------------------------------------------------
// Formatos compactos (otimização 6): o cubemap pré-filtrado em RGB32F gasta 12 B/texel; R11G11B10F e RGB9E5 gastam 4 B
// (-67%), a LUT em RG8 gasta 2 B/texel (vs 8 B em RG32F). A engine sobe esses buffers direto como textura.
// ---------------------------------------------------------------------------------------------
enum class HdrFormat : uint8_t { RGB9E5, R11G11B10F };

uint32_t PackRGB9E5(const Vec3& c);
Vec3 UnpackRGB9E5(uint32_t v);
uint32_t PackR11G11B10F(const Vec3& c);
Vec3 UnpackR11G11B10F(uint32_t v);

struct PackedPrefiltered {
    HdrFormat format = HdrFormat::RGB9E5;
    struct Mip { uint32_t size = 0; std::vector<uint32_t> face[6]; };
    std::vector<Mip> mips;
    uint64_t Bytes() const;
};
PackedPrefiltered PackPrefiltered(const PrefilteredEnv& pe, HdrFormat fmt);
PrefilteredEnv UnpackPrefiltered(const PackedPrefiltered& p);
uint64_t PrefilteredBytes(const PrefilteredEnv& pe);   // versão RGB32F

struct PackedBrdfLut {
    uint32_t size = 0;
    std::vector<uint8_t> rg;                           // RG8 UNORM
    uint64_t Bytes() const { return rg.size(); }
};
PackedBrdfLut PackBrdfLut(const BrdfLut& lut);
BrdfLut UnpackBrdfLut(const PackedBrdfLut& p);

// ---------------------------------------------------------------------------------------------
// Avaliação completa
// ---------------------------------------------------------------------------------------------
struct IblData {
    Sh9 irradiance;           // já convoluído (ConvolveCosine)
    PrefilteredEnv prefiltered;
    BrdfLut lut;
    float intensity = 1.0f;
};

IblData BuildIblData(const EnvFn& env, uint32_t shResolution = 96, uint32_t cubeSize = 64, uint32_t mipCount = 6,
                     uint32_t lutSize = 32);

struct IndirectResult {
    Vec3 diffuse;
    Vec3 specular;
    Vec3 Total() const { return diffuse + specular; }
};

// Oclusão especular (Lagarde): AO de difusa não escurece o especular da mesma forma.
// Fator de mistura N->R para amostrar o ambiente pré-filtrado (Lagarde).
float DominantDirFactor(float roughness);

float SpecularOcclusion(float NoV, float ao, float roughness);

// `ao` em [0,1] (1 = sem oclusão). Modula o difuso por `ao` e o especular por SpecularOcclusion.
IndirectResult EvaluateIndirect(const IblData& ibl, const Reference::SurfaceData& s, float ao = 1.0f);

// Verdade numérica: integra BRDF * L_env * NoL na esfera inteira (res x 2*res células). Serve para medir o erro do
// split-sum + SH. O custo é alto: use só em testes e ferramentas.
Vec3 IntegrateIndirectReference(const EnvFn& env, const Reference::SurfaceData& s, uint32_t resolution = 512);

// Céu procedural suave (gradiente + lobo do sol). `sunDir` aponta PARA o sol.
struct ProceduralSky {
    Vec3 zenith{0.20f, 0.38f, 0.80f};
    Vec3 horizon{0.85f, 0.80f, 0.72f};
    Vec3 ground{0.10f, 0.085f, 0.07f};
    Vec3 sunDir{0.4f, 0.6f, 0.3f};
    Vec3 sunColor{1.0f, 0.9f, 0.7f};
    float sunPower = 24.0f;      // expoente do lobo
    float sunIntensity = 3.0f;
    Vec3 Radiance(const Vec3& dir) const;
};

} // namespace LightPrism::Ibl
