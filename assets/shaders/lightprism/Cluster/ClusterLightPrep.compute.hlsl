// Passe 1: luzes -> view space + esfera envolvente + faixa de clusters candidatos.
// Dispatch: ceil((numPointLights + numSpotLights) / 64) x 1 x 1. Ordem de saída: pontos, depois spots.
#include "../Common/Cluster.hlsli"

cbuffer ClusterGridCB : register(b0) { ClusterGrid Grid; };
cbuffer ViewCB        : register(b1) { row_major float4x4 ViewMatrix; };

StructuredBuffer<PointLight> InPointLights : register(t0);
StructuredBuffer<SpotLight>  InSpotLights  : register(t1);
RWStructuredBuffer<ClusterLight> OutClusterLights : register(u0);

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    uint i = id.x;
    uint total = Grid.numPointLights + Grid.numSpotLights;
    if (i >= total) return;

    if (i < Grid.numPointLights)
        OutClusterLights[i] = PrepareClusterPoint(InPointLights[i], Grid, ViewMatrix);
    else
        OutClusterLights[i] = PrepareClusterSpot(InSpotLights[i - Grid.numPointLights], Grid, ViewMatrix);
}
