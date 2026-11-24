// Passe 4: reexecuta o teste (mesma ordem e mesmo teto do passe de contagem) usando groupshared
// e grava os índices no offset final = offsetLocal + GroupSums[cluster / 256].
// Escreve também a célula (offset, count).
// Dispatch: ceil(clusterCount / 64) x 1 x 1.
#include "../Common/Cluster.hlsli"

cbuffer ClusterGridCB : register(b0) { ClusterGrid Grid; };

StructuredBuffer<ClusterLight> Lights : register(t0);

RWStructuredBuffer<uint>        LightIndexList : register(u0);
RWStructuredBuffer<ClusterCell> LightGrid      : register(u1);
RWStructuredBuffer<uint>        ClusterCounts  : register(u2); // offsets locais (saída do ScanLocal)
RWStructuredBuffer<uint>        GroupSums      : register(u3); // offsets dos grupos (saída do ScanGroupSums)

groupshared ClusterLight gs_Lights[64];

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID, uint3 gtid : SV_GroupThreadID) {
    uint i = id.x;
    uint clusterCount = ClusterCountOf(Grid);
    bool active = i < clusterCount;

    uint offset = active ? (ClusterCounts[i] + GroupSums[i / LP_SCAN_GROUP]) : 0u;
    uint3 c = active ? ClusterCoordsOf(Grid, i) : uint3(0, 0, 0);
    // Sem ternario: HLSL nao aceita ramos de tipos diferentes (struct x escalar).
    ClusterAABB aabb = (ClusterAABB)0;
    if (active) aabb = BuildClusterAABB(Grid, c);
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
                if (LightIntersectsAABB(L, aabb)) {
                    uint pos = offset + count;
                    if (pos < Grid.maxIndices) LightIndexList[pos] = base + j;
                    ++count;
                }
            }
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (active) {
        ClusterCell cell;
        cell.offset = offset;
        cell.count = (offset >= Grid.maxIndices) ? 0u : min(count, Grid.maxIndices - offset);
        LightGrid[i] = cell;
    }
}