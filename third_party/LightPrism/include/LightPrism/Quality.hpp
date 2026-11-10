#pragma once
// Presets de qualidade: degradação graciosa de Mobile até High.
#include <cstdint>

#include "LightPrism/Rhi.hpp"

namespace LightPrism {

enum class QualityTier : uint8_t { Mobile, Low, Medium, High };

struct QualitySettings {
    QualityTier tier = QualityTier::Medium;
    uint32_t csmCascades = 3;
    uint32_t shadowMapResolution = 2048;
    uint32_t pcfTaps = 4;            // 1, 4 ou 9
    bool ssao = true;
    bool ssr = false;
    bool volumetrics = false;
    float volumetricResolutionScale = 0.25f;
    bool interleavedShadowUpdates = false; // luzes ímpares/pares em frames alternados
    uint32_t maxLightsPerCluster = 64;
    // Memória/ALU (otimizações da Fase 5a)
    bool packedIbl = false;          // cubemap em RGB9E5/R11G11B10F (4 B/texel) e LUT em RG8
    bool shadowDepth16 = false;      // shadow maps D16 em vez de D32 (metade da memória)
    bool ssaoHalfRes = false;        // SSAO em 1/4 dos pixels com upsample por profundidade
    bool shL1 = false;               // irradiância com 4 coeficientes SH em vez de 9
    uint32_t iblCubeSize = 64;       // face do mip 0 do ambiente pré-filtrado
    float autoRadiusCutoff = 0.0f;   // raio de culling automático (LightingConfig::autoRadiusCutoff)
    uint32_t indicesPerCluster = 8;  // orçamento inicial da lista de índices (ClusterSystemDesc)
};

inline QualitySettings MakeQualitySettings(QualityTier tier) {
    QualitySettings q;
    q.tier = tier;
    switch (tier) {
    case QualityTier::Mobile:
        q.csmCascades = 1; q.shadowMapResolution = 1024; q.pcfTaps = 1; q.ssao = false;
        q.interleavedShadowUpdates = true; q.maxLightsPerCluster = 16;
        q.packedIbl = true; q.shadowDepth16 = true; q.ssaoHalfRes = true; q.shL1 = true; q.iblCubeSize = 32;
        q.autoRadiusCutoff = 0.1f; q.indicesPerCluster = 4; break;
    case QualityTier::Low:
        q.csmCascades = 2; q.shadowMapResolution = 1024; q.pcfTaps = 4; q.ssao = false;
        q.interleavedShadowUpdates = true; q.maxLightsPerCluster = 32;
        q.packedIbl = true; q.shadowDepth16 = true; q.ssaoHalfRes = true; q.iblCubeSize = 32;
        q.autoRadiusCutoff = 0.05f; q.indicesPerCluster = 6; break;
    case QualityTier::Medium:
        q.packedIbl = true; q.ssaoHalfRes = true; q.autoRadiusCutoff = 0.02f; break;
    case QualityTier::High:
        q.csmCascades = 4; q.shadowMapResolution = 4096; q.pcfTaps = 9; q.ssr = true;
        q.volumetrics = true; q.maxLightsPerCluster = 128; q.iblCubeSize = 128; q.autoRadiusCutoff = 0.01f; break;
    }
    return q;
}

inline QualityTier SuggestTier(const DeviceCaps& caps) {
    constexpr uint64_t GB = 1024ull * 1024 * 1024;
    if (caps.mobileClass) return QualityTier::Mobile;
    if (!caps.computeShaders) return QualityTier::Low;
    if (caps.vramBytes == 0) return QualityTier::Medium;
    if (caps.vramBytes < 3 * GB) return QualityTier::Low;
    if (caps.vramBytes < 6 * GB) return QualityTier::Medium;
    return QualityTier::High;
}

} // namespace LightPrism
