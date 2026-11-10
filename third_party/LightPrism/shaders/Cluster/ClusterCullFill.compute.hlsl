// Passe 4: reexecuta o teste (mesma ordem e mesmo teto do passe de contagem) e grava os índices
// no offset final = offsetLocal + GroupSums[cluster / 256]. Escreve também a célula (offset, count).
// Writes além de maxIndices são descartados (overflow reportado por Stats[0] > maxIndices).
// Dispatch: ceil(clusterCount / 64) x 1 x 1.
#include "../Common/Cluster.hlsli"

cbuffer ClusterGridCB : register(b0) { ClusterGrid Grid; };

StructuredBuffer<ClusterLight> Lights : register(t0);

RWStructuredBuffer<uint>        LightIndexList : register(u0);
RWStructuredBuffer<ClusterCell> LightGrid      : register(u1);
RWStructuredBuffer<uint>        ClusterCounts  : register(u2); // offsets locais (saída do ScanLocal)
RWStructuredBuffer<uint>        GroupSums      : register(u3); // offsets dos grupos (saída do ScanGroupSums)

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    uint i = id.x;
    if (i >= ClusterCountOf(Grid)) return;

    uint offset = ClusterCounts[i] + GroupSums[i / LP_SCAN_GROUP];

    uint3 c = ClusterCoordsOf(Grid, i);
    ClusterAABB aabb = BuildClusterAABB(Grid, c);
    uint total = Grid.numPointLights + Grid.numSpotLights;

    uint count = 0;
    for (uint li = 0; li < total && count < Grid.maxLightsPerCluster; ++li) {
        ClusterLight L = Lights[li];
        if (!ClusterInLightRange(L, c)) continue;
        if (LightIntersectsAABB(L, aabb)) {
            uint pos = offset + count;
            if (pos < Grid.maxIndices) LightIndexList[pos] = li;
            ++count;
        }
    }

    ClusterCell cell;
    cell.offset = offset;
    cell.count = (offset >= Grid.maxIndices) ? 0u : min(count, Grid.maxIndices - offset);
    LightGrid[i] = cell;
}
