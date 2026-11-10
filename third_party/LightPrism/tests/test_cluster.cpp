// Testes da Fase 2: grid de clusters, AABBs, interseções, culling (invariante de correção), teto, overflow e scan.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <random>

#include "LightPrism/Cluster/ClusterCulling.hpp"

using namespace LightPrism;

static int g_failures = 0;
#define CHECK(cond)                                                                                   \
    do {                                                                                              \
        if (!(cond)) { std::printf("FALHOU %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failures; } \
    } while (0)

namespace {

struct Scene {
    ClusterGridGPU grid;
    Mat4 view;
    Mat4 proj;
    std::vector<ClusterAABB> aabbs;
};

Scene MakeScene(uint32_t w = 1280, uint32_t h = 720, uint32_t tile = 16, uint32_t maxPerCluster = 256,
                uint32_t maxIndices = 0) {
    Scene s;
    s.proj = PerspectiveRH_ReverseZ(1.0f, float(w) / float(h), 0.1f, 200.0f);
    ClusterGridDesc d;
    d.screenWidth = w; d.screenHeight = h; d.tileSizePx = tile; d.zSlices = 24;
    d.zNear = 0.1f; d.zFar = 200.0f;
    d.proj00 = s.proj.m[0][0]; d.proj11 = s.proj.m[1][1];
    d.maxLightsPerCluster = maxPerCluster;
    d.maxIndices = maxIndices;
    s.grid = MakeClusterGrid(d);
    s.view = LookAtRH({0, 2, 0}, {0, 2, -1});
    s.aabbs = BuildAllClusterAABBs(s.grid);
    return s;
}

struct Rng {
    std::mt19937 gen{12345};
    float U(float a, float b) { return std::uniform_real_distribution<float>(a, b)(gen); }
};

// Projeta um ponto de mundo; retorna false se fora do volume visível. Preenche o cluster.
bool ClusterOfWorldPoint(const Scene& s, const Vec3& wp, uint32_t& cluster) {
    const Vec3 v = s.view.TransformPoint(wp);
    const float depth = -v.z;
    if (depth < s.grid.zNear || depth > s.grid.zFar) return false;
    const float nx = v.x * s.grid.projScaleX / depth;
    const float ny = v.y * s.grid.projScaleY / depth;
    const float px = (nx * 0.5f + 0.5f) * s.grid.screenWidth;
    const float py = (0.5f - ny * 0.5f) * s.grid.screenHeight;
    if (px < 0 || py < 0 || px >= s.grid.screenWidth || py >= s.grid.screenHeight) return false;
    cluster = ClusterIndexOf(s.grid, uint32_t(px) / s.grid.tileSizePx, uint32_t(py) / s.grid.tileSizePx,
                             SliceFromDepth(s.grid, depth));
    return true;
}

bool ListContains(const ClusterCullResult& r, uint32_t cluster, uint32_t lightIndex) {
    const ClusterCell c = r.cells[cluster];
    for (uint32_t i = 0; i < c.count; ++i)
        if (r.indices[c.offset + i] == lightIndex) return true;
    return false;
}

Vec3 RandomUnit(Rng& r) {
    for (;;) {
        const Vec3 v{r.U(-1, 1), r.U(-1, 1), r.U(-1, 1)};
        const float l = Length(v);
        if (l > 0.1f && l <= 1.0f) return v * (1.0f / l);
    }
}

} // namespace

static void TestSlices() {
    const Scene s = MakeScene();
    const ClusterGridGPU& g = s.grid;
    CHECK(SliceFromDepth(g, g.zNear) == 0);
    CHECK(SliceFromDepth(g, g.zFar * 0.9999f) == g.gridZ - 1);
    CHECK(SliceFromDepth(g, 10000.0f) == g.gridZ - 1);
    CHECK(SliceFromDepth(g, 0.0001f) == 0);
    for (uint32_t i = 0; i < g.gridZ; ++i) {
        const float d0 = SliceNearDepth(g, i), d1 = SliceNearDepth(g, i + 1);
        CHECK(d1 > d0);
        CHECK(SliceFromDepth(g, d0 * 1.001f) == i);
        CHECK(SliceFromDepth(g, d1 * 0.999f) == i);
    }
    CHECK(std::fabs(SliceNearDepth(g, g.gridZ) - g.zFar) < 0.01f);
}

static void TestAabbContainsPoints() {
    const Scene s = MakeScene();
    Rng rng;
    int checked = 0;
    for (int i = 0; i < 20000; ++i) {
        const float depth = rng.U(s.grid.zNear * 1.01f, s.grid.zFar * 0.99f);
        const float px = rng.U(0.0f, s.grid.screenWidth - 0.01f), py = rng.U(0.0f, s.grid.screenHeight - 0.01f);
        const float nx = px / s.grid.screenWidth * 2.0f - 1.0f, ny = 1.0f - py / s.grid.screenHeight * 2.0f;
        const Vec3 v{nx * depth / s.grid.projScaleX, ny * depth / s.grid.projScaleY, -depth};
        const uint32_t idx = ClusterIndexOf(s.grid, uint32_t(px) / s.grid.tileSizePx, uint32_t(py) / s.grid.tileSizePx,
                                            SliceFromDepth(s.grid, depth));
        const ClusterAABB& a = s.aabbs[idx];
        const float e = 1e-3f * depth;
        const bool inside = v.x >= a.minPoint.x - e && v.x <= a.maxPoint.x + e && v.y >= a.minPoint.y - e &&
                            v.y <= a.maxPoint.y + e && v.z >= a.minPoint.z - e && v.z <= a.maxPoint.z + e;
        CHECK(inside);
        ++checked;
    }
    CHECK(checked == 20000);
}

static void TestIntersections() {
    const Vec3 mn{-1, -1, -3}, mx{1, 1, -1};
    CHECK(SphereIntersectsAABB({0, 0, -2}, 0.1f, mn, mx));
    CHECK(SphereIntersectsAABB({2, 0, -2}, 1.01f, mn, mx));
    CHECK(!SphereIntersectsAABB({3, 0, -2}, 1.0f, mn, mx));

    SpotLightGPU spot{};
    spot.position = {0, 0, 0};
    spot.radius = 10.0f;
    spot.direction = {0, 0, -1};
    const float cosI = std::cos(0.3f), cosO = std::cos(0.5f);
    spot.spotScale = 1.0f / (cosI - cosO);
    spot.spotOffset = -cosO * spot.spotScale;
    const ClusterGridGPU g = MakeScene().grid;
    const ClusterLightGPU L = PrepareClusterLight(spot, g, Mat4::Identity());

    auto box = [](Vec3 c, float h) { ClusterAABB a{}; a.minPoint = c - Vec3{h, h, h}; a.maxPoint = c + Vec3{h, h, h}; return a; };
    CHECK(LightIntersectsAABB(L, box({0, 0, -5}, 0.5f)));      // no eixo
    CHECK(!LightIntersectsAABB(L, box({0, 0, 5}, 0.5f)));      // atrás do apex
    CHECK(!LightIntersectsAABB(L, box({8, 0, -2}, 0.5f)));     // ~76° do eixo
    CHECK(!LightIntersectsAABB(L, box({0, 0, -20}, 0.5f)));    // além do alcance
    CHECK(LightIntersectsAABB(L, box({2, 0, -5}, 0.4f)));      // ~21°, dentro do cone
}

static void TestCullingInvariant() {
    const Scene s = MakeScene();
    Rng rng;

    std::vector<PointLightGPU> pts(150);
    for (auto& p : pts) {
        p = {};
        p.position = {rng.U(-30, 30), rng.U(0, 10), rng.U(-80, 10)};
        p.radius = rng.U(2, 14);
    }
    std::vector<SpotLightGPU> spots(60);
    for (auto& sp : spots) {
        sp = {};
        sp.position = {rng.U(-30, 30), rng.U(0, 10), rng.U(-60, 10)};
        sp.radius = rng.U(4, 20);
        sp.direction = RandomUnit(rng);
        const float outer = rng.U(0.2f, 1.2f), inner = outer * 0.5f;
        sp.spotScale = 1.0f / std::max(std::cos(inner) - std::cos(outer), 1e-4f);
        sp.spotOffset = -std::cos(outer) * sp.spotScale;
    }

    ClusterCullResult fast, brute;
    CullLightsCPU(s.grid, s.aabbs, s.view, pts.data(), uint32_t(pts.size()), spots.data(), uint32_t(spots.size()), fast);
    CullLightsBruteForce(s.grid, s.aabbs, s.view, pts.data(), uint32_t(pts.size()), spots.data(), uint32_t(spots.size()), brute);
    CHECK(!fast.overflow && !brute.overflow);

    // Invariante de correção: todo ponto dentro do volume da luz, visível, tem a luz na lista do seu cluster.
    int testedPoints = 0;
    const uint32_t np = uint32_t(pts.size());
    for (uint32_t li = 0; li < np + spots.size(); ++li) {
        for (int k = 0; k < 40; ++k) {
            Vec3 wp;
            if (li < np) {
                wp = pts[li].position + RandomUnit(rng) * (pts[li].radius * std::cbrt(rng.U(0, 1)));
            } else {
                const SpotLightGPU& sp = spots[li - np];
                const float cosOuter = -sp.spotOffset / sp.spotScale;
                const float theta = std::acos(cosOuter) * std::sqrt(rng.U(0, 1)) * 0.999f;
                Vec3 a = std::fabs(sp.direction.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
                const Vec3 u = Normalize(Cross(sp.direction, a)), v = Cross(sp.direction, u);
                const float phi = rng.U(0, 2 * kPi);
                const Vec3 dir = sp.direction * std::cos(theta) + (u * std::cos(phi) + v * std::sin(phi)) * std::sin(theta);
                wp = sp.position + dir * (sp.radius * std::cbrt(rng.U(0, 1)) * 0.999f);
            }
            uint32_t cluster;
            if (!ClusterOfWorldPoint(s, wp, cluster)) continue;
            ++testedPoints;
            CHECK(ListContains(fast, cluster, li));
            CHECK(ListContains(brute, cluster, li));
        }
    }
    CHECK(testedPoints > 1000);

    // A versão eficiente só pode remover pares que o teste exato também removeria: fast ⊆ brute.
    for (size_t c = 0; c < fast.cells.size(); ++c) {
        const ClusterCell f = fast.cells[c], b = brute.cells[c];
        CHECK(f.count <= b.count);
        for (uint32_t i = 0; i < f.count; ++i)
            CHECK(std::binary_search(brute.indices.begin() + b.offset, brute.indices.begin() + b.offset + b.count,
                                     fast.indices[f.offset + i]));
    }
}

static void TestPerClusterCap() {
    const Scene s = MakeScene(1280, 720, 16, /*cap*/ 4);
    std::vector<PointLightGPU> pts(100);
    for (auto& p : pts) { p = {}; p.position = {0, 2, -10}; p.radius = 5.0f; }
    ClusterCullResult r;
    CullLightsCPU(s.grid, s.aabbs, s.view, pts.data(), 100, nullptr, 0, r);
    uint32_t full = 0;
    for (const ClusterCell& c : r.cells) {
        CHECK(c.count <= 4);
        if (c.count == 4) {
            ++full;
            for (uint32_t i = 0; i < 4; ++i) CHECK(r.indices[c.offset + i] == i); // ficam as primeiras (maior prioridade)
        }
    }
    CHECK(full > 0);
}

static void TestGlobalOverflow() {
    const Scene s = MakeScene(1280, 720, 16, 64, /*maxIndices*/ 5000);
    std::vector<PointLightGPU> pts(50);
    for (auto& p : pts) { p = {}; p.position = {0, 2, -10}; p.radius = 6.0f; }
    ClusterCullResult r;
    CullLightsCPU(s.grid, s.aabbs, s.view, pts.data(), 50, nullptr, 0, r);
    CHECK(r.overflow);
    CHECK(r.requestedIndices > 5000);
    CHECK(r.indices.size() == 5000);
    uint64_t sum = 0;
    for (const ClusterCell& c : r.cells) {
        sum += c.count;
        CHECK(uint64_t(c.offset) + c.count <= 5000 || c.count == 0);
    }
    CHECK(sum <= 5000);
}

static void TestScan() {
    std::mt19937 gen(7);
    for (size_t n : {size_t(1), size_t(2), size_t(255), size_t(256), size_t(257), size_t(1000), size_t(65536),
                     size_t(100000), size_t(300000), size_t(1100000)}) {
        std::vector<uint32_t> counts(n);
        for (auto& c : counts) c = gen() % 5;
        const ScanResult r = HierarchicalExclusiveScan(counts);
        std::vector<uint32_t> ref(n);
        uint64_t run = 0;
        for (size_t i = 0; i < n; ++i) { ref[i] = uint32_t(run); run += counts[i]; }
        CHECK(r.offsets == ref);
        CHECK(r.total == run);
    }
    CHECK(HierarchicalExclusiveScan({}).total == 0);
}

static void TestMemoryBudget() {
    // 1080p, tile 16, 24 fatias: ~196k clusters. Com 8 índices/cluster, tudo cabe em poucos MiB
    // (o esquema fixo 64 x clusters do documento original usava ~190 MB só de índices).
    const Scene s = MakeScene(1920, 1080, 16);
    const uint64_t clusters = ClusterCount(s.grid);
    CHECK(clusters == 120u * 68u * 24u);
    const uint64_t bytes = clusters * (sizeof(ClusterAABB) + 4 + sizeof(ClusterCell)) + uint64_t(s.grid.maxIndices) * 4;
    CHECK(bytes < 16ull * 1024 * 1024);
    CHECK(SuggestClusterTileSize(3840, 2160, QualityTier::High) == 32);
    CHECK(SuggestClusterTileSize(1920, 1080, QualityTier::High) == 16);
}

int main() {
    TestSlices();
    TestAabbContainsPoints();
    TestIntersections();
    TestCullingInvariant();
    TestPerClusterCap();
    TestGlobalOverflow();
    TestScan();
    TestMemoryBudget();
    if (g_failures == 0) std::printf("Testes de cluster: todos passaram.\n");
    else std::printf("Testes de cluster: %d falha(s).\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
