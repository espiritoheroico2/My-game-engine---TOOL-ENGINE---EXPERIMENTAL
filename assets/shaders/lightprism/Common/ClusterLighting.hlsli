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

// Defina LP_ENABLE_POINT_SHADOWS para sombras de pontuais/spot feitas pela engine (PointShadow.hlsli).
#ifdef LP_ENABLE_POINT_SHADOWS
#include "PointShadow.hlsli"
#endif

cbuffer ClusterGridCB : register(b5) { ClusterGrid ClusterGridData; };
StructuredBuffer<ClusterCell> LightGrid      : register(t8);
StructuredBuffer<uint>        LightIndexList : register(t9);

// pixelPos = SV_Position.xy; viewDepth = -viewPos.z (distância positiva ao longo do eixo da câmera).
// receiveShadow: 1 = a superficie recebe sombra das luzes pontuais/spot (ObjectBuffer.receiveShadow).
float3 EvaluateClusteredLights(SurfaceData s, float3 worldPos, float2 pixelPos, float viewDepth, float receiveShadow) {
    float3 total = 0.0f;
    if (Globals.hasSun != 0u) {
        float3 sun = EvaluateDirectionalLight(SunLight, s);
#ifdef LP_ENABLE_CSM
        sun *= receiveShadow > 0.5 ? CsmVisibility(worldPos, s.N, viewDepth) : 1.0f;
#endif
        total += sun;
    }

    ClusterCell cell = LightGrid[ClusterIndexForPixel(ClusterGridData, pixelPos, viewDepth)];
    uint np = ClusterGridData.numPointLights;
    for (uint i = 0; i < cell.count; ++i) {
        uint li = LightIndexList[cell.offset + i];
        if (li < np) {
            float3 c = EvaluatePointLight(PointLightsBuffer[li], s, worldPos);
#ifdef LP_ENABLE_POINT_SHADOWS
            c *= PointShadowForLight(PointLightsBuffer[li].position, 0u, worldPos, s.N, receiveShadow);
#endif
            total += c;
        } else {
            float3 c = EvaluateSpotLight(SpotLightsBuffer[li - np], s, worldPos);
#ifdef LP_ENABLE_POINT_SHADOWS
            c *= PointShadowForLight(SpotLightsBuffer[li - np].position, 1u, worldPos, s.N, receiveShadow);
#endif
            total += c;
        }
    }
    return total;
}

#endif
