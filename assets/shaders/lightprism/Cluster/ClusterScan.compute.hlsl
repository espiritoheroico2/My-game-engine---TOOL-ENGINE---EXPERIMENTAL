// Passe 3: prefix sum exclusivo hierárquico sobre ClusterCounts.
//   ScanLocal     : 1 thread por cluster; scan exclusivo por grupo de 256, grava a soma do grupo.
//   ScanGroupSums : 1 grupo de 1024 threads; scan exclusivo das somas dos grupos (K somas por thread),
//                   grava o total em Stats[0] (= índices pedidos; compare com maxIndices para detectar overflow).
// O passo final (somar GroupSums[i / 256] ao offset local) é feito no passe de fill.
// Dispatch: ScanLocal ceil(clusterCount / 256) x 1 x 1; ScanGroupSums 1 x 1 x 1.
#include "../Common/Cluster.hlsli"

cbuffer ClusterGridCB : register(b0) { ClusterGrid Grid; };

RWStructuredBuffer<uint> ClusterCounts : register(u0);
RWStructuredBuffer<uint> GroupSums     : register(u1);
RWStructuredBuffer<uint> Stats         : register(u2);

groupshared uint gsScan[LP_SCAN_SUMS_THREADS];

// Hillis-Steele inclusivo. Todas as threads do grupo DEVEM chamar (usa barreiras). n = threads do grupo.
uint BlockInclusiveScan(uint t, uint v, uint n) {
    gsScan[t] = v;
    GroupMemoryBarrierWithGroupSync();
    for (uint offset = 1; offset < n; offset <<= 1) {
        uint add = (t >= offset) ? gsScan[t - offset] : 0u;
        GroupMemoryBarrierWithGroupSync();
        gsScan[t] += add;
        GroupMemoryBarrierWithGroupSync();
    }
    return gsScan[t];
}

[numthreads(LP_SCAN_GROUP, 1, 1)]
void ScanLocal(uint3 id : SV_DispatchThreadID, uint3 gtid : SV_GroupThreadID, uint3 gid : SV_GroupID) {
    uint n = ClusterCountOf(Grid);
    uint i = id.x;
    uint v = (i < n) ? ClusterCounts[i] : 0u;

    uint inclusive = BlockInclusiveScan(gtid.x, v, LP_SCAN_GROUP);

    if (i < n) ClusterCounts[i] = inclusive - v;                 // exclusivo local
    if (gtid.x == LP_SCAN_GROUP - 1) GroupSums[gid.x] = inclusive; // soma do grupo
}

[numthreads(LP_SCAN_SUMS_THREADS, 1, 1)]
void ScanGroupSums(uint3 gtid : SV_GroupThreadID) {
    uint t = gtid.x;
    uint numGroups = (ClusterCountOf(Grid) + LP_SCAN_GROUP - 1) / LP_SCAN_GROUP;
    uint K = (numGroups + LP_SCAN_SUMS_THREADS - 1) / LP_SCAN_SUMS_THREADS;
    uint b = t * K;
    uint e = min(b + K, numGroups);

    uint sum = 0;
    for (uint g = b; g < e; ++g) sum += GroupSums[g];

    uint inclusive = BlockInclusiveScan(t, sum, LP_SCAN_SUMS_THREADS);

    uint run = inclusive - sum;
    for (uint h = b; h < e; ++h) {
        uint v = GroupSums[h];
        GroupSums[h] = run;
        run += v;
    }
    if (t == LP_SCAN_SUMS_THREADS - 1) Stats[0] = inclusive;
}
