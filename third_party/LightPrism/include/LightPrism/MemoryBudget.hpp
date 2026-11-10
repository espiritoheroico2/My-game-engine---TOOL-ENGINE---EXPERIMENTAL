#pragma once
// Estimativa de memória de GPU da Light Prism por qualidade e resolução (otimização 8). Serve para escolher o tier,
// documentar orçamentos e detectar regressões: os testes comparam estes números com as alocações reais.
#include <cstdint>
#include <cstdio>
#include <string>

#include "LightPrism/Cluster/ClusterTypes.hpp"
#include "LightPrism/LightTypes.hpp"
#include "LightPrism/Quality.hpp"

namespace LightPrism {

struct MemoryQuery {
    uint32_t width = 1920, height = 1080;
    uint32_t tileSizePx = 16, zSlices = 24;
    QualitySettings quality = MakeQualitySettings(QualityTier::Medium);
    uint32_t maxPointLights = 4096, maxSpotLights = 1024;
    uint32_t iblMips = 6, lutSize = 32;
    bool hasSun = true;
    bool ibl = true;
};

struct MemoryEstimate {
    uint64_t lightBuffers = 0;   // pontos + spots + cbuffers
    uint64_t clusters = 0;       // counts + grid + índices + somas + ClusterLight
    uint64_t shadowMaps = 0;
    uint64_t ibl = 0;            // cubemap pré-filtrado + LUT (SH é cbuffer, ~160 B)
    uint64_t ssao = 0;           // AO bruto + final (R8)
    uint64_t Total() const { return lightBuffers + clusters + shadowMaps + ibl + ssao; }
};

inline uint64_t EstimateCubemapChainBytes(uint32_t baseSize, uint32_t mips, uint32_t bytesPerTexel) {
    uint64_t b = 0;
    for (uint32_t m = 0; m < mips; ++m) {
        const uint32_t s = (baseSize >> m) < 8u ? 8u : (baseSize >> m);
        b += uint64_t(s) * s * 6 * bytesPerTexel;
    }
    return b;
}

inline MemoryEstimate EstimateMemory(const MemoryQuery& q) {
    MemoryEstimate e;
    const QualitySettings& s = q.quality;
    e.lightBuffers = uint64_t(q.maxPointLights) * sizeof(PointLightGPU) + uint64_t(q.maxSpotLights) * sizeof(SpotLightGPU);

    ClusterGridDesc d;
    d.screenWidth = q.width; d.screenHeight = q.height; d.tileSizePx = q.tileSizePx; d.zSlices = q.zSlices;
    const ClusterGridGPU g = MakeClusterGrid(d);
    const uint64_t clusters = ClusterCount(g);
    const uint64_t groups = (clusters + kClusterScanGroupSize - 1) / kClusterScanGroupSize;
    e.clusters = clusters * 4 + groups * 4 + clusters * sizeof(ClusterCell) + clusters * s.indicesPerCluster * 4 +
                 uint64_t(q.maxPointLights + q.maxSpotLights) * sizeof(ClusterLightGPU);

    if (q.hasSun && s.csmCascades > 0)
        e.shadowMaps = uint64_t(s.csmCascades) * s.shadowMapResolution * s.shadowMapResolution * (s.shadowDepth16 ? 2 : 4);

    if (q.ibl) {
        e.ibl = EstimateCubemapChainBytes(s.iblCubeSize, q.iblMips, s.packedIbl ? 4 : 12) +
                uint64_t(q.lutSize) * q.lutSize * (s.packedIbl ? 2 : 8);
    }
    if (s.ssao) {
        const uint64_t px = uint64_t(q.width) * q.height;
        e.ssao = (s.ssaoHalfRes ? px / 4 : px) * 2;   // 2 alvos R8
    }
    return e;
}

inline std::string FormatMiB(uint64_t bytes) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", double(bytes) / (1024.0 * 1024.0));
    return buf;
}

} // namespace LightPrism
