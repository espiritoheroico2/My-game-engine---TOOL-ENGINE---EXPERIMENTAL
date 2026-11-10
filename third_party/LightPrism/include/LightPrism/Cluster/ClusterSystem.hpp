#pragma once
// Orquestra o clustered culling da Fase 2: cria buffers/pipelines pelo RHI, dispara os 5 passes de compute
// com barreiras e expõe LightGrid/LightIndexList/Grid para o passe de iluminação da engine.
// Sem compute (ou se algum shader falhar ao compilar), cai no culling por CPU e sobe os mesmos buffers.
//
// Uso por frame:
//   const FrameLightView& v = lights.BuildFrame(fp);
//   lights.UploadToGPU(device);
//   clusters.Execute(device, lights, v, MakeClusterCamera(view, proj, w, h, near, far));
//   // ... passe de iluminação lê GridBuffer()/LightGridBuffer()/LightIndexBuffer() (ClusterLighting.hlsli)
//   // ... alguns frames depois: clusters.ReadbackStats(stats) para detectar overflow (opcional).
#include <cstdint>
#include <memory>
#include <string>

#include "LightPrism/Cluster/ClusterCulling.hpp"
#include "LightPrism/LightingSystem.hpp"
#include "LightPrism/Mat4.hpp"

namespace LightPrism {

enum class ClusterMode : uint8_t { Auto, Gpu, Cpu };

struct ClusterSystemDesc {
    // Raiz dos shaders HLSL (a pasta `shaders/` da Light Prism, instalada junto ao executável).
    std::string shaderDirectory = "shaders";
    uint32_t tileSizePx = 16;
    uint32_t zSlices = 24;
    uint32_t maxLightsPerCluster = 64;
    uint32_t indicesPerCluster = 8; // orçamento médio da lista global (maxIndices = clusters * isto)
    ClusterMode mode = ClusterMode::Auto;
    // Orçamento adaptativo da lista de índices (memória): cresce sozinho se houver overflow (x1,25 do pedido) e encolhe
    // para x1,5 do uso real depois de `shrinkAfterFrames` frames com uso < 50% do orçamento. Precisa de ReadbackStats válido.
    bool adaptiveIndexBudget = false;
    uint32_t minIndicesPerCluster = 1;
    uint32_t maxIndicesPerCluster = 64;
    uint32_t shrinkAfterFrames = 30;
};

struct ClusterCamera {
    Mat4 view;
    uint32_t screenWidth = 1920;
    uint32_t screenHeight = 1080;
    float proj00 = 1.0f; // P[0][0]
    float proj11 = 1.0f; // P[1][1]
    float zNear = 0.1f;
    float zFar = 500.0f;
};

// Extrai proj00/proj11 de qualquer projeção em perspectiva (Z reverso ou não) com a convenção do Mat4.
inline ClusterCamera MakeClusterCamera(const Mat4& view, const Mat4& proj, uint32_t w, uint32_t h, float zNear,
                                       float zFar) {
    ClusterCamera c;
    c.view = view; c.screenWidth = w; c.screenHeight = h;
    c.proj00 = proj.m[0][0]; c.proj11 = proj.m[1][1];
    c.zNear = zNear; c.zFar = zFar;
    return c;
}

struct ClusterStats {
    bool valid = false;               // false: dado ainda indisponível (GPU não terminou) ou readback falhou
    uint64_t requestedIndices = 0;    // soma das contagens (já com teto por cluster)
    uint32_t maxIndices = 0;
    bool overflow = false;            // requestedIndices > maxIndices: alguns clusters perderam luzes
};

class ClusterSystem {
public:
    explicit ClusterSystem(const ClusterSystemDesc& desc = {});
    ~ClusterSystem();
    ClusterSystem(ClusterSystem&&) noexcept;
    ClusterSystem& operator=(ClusterSystem&&) noexcept;
    ClusterSystem(const ClusterSystem&) = delete;
    ClusterSystem& operator=(const ClusterSystem&) = delete;

    // Atualiza LightGrid/LightIndexList para este frame. `lights` precisa ter feito UploadToGPU(device) com
    // `view` (modo GPU lê os buffers de luz dele; modo CPU lê os dados de `view`). false em falha (ver LastError()).
    bool Execute(IGraphicsDevice& device, const LightingSystem& lights, const FrameLightView& view,
                 const ClusterCamera& camera);

    // Modo efetivamente em uso depois do último Execute (Auto resolve para Gpu ou Cpu).
    ClusterMode ActiveMode() const;
    const std::string& LastError() const;       // vazio se nada falhou
    const std::string& FallbackReason() const;  // por que caiu no CPU (vazio se não caiu)

    // Memória (bytes) dos buffers de cluster alocados no momento (counts + grid + índices + somas).
    uint64_t AllocatedBytes() const;
    const ClusterGridGPU& Grid() const;         // valores atuais (já com numPoint/numSpot do último frame)

    // Buffers para o passe de iluminação (nulos antes do primeiro Execute bem-sucedido).
    IGpuBuffer* GridBuffer() const;             // ClusterGridGPU (cbuffer)
    IGpuBuffer* LightGridBuffer() const;        // ClusterCell[clusterCount]
    IGpuBuffer* LightIndexBuffer() const;       // uint[maxIndices]

    // Modo CPU: imediato. Modo GPU: lê o `Stats` copiado no Execute anterior; chame alguns frames depois.
    bool ReadbackStats(ClusterStats& out) const;

    // Só para testes/depuração.
    IGpuBuffer* DebugStatsBuffer() const;
    IGpuBuffer* DebugCountsBuffer() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace LightPrism
