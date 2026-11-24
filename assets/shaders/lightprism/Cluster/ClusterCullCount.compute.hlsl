// Passe 2: conta quantas luzes tocam cada cluster (com teto por cluster).
// Usa groupshared para compartilhar lotes de 64 luzes entre as threads do grupo,
// reduzindo leituras globais repetitivas mantendo a ordem deterministica de prioridade.
// Dispatch: ceil(clusterCount / 64) x 1 x 1.
#include "../Common/Cluster.hlsli"

cbuffer ClusterGridCB : register(b0) { ClusterGrid Grid; };

StructuredBuffer<ClusterLight> Lights : register(t0);
RWStructuredBuffer<uint> ClusterCounts : register(u0);

groupshared ClusterLight gs_Lights[64];

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID, uint3 gtid : SV_GroupThreadID) {
    uint i = id.x;
    uint clusterCount = ClusterCountOf(Grid);
    bool active = i < clusterCount;

    uint3 c = active ? ClusterCoordsOf(Grid, i) : uint3(0, 0, 0);
    ClusterAABB aabb = active ? BuildClusterAABB(Grid, c) : (ClusterAABB)0;
    uint total = Grid.numPointLights + Grid.numSpotLights;

    uint count = 0;
    for (uint base = 0; base < total; base += 64u) {
        uint loadIdx = base + gtid.x;
        if (loadIdx < total) {
            gs_Lights[gtid.x] = Lights[loadIdx];
        }
        GroupMemoryBarrierWithGroupSync();

        if (active && count < Grid.maxLightsPerCluster) {
            uint batchLen = min(64u, total - base);
            for (uint j = 0; j < batchLen && count < Grid.maxLightsPerCluster; ++j) {
                ClusterLight L = gs_Lights[j];
                if (!ClusterInLightRange(L, c)) continue;
                if (LightIntersectsAABB(L, aabb)) ++count;
            }
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (active) ClusterCounts[i] = count;
}