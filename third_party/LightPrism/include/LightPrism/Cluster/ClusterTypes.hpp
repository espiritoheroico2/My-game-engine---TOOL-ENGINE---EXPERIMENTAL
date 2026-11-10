#pragma once
// Tipos do Clustered Shading (Fase 2). Espelham shaders/Common/Cluster.hlsli.
//
// Espaço de view: mão direita, câmera olha para -Z. "Profundidade" d = -z (positiva).
// Cluster (x, y, z): tile de tileSizePx pixels (y=0 no topo da tela) e fatia logarítmica de profundidade.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <type_traits>

#include "LightPrism/LightTypes.hpp"
#include "LightPrism/Quality.hpp"

namespace LightPrism {

constexpr uint32_t kClusterScanGroupSize = 256;  // deve bater com LP_SCAN_GROUP no HLSL
constexpr uint32_t kClusterScanSumsThreads = 1024;

struct ClusterCell {
    uint32_t offset;
    uint32_t count;
};
static_assert(sizeof(ClusterCell) == 8, "ClusterCell");

// AABB de um cluster em view space (float4 para casar com StructuredBuffer HLSL).
struct alignas(16) ClusterAABB {
    Vec3 minPoint; float _pad0;
    Vec3 maxPoint; float _pad1;
};
static_assert(sizeof(ClusterAABB) == 32, "ClusterAABB");

// Luz já em view space, com esfera envolvente e faixa de clusters candidatos (saída do passe "prep").
struct alignas(16) ClusterLightGPU {
    Vec3 boundCenter; float boundRadius;
    Vec3 apex;        float range;
    Vec3 dir;         float cosOuter;
    float sinOuter;   uint32_t kind;     // 0 = ponto, 1 = spot
    int32_t sliceMin; int32_t sliceMax;  // faixa vazia: sliceMin > sliceMax
    int32_t tileMinX; int32_t tileMinY; int32_t tileMaxX; int32_t tileMaxY;
};
static_assert(sizeof(ClusterLightGPU) == 80, "ClusterLightGPU");
static_assert(std::is_trivially_copyable<ClusterLightGPU>::value, "ClusterLightGPU");

struct alignas(16) ClusterGridGPU {
    uint32_t gridX, gridY, gridZ, tileSizePx;
    float zNear, zFar, sliceScale, sliceBias;
    float projScaleX, projScaleY, screenWidth, screenHeight;
    uint32_t numPointLights, numSpotLights, maxLightsPerCluster, maxIndices;
};
static_assert(sizeof(ClusterGridGPU) == 64, "ClusterGridGPU");

// Configuração em alto nível.
struct ClusterGridDesc {
    uint32_t screenWidth = 1920;
    uint32_t screenHeight = 1080;
    uint32_t tileSizePx = 16;
    uint32_t zSlices = 24;
    float zNear = 0.1f;
    float zFar = 500.0f;
    float proj00 = 1.0f; // P[0][0] da projeção (1 / (aspect * tan(fovY/2)))
    float proj11 = 1.0f; // P[1][1] da projeção (1 / tan(fovY/2))
    uint32_t maxLightsPerCluster = 64;
    uint32_t maxIndices = 0; // 0 = automático: 8 índices por cluster
};

inline ClusterGridGPU MakeClusterGrid(const ClusterGridDesc& d) {
    ClusterGridGPU g{};
    g.tileSizePx = std::max(d.tileSizePx, 1u);
    g.gridX = (d.screenWidth + g.tileSizePx - 1) / g.tileSizePx;
    g.gridY = (d.screenHeight + g.tileSizePx - 1) / g.tileSizePx;
    g.gridZ = std::max(d.zSlices, 1u);
    g.zNear = d.zNear;
    g.zFar = d.zFar;
    g.sliceScale = float(g.gridZ) / std::log2(d.zFar / d.zNear);
    g.sliceBias = -std::log2(d.zNear) * g.sliceScale;
    g.projScaleX = d.proj00;
    g.projScaleY = d.proj11;
    g.screenWidth = float(d.screenWidth);
    g.screenHeight = float(d.screenHeight);
    g.maxLightsPerCluster = d.maxLightsPerCluster;
    g.maxIndices = d.maxIndices ? d.maxIndices : g.gridX * g.gridY * g.gridZ * 8u;
    return g;
}

inline uint32_t ClusterCount(const ClusterGridGPU& g) { return g.gridX * g.gridY * g.gridZ; }

inline uint32_t ClusterIndexOf(const ClusterGridGPU& g, uint32_t x, uint32_t y, uint32_t z) {
    return x + y * g.gridX + z * g.gridX * g.gridY;
}

// Fatia de profundidade (log2 com escala/bias pré-calculados: um FMA no shader).
inline uint32_t SliceFromDepth(const ClusterGridGPU& g, float depth) {
    const float s = std::floor(std::log2(std::max(depth, g.zNear)) * g.sliceScale + g.sliceBias);
    return uint32_t(std::clamp(s, 0.0f, float(g.gridZ - 1)));
}

// Profundidade onde a fatia `i` começa (i = gridZ devolve zFar).
inline float SliceNearDepth(const ClusterGridGPU& g, uint32_t i) {
    return std::exp2((float(i) - g.sliceBias) / g.sliceScale);
}

// Tamanho de tile sugerido: mantém o número de clusters (e a memória) sob controle em telas grandes.
inline uint32_t SuggestClusterTileSize(uint32_t width, uint32_t height, QualityTier tier) {
    if (tier == QualityTier::Mobile || tier == QualityTier::Low) return 32;
    return uint64_t(width) * height > 2'600'000ull ? 32u : 16u; // > ~1440p
}

} // namespace LightPrism
