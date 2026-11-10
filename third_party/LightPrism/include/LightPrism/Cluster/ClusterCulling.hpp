#pragma once
// Referência CPU do culling em clusters. Serve para:
//  1. validar a matemática (os shaders de compute espelham estas funções);
//  2. fallback em GPUs/APIs sem compute shaders (produz os mesmos buffers LightGrid + LightIndexList).
//
// Algoritmo em 2 passes com prefix sum (sem arrays de tamanho fixo por cluster):
//   contar (com teto por cluster) -> scan exclusivo -> preencher índices.
// Ordem das luzes = ordem dos buffers (pontos, depois spots; cada grupo já vem em ordem de prioridade),
// então quando um cluster estoura o teto, ficam as luzes de maior prioridade.
#include <cstdint>
#include <vector>

#include "LightPrism/Cluster/ClusterTypes.hpp"
#include "LightPrism/Mat4.hpp"

namespace LightPrism {

// --- Geometria -------------------------------------------------------------------------------
ClusterAABB BuildClusterAABB(const ClusterGridGPU& g, uint32_t tx, uint32_t ty, uint32_t tz);
std::vector<ClusterAABB> BuildAllClusterAABBs(const ClusterGridGPU& g);

ClusterLightGPU PrepareClusterLight(const PointLightGPU& l, const ClusterGridGPU& g, const Mat4& view);
ClusterLightGPU PrepareClusterLight(const SpotLightGPU& l, const ClusterGridGPU& g, const Mat4& view);

bool SphereIntersectsAABB(const Vec3& center, float radius, const Vec3& bmin, const Vec3& bmax);
bool ConeIntersectsSphere(const ClusterLightGPU& spot, const Vec3& center, float radius);
bool LightIntersectsAABB(const ClusterLightGPU& l, const ClusterAABB& aabb);

// --- Culling ---------------------------------------------------------------------------------
struct ClusterCullResult {
    std::vector<ClusterCell> cells;   // um por cluster
    std::vector<uint32_t> indices;    // índices de luz (pontos: [0,np); spots: np + i)
    uint64_t requestedIndices = 0;    // soma das contagens (já com teto por cluster)
    bool overflow = false;            // requestedIndices > maxIndices (alguns clusters perderam luzes)
};

// Versão eficiente: cada luz só visita os clusters da sua faixa.
void CullLightsCPU(const ClusterGridGPU& grid, const std::vector<ClusterAABB>& aabbs, const Mat4& view,
                   const PointLightGPU* points, uint32_t numPoints,
                   const SpotLightGPU* spots, uint32_t numSpots, ClusterCullResult& out,
                   const uint8_t* activeMask = nullptr);

// Marca os clusters que algum pixel realmente usa (tile do pixel + fatia da profundidade). Com `activeMask` no
// culling, clusters vazios (atrás de paredes, céu, longe demais) nem são testados.
// `viewDepth` = distância linear de view por pixel (0 = sem geometria). Devolve quantos clusters ficaram ativos.
uint32_t MarkActiveClusters(const ClusterGridGPU& grid, const float* viewDepth, uint32_t width, uint32_t height,
                            std::vector<uint8_t>& mask);

// Referência lenta (todos os clusters x todas as luzes), usada nos testes.
void CullLightsBruteForce(const ClusterGridGPU& grid, const std::vector<ClusterAABB>& aabbs, const Mat4& view,
                          const PointLightGPU* points, uint32_t numPoints,
                          const SpotLightGPU* spots, uint32_t numSpots, ClusterCullResult& out);

// --- Scan hierárquico (emula exatamente os 2 kernels HLSL de scan + a soma no passe de fill) ----
struct ScanResult {
    std::vector<uint32_t> offsets; // exclusivo
    uint64_t total = 0;
};
ScanResult HierarchicalExclusiveScan(const std::vector<uint32_t>& counts);

} // namespace LightPrism
