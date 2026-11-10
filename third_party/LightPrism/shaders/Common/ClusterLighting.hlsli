// Lado do pixel: lê a lista compacta do cluster e avalia só as luzes que importam.
// Substitui o laço global de ForwardLighting.hlsli. Funciona igual com a lista vinda da GPU ou do CPU fallback.
//
// Índice de luz na lista: [0, numPointLights) = ponto; senão spot (índice - numPointLights).
#ifndef LP_CLUSTER_LIGHTING_HLSLI
#define LP_CLUSTER_LIGHTING_HLSLI

#include "PBR.hlsli"
#include "LightBindings.hlsli"
#include "Cluster.hlsli"

// Defina LP_ENABLE_CSM antes de incluir para aplicar as sombras em cascata ao sol (veja Shadow.hlsli).
#ifdef LP_ENABLE_CSM
#include "Shadow.hlsli"
#endif

cbuffer ClusterGridCB : register(b5) { ClusterGrid ClusterGridData; };
StructuredBuffer<ClusterCell> LightGrid      : register(t8);
StructuredBuffer<uint>        LightIndexList : register(t9);

// pixelPos = SV_Position.xy; viewDepth = -viewPos.z (distância positiva ao longo do eixo da câmera).
float3 EvaluateClusteredLights(SurfaceData s, float3 worldPos, float2 pixelPos, float viewDepth) {
    float3 total = 0.0f;
    if (Globals.hasSun != 0u) {
        float3 sun = EvaluateDirectionalLight(SunLight, s);
#ifdef LP_ENABLE_CSM
        sun *= CsmVisibility(worldPos, s.N, viewDepth);
#endif
        total += sun;
    }

    ClusterCell cell = LightGrid[ClusterIndexForPixel(ClusterGridData, pixelPos, viewDepth)];
    uint np = ClusterGridData.numPointLights;
    for (uint i = 0; i < cell.count; ++i) {
        uint li = LightIndexList[cell.offset + i];
        if (li < np) total += EvaluatePointLight(PointLightsBuffer[li], s, worldPos);
        else         total += EvaluateSpotLight(SpotLightsBuffer[li - np], s, worldPos);
    }
    return total;
}

#endif
