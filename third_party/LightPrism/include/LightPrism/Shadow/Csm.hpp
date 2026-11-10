#pragma once
// Cascaded Shadow Maps para a luz direcional (Fase 3). Espelha shaders/Common/Shadow.hlsli.
//
// A biblioteca calcula as cascatas (matrizes, splits, bias) e sobe os dados; o DESENHO dos shadow maps é da engine
// hospedeira (o RHI não tem draw): para cada cascata i, renderize só profundidade com `cascades[i].viewProj`
// (convencional: perto = 0, longe = 1; NÃO é o Z reverso da câmera) numa Texture2DArray de `shadowMapSize`².
//
// Estabilidade (sem tremor ao mover/girar a câmera):
//  - cada cascata envolve a fatia do frustum numa ESFERA; o raio só depende de near/far/projeção, nunca da rotação;
//  - o raio é arredondado para cima em múltiplos de 1/16 m;
//  - o centro, em espaço de luz, é fixado numa grade de texels ("texel snapping"): um ponto fixo do mundo cai
//    sempre na mesma posição fracionária dentro do texel, independente da câmera.
//
// Convenção de texel: x_tex = (clip.x * 0.5 + 0.5) * size ; y_tex = (0.5 - clip.y * 0.5) * size (y para baixo).
#include <cmath>
#include <cstdint>
#include <memory>
#include <type_traits>

#include "LightPrism/LightingSystem.hpp"
#include "LightPrism/Mat4.hpp"
#include "LightPrism/Quality.hpp"

namespace LightPrism {

constexpr uint32_t kMaxCsmCascades = 4;

struct CsmConfig {
    uint32_t cascadeCount = 3;      // 1..4
    uint32_t shadowMapSize = 2048;  // por cascata; potência de 2 par
    float splitLambda = 0.7f;       // 0 = uniforme, 1 = logarítmico
    float shadowDistance = 0.0f;    // 0 = até o far da câmera
    float blendFraction = 0.1f;     // fração final de cada cascata que faz fade para a próxima (0 = sem blend)
    float casterExtension = 60.0f;  // metros além da esfera, em direção à luz, para incluir casters fora do frustum
    float depthBias = 0.05f;        // metros
    float normalBias = 1.5f;        // texels da cascata, ao longo da normal
    uint32_t pcfTaps = 4;           // 1, 4 ou 9
    bool snapToTexels = true;       // desligar só para depuração: causa tremor
};

inline CsmConfig MakeCsmConfig(const QualitySettings& q) {
    CsmConfig c;
    c.cascadeCount = q.csmCascades < 1 ? 1u : (q.csmCascades > kMaxCsmCascades ? kMaxCsmCascades : q.csmCascades);
    c.shadowMapSize = q.shadowMapResolution;
    c.pcfTaps = q.pcfTaps;
    return c;
}

// ---- Layout GPU (espelha CsmData em Shadow.hlsli) -------------------------------------------
struct alignas(16) CsmCascadeGPU {
    Mat4 viewProj;         // mundo -> clip da luz (row-major; HLSL: row_major float4x4)
    float splitNear;       // profundidade de view onde a cascata começa
    float splitFar;        // ... e termina
    float texelWorldSize;  // metros por texel
    float depthRange;      // metros entre o plano near e far da ortográfica (clip.z * depthRange = distância)
};
struct alignas(16) CsmGPU {
    CsmCascadeGPU cascades[kMaxCsmCascades];
    uint32_t cascadeCount;  // 0 = sombras desligadas
    float shadowMapSize;
    float depthBias;        // metros
    float normalBias;       // texels
    float blendFraction;
    uint32_t pcfTaps;
    float _pad0, _pad1;
};
static_assert(sizeof(CsmCascadeGPU) == 80, "CsmCascadeGPU");
static_assert(sizeof(CsmGPU) == 352, "CsmGPU");
static_assert(std::is_standard_layout<CsmGPU>::value && std::is_trivially_copyable<CsmGPU>::value, "CsmGPU");

struct CsmCameraInput {
    Mat4 view;              // mundo -> view
    float proj00 = 1.0f;    // P[0][0]
    float proj11 = 1.0f;    // P[1][1]
    float zNear = 0.1f;
    float zFar = 500.0f;
};

struct CsmResult {
    CsmGPU gpu{};
    Vec3 sphereCenter[kMaxCsmCascades]{};      // centro da esfera da fatia no mundo, SEM snapping
    float sphereRadiusTight[kMaxCsmCascades]{}; // raio que envolve a fatia (arredondado a 1/16 m), sem folga
    float sphereRadius[kMaxCsmCascades]{};      // metade da caixa ortográfica: tight + folga de 1 texel p/ o snapping
};

// Calcula as cascatas para o frame. `sunTravelDir` = direção em que os raios da luz viajam. cascadeCount = 0 se a
// entrada for inválida (far <= near, projeção <= 0).
CsmResult ComputeCsm(const CsmConfig& config, const CsmCameraInput& camera, const Vec3& sunTravelDir);

// ---- Referência CPU da amostragem (espelha CsmVisibility do HLSL; HLSL usa SampleCmp = PCF bilinear) ---------
struct CsmDepthMaps {
    const float* data[kMaxCsmCascades] = {};  // size*size floats por cascata; 1.0 = vazio
    uint32_t size = 0;
};

namespace Reference {

inline float CsmSampleCascade(const CsmGPU& csm, const CsmDepthMaps& maps, uint32_t k, const Vec3& worldPos, const Vec3& N) {
    const CsmCascadeGPU& c = csm.cascades[k];
    const Vec3 p = worldPos + N * (csm.normalBias * c.texelWorldSize);
    const Vec3 clip = c.viewProj.TransformPoint(p);
    if (clip.x < -1.0f || clip.x > 1.0f || clip.y < -1.0f || clip.y > 1.0f || clip.z > 1.0f) return 1.0f;
    const float depth = clip.z - csm.depthBias / c.depthRange;
    const float size = float(maps.size);
    const float tx = (clip.x * 0.5f + 0.5f) * size, ty = (0.5f - clip.y * 0.5f) * size;

    int n = 1;
    float off[9][2] = {{0, 0}};
    if (csm.pcfTaps >= 9) {
        n = 0;
        for (int j = -1; j <= 1; ++j)
            for (int i = -1; i <= 1; ++i) { off[n][0] = float(i); off[n][1] = float(j); ++n; }
    } else if (csm.pcfTaps >= 4) {
        n = 4;
        const float o[4][2] = {{-0.5f, -0.5f}, {0.5f, -0.5f}, {-0.5f, 0.5f}, {0.5f, 0.5f}};
        for (int i = 0; i < 4; ++i) { off[i][0] = o[i][0]; off[i][1] = o[i][1]; }
    }
    float lit = 0.0f;
    const int maxIdx = int(maps.size) - 1;
    for (int i = 0; i < n; ++i) {
        int ix = int(std::floor(tx + off[i][0])), iy = int(std::floor(ty + off[i][1]));
        ix = ix < 0 ? 0 : (ix > maxIdx ? maxIdx : ix);
        iy = iy < 0 ? 0 : (iy > maxIdx ? maxIdx : iy);
        if (depth <= maps.data[k][size_t(iy) * maps.size + size_t(ix)]) lit += 1.0f;
    }
    return lit / float(n);
}

// Visibilidade do sol [0,1] para um ponto; `viewDepth` = -viewPos.z. Fora do alcance das sombras: 1.
inline float CsmVisibility(const CsmGPU& csm, const CsmDepthMaps& maps, const Vec3& worldPos, const Vec3& N, float viewDepth,
                           uint32_t* cascadeOut = nullptr) {
    uint32_t k = 0;
    while (k < csm.cascadeCount && viewDepth > csm.cascades[k].splitFar) ++k;
    if (cascadeOut) *cascadeOut = k < csm.cascadeCount ? k : 255u;
    if (k >= csm.cascadeCount) return 1.0f;

    float vis = CsmSampleCascade(csm, maps, k, worldPos, N);
    const CsmCascadeGPU& c = csm.cascades[k];
    const float blendWidth = csm.blendFraction * (c.splitFar - c.splitNear);
    if (k + 1 < csm.cascadeCount && blendWidth > 0.0f && viewDepth > c.splitFar - blendWidth) {
        const float t = (viewDepth - (c.splitFar - blendWidth)) / blendWidth;
        vis = Lerp(vis, CsmSampleCascade(csm, maps, k + 1, worldPos, N), Saturate(t));
    }
    return vis;
}

} // namespace Reference

// ---- Sistema: calcula e sobe o cbuffer por frame -----------------------------------------------
class CsmSystem {
public:
    explicit CsmSystem(const CsmConfig& config = {});
    ~CsmSystem();
    CsmSystem(CsmSystem&&) noexcept;
    CsmSystem& operator=(CsmSystem&&) noexcept;
    CsmSystem(const CsmSystem&) = delete;
    CsmSystem& operator=(const CsmSystem&) = delete;

    void SetConfig(const CsmConfig& config);
    const CsmConfig& Config() const;

    // Sem sol, ou sol sem LightFlag_CastShadows, ou câmera inválida: cascadeCount = 0 (HLSL devolve visibilidade 1).
    // false se a criação/atualização do buffer falhar.
    bool Update(IGraphicsDevice& device, const FrameLightView& frame, const CsmCameraInput& camera);

    const CsmResult& Result() const;  // do último Update (para o passe de shadow da engine)
    IGpuBuffer* Buffer() const;       // CsmGPU (cbuffer, 352 bytes)

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace LightPrism
