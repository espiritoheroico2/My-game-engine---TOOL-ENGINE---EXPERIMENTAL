// Passe 2: conta quantas luzes tocam cada cluster (com teto por cluster). Sem atomics, sem arrays locais.
// O AABB do cluster é calculado na hora (não há buffer de AABBs).
// Dispatch: ceil(clusterCount / 64) x 1 x 1.
#include "../Common/Cluster.hlsli"

cbuffer ClusterGridCB : register(b0) { ClusterGrid Grid; };

StructuredBuffer<ClusterLight> Lights : register(t0);
RWStructuredBuffer<uint> ClusterCounts : register(u0);

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    uint i = id.x;
    if (i >= ClusterCountOf(Grid)) return;

    uint3 c = ClusterCoordsOf(Grid, i);
    ClusterAABB aabb = BuildClusterAABB(Grid, c);
    uint total = Grid.numPointLights + Grid.numSpotLights;

    uint count = 0;
    // Luzes em ordem de prioridade: ao atingir o teto, as menos importantes são descartadas.
    for (uint li = 0; li < total && count < Grid.maxLightsPerCluster; ++li) {
        ClusterLight L = Lights[li];
        if (!ClusterInLightRange(L, c)) continue;
        if (LightIntersectsAABB(L, aabb)) ++count;
    }
    ClusterCounts[i] = count;
}
