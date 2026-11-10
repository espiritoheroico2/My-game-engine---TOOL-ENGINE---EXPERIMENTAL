#include "LightPrism/Cluster/ClusterCulling.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace LightPrism {

// ---------------------------------------------------------------------------------------------
// Geometria
// ---------------------------------------------------------------------------------------------
ClusterAABB BuildClusterAABB(const ClusterGridGPU& g, uint32_t tx, uint32_t ty, uint32_t tz) {
    const float x0 = float(tx * g.tileSizePx), x1 = float((tx + 1) * g.tileSizePx);
    const float y0 = float(ty * g.tileSizePx), y1 = float((ty + 1) * g.tileSizePx);
    const float nx[2] = {x0 / g.screenWidth * 2.0f - 1.0f, x1 / g.screenWidth * 2.0f - 1.0f};
    const float ny[2] = {1.0f - y0 / g.screenHeight * 2.0f, 1.0f - y1 / g.screenHeight * 2.0f};
    const float depths[2] = {SliceNearDepth(g, tz), SliceNearDepth(g, tz + 1)};

    constexpr float inf = std::numeric_limits<float>::infinity();
    Vec3 mn{inf, inf, inf}, mx{-inf, -inf, -inf};
    for (float d : depths)
        for (float ax : nx)
            for (float ay : ny) {
                const Vec3 p{ax * d / g.projScaleX, ay * d / g.projScaleY, -d};
                mn = {std::min(mn.x, p.x), std::min(mn.y, p.y), std::min(mn.z, p.z)};
                mx = {std::max(mx.x, p.x), std::max(mx.y, p.y), std::max(mx.z, p.z)};
            }
    ClusterAABB out{};
    out.minPoint = mn;
    out.maxPoint = mx;
    return out;
}

std::vector<ClusterAABB> BuildAllClusterAABBs(const ClusterGridGPU& g) {
    std::vector<ClusterAABB> v(ClusterCount(g));
    for (uint32_t z = 0; z < g.gridZ; ++z)
        for (uint32_t y = 0; y < g.gridY; ++y)
            for (uint32_t x = 0; x < g.gridX; ++x) v[ClusterIndexOf(g, x, y, z)] = BuildClusterAABB(g, x, y, z);
    return v;
}

namespace {

void SetEmptyRange(ClusterLightGPU& o) {
    o.sliceMin = 1; o.sliceMax = 0;
    o.tileMinX = 1; o.tileMaxX = 0;
    o.tileMinY = 1; o.tileMaxY = 0;
}

// Faixa conservativa de clusters que a esfera (view space) pode tocar.
void ComputeRange(const ClusterGridGPU& g, const Vec3& c, float r, ClusterLightGPU& o) {
    const float dmin = -c.z - r;
    const float dmax = -c.z + r;
    if (dmax < g.zNear || dmin > g.zFar) { SetEmptyRange(o); return; }

    o.sliceMin = int32_t(SliceFromDepth(g, std::max(dmin, g.zNear)));
    o.sliceMax = int32_t(SliceFromDepth(g, dmax));

    if (dmin <= g.zNear) { // cruza o plano near: projeção não é limitada
        o.tileMinX = 0; o.tileMaxX = int32_t(g.gridX) - 1;
        o.tileMinY = 0; o.tileMaxY = int32_t(g.gridY) - 1;
        return;
    }

    const float xs[4] = {(c.x - r) / dmin, (c.x - r) / dmax, (c.x + r) / dmin, (c.x + r) / dmax};
    const float ys[4] = {(c.y - r) / dmin, (c.y - r) / dmax, (c.y + r) / dmin, (c.y + r) / dmax};
    const float ndcMinX = *std::min_element(xs, xs + 4) * g.projScaleX;
    const float ndcMaxX = *std::max_element(xs, xs + 4) * g.projScaleX;
    const float ndcMinY = *std::min_element(ys, ys + 4) * g.projScaleY;
    const float ndcMaxY = *std::max_element(ys, ys + 4) * g.projScaleY;

    // Meio pixel de folga contra erros de arredondamento nas bordas dos tiles.
    const float pxMin = (ndcMinX * 0.5f + 0.5f) * g.screenWidth - 0.5f;
    const float pxMax = (ndcMaxX * 0.5f + 0.5f) * g.screenWidth + 0.5f;
    const float pyMin = (0.5f - ndcMaxY * 0.5f) * g.screenHeight - 0.5f;
    const float pyMax = (0.5f - ndcMinY * 0.5f) * g.screenHeight + 0.5f;
    if (pxMax < 0.0f || pxMin >= g.screenWidth || pyMax < 0.0f || pyMin >= g.screenHeight) { SetEmptyRange(o); return; }

    const float tile = float(g.tileSizePx);
    const float gx = float(g.gridX - 1), gy = float(g.gridY - 1);
    o.tileMinX = int32_t(std::clamp(std::floor(pxMin / tile), 0.0f, gx));
    o.tileMaxX = int32_t(std::clamp(std::floor(pxMax / tile), 0.0f, gx));
    o.tileMinY = int32_t(std::clamp(std::floor(pyMin / tile), 0.0f, gy));
    o.tileMaxY = int32_t(std::clamp(std::floor(pyMax / tile), 0.0f, gy));
}

} // namespace

ClusterLightGPU PrepareClusterLight(const PointLightGPU& l, const ClusterGridGPU& g, const Mat4& view) {
    ClusterLightGPU o{};
    const Vec3 p = view.TransformPoint(l.position);
    const float rc = (l.cullRadius > 0.0f && l.cullRadius < l.radius) ? l.cullRadius : l.radius;
    o.boundCenter = p;
    o.boundRadius = rc;
    o.apex = p;
    o.range = rc;
    o.dir = {0.0f, 0.0f, -1.0f};
    o.cosOuter = -1.0f;
    o.sinOuter = 0.0f;
    o.kind = 0;
    ComputeRange(g, o.boundCenter, o.boundRadius, o);
    return o;
}

ClusterLightGPU PrepareClusterLight(const SpotLightGPU& l, const ClusterGridGPU& g, const Mat4& view) {
    ClusterLightGPU o{};
    o.apex = view.TransformPoint(l.position);
    o.range = l.radius;
    o.dir = Normalize(view.TransformDirection(l.direction));
    o.cosOuter = std::clamp(-l.spotOffset / l.spotScale, 0.0f, 1.0f);
    o.sinOuter = std::sqrt(std::max(1.0f - o.cosOuter * o.cosOuter, 0.0f));
    o.kind = 1;

    // Esfera envolvente do setor cônico (apex, eixo, raio R, semi-ângulo θ):
    // θ < 60° -> esfera que passa pelo apex e pela borda, centro em R/(2cosθ) no eixo; senão esfera no apex.
    if (o.cosOuter > 0.5f) {
        const float t = l.radius / (2.0f * o.cosOuter);
        o.boundCenter = o.apex + o.dir * t;
        o.boundRadius = t;
    } else {
        o.boundCenter = o.apex;
        o.boundRadius = l.radius;
    }
    ComputeRange(g, o.boundCenter, o.boundRadius, o);
    return o;
}

bool SphereIntersectsAABB(const Vec3& c, float r, const Vec3& bmin, const Vec3& bmax) {
    const Vec3 closest{std::max(bmin.x, std::min(c.x, bmax.x)), std::max(bmin.y, std::min(c.y, bmax.y)),
                       std::max(bmin.z, std::min(c.z, bmax.z))};
    const Vec3 d = closest - c;
    return Dot(d, d) <= r * r;
}

// Esfera vs cone infinito truncado (apex, eixo, altura = range). Conservador.
bool ConeIntersectsSphere(const ClusterLightGPU& s, const Vec3& c, float r) {
    const Vec3 v = c - s.apex;
    const float vlenSq = Dot(v, v);
    const float v1 = Dot(v, s.dir);
    const float distClosest = s.cosOuter * std::sqrt(std::max(vlenSq - v1 * v1, 0.0f)) - v1 * s.sinOuter;
    const bool angleCull = distClosest > r;
    const bool frontCull = v1 > r + s.range;
    const bool backCull = v1 < -r;
    return !(angleCull || frontCull || backCull);
}

bool LightIntersectsAABB(const ClusterLightGPU& l, const ClusterAABB& a) {
    if (!SphereIntersectsAABB(l.boundCenter, l.boundRadius, a.minPoint, a.maxPoint)) return false;
    if (l.kind == 0) return true;
    const Vec3 center = (a.minPoint + a.maxPoint) * 0.5f;
    const float radius = Length(a.maxPoint - a.minPoint) * 0.5f;
    return ConeIntersectsSphere(l, center, radius);
}

// ---------------------------------------------------------------------------------------------
// Culling
// ---------------------------------------------------------------------------------------------
namespace {

// Visita pares (cluster, luz) em ordem de luz; dentro de cada luz, ordem de cluster.
template <class Visit>
void ForEachHit(const ClusterGridGPU& g, const std::vector<ClusterAABB>& aabbs,
                const std::vector<ClusterLightGPU>& lights, bool useRanges, const uint8_t* activeMask, Visit&& visit) {
    for (uint32_t li = 0; li < lights.size(); ++li) {
        const ClusterLightGPU& L = lights[li];
        int32_t z0 = 0, z1 = int32_t(g.gridZ) - 1;
        int32_t x0 = 0, x1 = int32_t(g.gridX) - 1, y0 = 0, y1 = int32_t(g.gridY) - 1;
        if (useRanges) {
            z0 = L.sliceMin; z1 = L.sliceMax;
            x0 = L.tileMinX; x1 = L.tileMaxX; y0 = L.tileMinY; y1 = L.tileMaxY;
        }
        for (int32_t z = z0; z <= z1; ++z)
            for (int32_t y = y0; y <= y1; ++y)
                for (int32_t x = x0; x <= x1; ++x) {
                    const uint32_t idx = ClusterIndexOf(g, uint32_t(x), uint32_t(y), uint32_t(z));
                    if (activeMask && !activeMask[idx]) continue;
                    if (LightIntersectsAABB(L, aabbs[idx])) visit(idx, li);
                }
    }
}

void CullImpl(const ClusterGridGPU& gridIn, const std::vector<ClusterAABB>& aabbs, const Mat4& view,
              const PointLightGPU* points, uint32_t np, const SpotLightGPU* spots, uint32_t ns,
              bool useRanges, ClusterCullResult& out, const uint8_t* activeMask) {
    ClusterGridGPU g = gridIn;
    g.numPointLights = np;
    g.numSpotLights = ns;

    std::vector<ClusterLightGPU> lights;
    lights.reserve(size_t(np) + ns);
    for (uint32_t i = 0; i < np; ++i) lights.push_back(PrepareClusterLight(points[i], g, view));
    for (uint32_t i = 0; i < ns; ++i) lights.push_back(PrepareClusterLight(spots[i], g, view));

    const uint32_t clusters = ClusterCount(g);
    const uint32_t cap = g.maxLightsPerCluster;

    // Passe 1: contar (com teto por cluster)
    std::vector<uint32_t> counts(clusters, 0);
    ForEachHit(g, aabbs, lights, useRanges, activeMask, [&](uint32_t c, uint32_t) {
        if (counts[c] < cap) ++counts[c];
    });

    // Scan exclusivo (mesmo algoritmo da GPU)
    const ScanResult scan = HierarchicalExclusiveScan(counts);

    // Passe 2: preencher
    const uint64_t written = std::min<uint64_t>(scan.total, g.maxIndices);
    out.indices.assign(size_t(written), 0u);
    out.cells.assign(clusters, ClusterCell{0, 0});
    std::vector<uint32_t> cursor(clusters, 0);
    ForEachHit(g, aabbs, lights, useRanges, activeMask, [&](uint32_t c, uint32_t li) {
        if (cursor[c] >= counts[c]) return;
        const uint64_t pos = uint64_t(scan.offsets[c]) + cursor[c]++;
        if (pos < g.maxIndices) out.indices[size_t(pos)] = li;
    });

    for (uint32_t c = 0; c < clusters; ++c) {
        const uint32_t off = scan.offsets[c];
        const uint32_t ok = off >= g.maxIndices ? 0u : std::min(counts[c], g.maxIndices - off);
        out.cells[c] = ClusterCell{off, ok};
    }
    out.requestedIndices = scan.total;
    out.overflow = scan.total > g.maxIndices;
}

} // namespace

void CullLightsCPU(const ClusterGridGPU& grid, const std::vector<ClusterAABB>& aabbs, const Mat4& view,
                   const PointLightGPU* points, uint32_t np, const SpotLightGPU* spots, uint32_t ns,
                   ClusterCullResult& out, const uint8_t* activeMask) {
    CullImpl(grid, aabbs, view, points, np, spots, ns, true, out, activeMask);
}

uint32_t MarkActiveClusters(const ClusterGridGPU& g, const float* viewDepth, uint32_t width, uint32_t height,
                            std::vector<uint8_t>& mask) {
    mask.assign(ClusterCount(g), 0);
    uint32_t active = 0;
    for (uint32_t y = 0; y < height; ++y)
        for (uint32_t x = 0; x < width; ++x) {
            const float d = viewDepth[size_t(y) * width + x];
            if (d <= 0.0f) continue;
            const uint32_t tx = std::min(x / g.tileSizePx, g.gridX - 1);
            const uint32_t ty = std::min(y / g.tileSizePx, g.gridY - 1);
            const uint32_t idx = ClusterIndexOf(g, tx, ty, SliceFromDepth(g, d));
            if (!mask[idx]) { mask[idx] = 1; ++active; }
        }
    return active;
}

void CullLightsBruteForce(const ClusterGridGPU& grid, const std::vector<ClusterAABB>& aabbs, const Mat4& view,
                          const PointLightGPU* points, uint32_t np, const SpotLightGPU* spots, uint32_t ns,
                          ClusterCullResult& out) {
    CullImpl(grid, aabbs, view, points, np, spots, ns, false, out, nullptr);
}

// ---------------------------------------------------------------------------------------------
// Scan hierárquico: espelha ClusterScan.compute.hlsl (ScanLocal + ScanGroupSums) e a soma no fill.
// ---------------------------------------------------------------------------------------------
namespace {

// Hillis-Steele inclusivo sobre `v` (equivale ao laço com barreiras do groupshared).
void InclusiveScanInPlace(std::vector<uint32_t>& v) {
    std::vector<uint32_t> tmp(v.size());
    for (size_t off = 1; off < v.size(); off <<= 1) {
        for (size_t t = 0; t < v.size(); ++t) tmp[t] = t >= off ? v[t - off] : 0u;
        for (size_t t = 0; t < v.size(); ++t) v[t] += tmp[t];
    }
}

} // namespace

ScanResult HierarchicalExclusiveScan(const std::vector<uint32_t>& counts) {
    ScanResult r;
    const size_t n = counts.size();
    r.offsets.assign(n, 0u);
    if (n == 0) return r;

    const size_t G = kClusterScanGroupSize;
    const size_t numGroups = (n + G - 1) / G;
    std::vector<uint32_t> groupSums(numGroups, 0u);

    // Kernel A: scan local por grupo (exclusivo gravado no lugar, soma do grupo em groupSums)
    for (size_t gi = 0; gi < numGroups; ++gi) {
        std::vector<uint32_t> v(G, 0u);
        for (size_t t = 0; t < G; ++t) {
            const size_t i = gi * G + t;
            v[t] = i < n ? counts[i] : 0u;
        }
        std::vector<uint32_t> in = v;
        InclusiveScanInPlace(v);
        for (size_t t = 0; t < G; ++t) {
            const size_t i = gi * G + t;
            if (i < n) r.offsets[i] = v[t] - in[t];
        }
        groupSums[gi] = v[G - 1];
    }

    // Kernel B: um grupo de 1024 threads; cada thread cuida de K somas consecutivas
    const size_t T = kClusterScanSumsThreads;
    const size_t K = (numGroups + T - 1) / T;
    std::vector<uint32_t> threadSum(T, 0u);
    for (size_t t = 0; t < T; ++t) {
        const size_t b = t * K, e = std::min(b + K, numGroups);
        for (size_t g = b; g < e; ++g) threadSum[t] += groupSums[g];
    }
    std::vector<uint32_t> inclusive = threadSum;
    InclusiveScanInPlace(inclusive);
    for (size_t t = 0; t < T; ++t) {
        const size_t b = t * K, e = std::min(b + K, numGroups);
        uint32_t run = inclusive[t] - threadSum[t];
        for (size_t g = b; g < e; ++g) { const uint32_t v = groupSums[g]; groupSums[g] = run; run += v; }
    }
    r.total = inclusive[T - 1];

    // Soma do offset do grupo (feita no passe de fill na GPU)
    for (size_t i = 0; i < n; ++i) r.offsets[i] += groupSums[i / G];
    return r;
}

} // namespace LightPrism
