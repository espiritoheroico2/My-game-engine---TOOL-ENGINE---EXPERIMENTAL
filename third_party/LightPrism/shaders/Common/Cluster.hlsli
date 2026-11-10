// Matemática de clusters. Espelha include/LightPrism/Cluster/*.hpp (a versão C++ é a referência testada).
// Sem recursos (buffers) aqui: só structs e funções, para poder ser incluído em qualquer shader.
#ifndef LP_CLUSTER_HLSLI
#define LP_CLUSTER_HLSLI

#include "LightTypes.hlsli"

#define LP_SCAN_GROUP 256          // = kClusterScanGroupSize
#define LP_SCAN_SUMS_THREADS 1024  // = kClusterScanSumsThreads

struct ClusterGrid {               // 64 bytes (ClusterGridGPU)
    uint  gridX, gridY, gridZ, tileSizePx;
    float zNear, zFar, sliceScale, sliceBias;
    float2 projScale;              // P[0][0], P[1][1]
    float2 screenSize;
    uint  numPointLights, numSpotLights, maxLightsPerCluster, maxIndices;
};

struct ClusterAABB { float4 minPoint; float4 maxPoint; };       // 32 bytes

struct ClusterLight {              // 80 bytes (ClusterLightGPU)
    float3 boundCenter; float boundRadius;
    float3 apex;        float range;
    float3 dir;         float cosOuter;
    float  sinOuter;    uint  kind;       // 0 = ponto, 1 = spot
    int    sliceMin;    int   sliceMax;   // vazio: sliceMin > sliceMax
    int4   tileRange;                     // (minX, minY, maxX, maxY)
};

struct ClusterCell { uint offset; uint count; };                // 8 bytes

uint ClusterCountOf(ClusterGrid g) { return g.gridX * g.gridY * g.gridZ; }

uint ClusterIndexOf(ClusterGrid g, uint3 c) { return c.x + c.y * g.gridX + c.z * g.gridX * g.gridY; }

uint3 ClusterCoordsOf(ClusterGrid g, uint index) {
    uint xy = g.gridX * g.gridY;
    uint z  = index / xy;
    uint r  = index - z * xy;
    uint y  = r / g.gridX;
    return uint3(r - y * g.gridX, y, z);
}

// depth = -viewPos.z (positivo)
uint SliceFromDepth(ClusterGrid g, float depth) {
    float s = floor(log2(max(depth, g.zNear)) * g.sliceScale + g.sliceBias);
    return (uint)clamp(s, 0.0f, (float)(g.gridZ - 1));
}

float SliceNearDepth(ClusterGrid g, uint i) { return exp2((float(i) - g.sliceBias) / g.sliceScale); }

// Índice do cluster de um pixel (pixelPos = SV_Position.xy, y para baixo).
uint ClusterIndexForPixel(ClusterGrid g, float2 pixelPos, float depth) {
    uint2 tile = min((uint2)pixelPos / g.tileSizePx, uint2(g.gridX - 1, g.gridY - 1));
    return ClusterIndexOf(g, uint3(tile, SliceFromDepth(g, depth)));
}

ClusterAABB BuildClusterAABB(ClusterGrid g, uint3 c) {
    float2 p0 = float2(c.xy * g.tileSizePx);
    float2 p1 = float2((c.xy + 1) * g.tileSizePx);
    float2 n0 = float2(p0.x / g.screenSize.x * 2.0f - 1.0f, 1.0f - p0.y / g.screenSize.y * 2.0f);
    float2 n1 = float2(p1.x / g.screenSize.x * 2.0f - 1.0f, 1.0f - p1.y / g.screenSize.y * 2.0f);
    float dn = SliceNearDepth(g, c.z);
    float df = SliceNearDepth(g, c.z + 1);

    float3 mn = float3(1e30f, 1e30f, 1e30f);
    float3 mx = float3(-1e30f, -1e30f, -1e30f);
    [unroll] for (int i = 0; i < 8; ++i) {
        float2 n = float2((i & 1) ? n1.x : n0.x, (i & 2) ? n1.y : n0.y);
        float  d = (i & 4) ? df : dn;
        float3 p = float3(n.x * d / g.projScale.x, n.y * d / g.projScale.y, -d);
        mn = min(mn, p);
        mx = max(mx, p);
    }
    ClusterAABB a;
    a.minPoint = float4(mn, 0.0f);
    a.maxPoint = float4(mx, 0.0f);
    return a;
}

bool SphereIntersectsAABB(float3 c, float r, float3 bmin, float3 bmax) {
    float3 closest = max(bmin, min(c, bmax));
    float3 d = closest - c;
    return dot(d, d) <= r * r;
}

bool ConeIntersectsSphere(ClusterLight s, float3 c, float r) {
    float3 v = c - s.apex;
    float vlenSq = dot(v, v);
    float v1 = dot(v, s.dir);
    float distClosest = s.cosOuter * sqrt(max(vlenSq - v1 * v1, 0.0f)) - v1 * s.sinOuter;
    bool angleCull = distClosest > r;
    bool frontCull = v1 > r + s.range;
    bool backCull  = v1 < -r;
    return !(angleCull || frontCull || backCull);
}

bool LightIntersectsAABB(ClusterLight l, ClusterAABB a) {
    if (!SphereIntersectsAABB(l.boundCenter, l.boundRadius, a.minPoint.xyz, a.maxPoint.xyz)) return false;
    if (l.kind == 0u) return true;
    float3 center = (a.minPoint.xyz + a.maxPoint.xyz) * 0.5f;
    float radius = length(a.maxPoint.xyz - a.minPoint.xyz) * 0.5f;
    return ConeIntersectsSphere(l, center, radius);
}

// Teste barato (só inteiros) antes da interseção exata.
bool ClusterInLightRange(ClusterLight l, uint3 c) {
    int3 ci = (int3)c;
    return ci.z >= l.sliceMin && ci.z <= l.sliceMax &&
           ci.x >= l.tileRange.x && ci.x <= l.tileRange.z &&
           ci.y >= l.tileRange.y && ci.y <= l.tileRange.w;
}

void SetEmptyRange(inout ClusterLight o) {
    o.sliceMin = 1; o.sliceMax = 0;
    o.tileRange = int4(1, 1, 0, 0);
}

void ComputeClusterRange(ClusterGrid g, float3 c, float r, inout ClusterLight o) {
    float dmin = -c.z - r;
    float dmax = -c.z + r;
    if (dmax < g.zNear || dmin > g.zFar) { SetEmptyRange(o); return; }

    o.sliceMin = (int)SliceFromDepth(g, max(dmin, g.zNear));
    o.sliceMax = (int)SliceFromDepth(g, dmax);

    if (dmin <= g.zNear) {
        o.tileRange = int4(0, 0, (int)g.gridX - 1, (int)g.gridY - 1);
        return;
    }

    float4 xs = float4(c.x - r, c.x - r, c.x + r, c.x + r) / float4(dmin, dmax, dmin, dmax);
    float4 ys = float4(c.y - r, c.y - r, c.y + r, c.y + r) / float4(dmin, dmax, dmin, dmax);
    float2 ndcMin = float2(min(min(xs.x, xs.y), min(xs.z, xs.w)), min(min(ys.x, ys.y), min(ys.z, ys.w))) * g.projScale;
    float2 ndcMax = float2(max(max(xs.x, xs.y), max(xs.z, xs.w)), max(max(ys.x, ys.y), max(ys.z, ys.w))) * g.projScale;

    float pxMin = (ndcMin.x * 0.5f + 0.5f) * g.screenSize.x - 0.5f;
    float pxMax = (ndcMax.x * 0.5f + 0.5f) * g.screenSize.x + 0.5f;
    float pyMin = (0.5f - ndcMax.y * 0.5f) * g.screenSize.y - 0.5f;
    float pyMax = (0.5f - ndcMin.y * 0.5f) * g.screenSize.y + 0.5f;
    if (pxMax < 0.0f || pxMin >= g.screenSize.x || pyMax < 0.0f || pyMin >= g.screenSize.y) { SetEmptyRange(o); return; }

    float tile = (float)g.tileSizePx;
    float gx = (float)(g.gridX - 1), gy = (float)(g.gridY - 1);
    o.tileRange = int4((int)clamp(floor(pxMin / tile), 0.0f, gx), (int)clamp(floor(pyMin / tile), 0.0f, gy),
                       (int)clamp(floor(pxMax / tile), 0.0f, gx), (int)clamp(floor(pyMax / tile), 0.0f, gy));
}

ClusterLight PrepareClusterPoint(PointLight l, ClusterGrid g, float4x4 view) {
    ClusterLight o = (ClusterLight)0;
    float3 p = mul(view, float4(l.position, 1.0f)).xyz;
    float rc = (l.cullRadius > 0.0f && l.cullRadius < l.radius) ? l.cullRadius : l.radius;
    o.boundCenter = p;
    o.boundRadius = rc;
    o.apex = p;
    o.range = rc;
    o.dir = float3(0.0f, 0.0f, -1.0f);
    o.cosOuter = -1.0f;
    o.sinOuter = 0.0f;
    o.kind = 0u;
    ComputeClusterRange(g, o.boundCenter, o.boundRadius, o);
    return o;
}

ClusterLight PrepareClusterSpot(SpotLight l, ClusterGrid g, float4x4 view) {
    ClusterLight o = (ClusterLight)0;
    o.apex = mul(view, float4(l.position, 1.0f)).xyz;
    o.range = l.radius;
    o.dir = normalize(mul((float3x3)view, l.direction));
    o.cosOuter = saturate(-l.spotOffset / l.spotScale);
    o.sinOuter = sqrt(max(1.0f - o.cosOuter * o.cosOuter, 0.0f));
    o.kind = 1u;
    if (o.cosOuter > 0.5f) {
        float t = l.radius / (2.0f * o.cosOuter);
        o.boundCenter = o.apex + o.dir * t;
        o.boundRadius = t;
    } else {
        o.boundCenter = o.apex;
        o.boundRadius = l.radius;
    }
    ComputeClusterRange(g, o.boundCenter, o.boundRadius, o);
    return o;
}

#endif
