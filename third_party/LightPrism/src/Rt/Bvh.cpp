#include "LightPrism/Rt/Bvh.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

#include "LightPrism/Util.hpp"

namespace LightPrism::Rt {

namespace {

constexpr uint32_t kStackSize = 128;

struct BNode {
    Aabb box;
    int left = -1, right = -1;
    uint32_t first = 0, count = 0;  // folha: faixa em `order`
};

Aabb TriBox(const Triangle& t) {
    Aabb b;
    b.Grow(t.v0); b.Grow(t.v1); b.Grow(t.v2);
    return b;
}

float Axis(const Vec3& v, int a) { return a == 0 ? v.x : (a == 1 ? v.y : v.z); }

// ---------------------------------------------------------------- SAH binned
struct SahCtx {
    std::vector<BNode>& nodes;
    std::vector<uint32_t>& order;
    const std::vector<Aabb>& boxes;
    const std::vector<Vec3>& cent;
    uint32_t maxLeaf;
};

int BuildSah(SahCtx& c, uint32_t begin, uint32_t end) {
    const int self = int(c.nodes.size());
    c.nodes.emplace_back();
    Aabb box, cbox;
    for (uint32_t i = begin; i < end; ++i) { box.Grow(c.boxes[c.order[i]]); cbox.Grow(c.cent[c.order[i]]); }
    c.nodes[self].box = box;
    const uint32_t n = end - begin;
    if (n <= 1) { c.nodes[self].first = begin; c.nodes[self].count = n; return self; }

    constexpr int kBins = 16;
    float bestCost = 1e30f; int bestAxis = -1; int bestSplit = 0;
    const float parentArea = std::max(box.Area(), 1e-20f);
    for (int a = 0; a < 3; ++a) {
        const float lo = Axis(cbox.lo, a), ext = Axis(cbox.hi, a) - lo;
        if (ext <= 1e-12f) continue;
        Aabb bb[kBins]; uint32_t cnt[kBins] = {};
        const float k = float(kBins) * (1.0f - 1e-6f) / ext;
        for (uint32_t i = begin; i < end; ++i) {
            const int b = std::min(kBins - 1, int((Axis(c.cent[c.order[i]], a) - lo) * k));
            ++cnt[b]; bb[b].Grow(c.boxes[c.order[i]]);
        }
        float areaL[kBins - 1]; uint32_t cntL[kBins - 1];
        Aabb acc; uint32_t cc = 0;
        for (int b = 0; b < kBins - 1; ++b) { acc.Grow(bb[b]); cc += cnt[b]; areaL[b] = acc.Area(); cntL[b] = cc; }
        acc = Aabb{}; cc = 0;
        for (int b = kBins - 1; b >= 1; --b) {
            acc.Grow(bb[b]); cc += cnt[b];
            const float cost = areaL[b - 1] * float(cntL[b - 1]) + acc.Area() * float(cc);
            if (cntL[b - 1] > 0 && cc > 0 && cost < bestCost) { bestCost = cost; bestAxis = a; bestSplit = b; }
        }
    }
    const float leafCost = float(n);
    const float splitCost = 1.0f + bestCost / parentArea;
    if (n <= c.maxLeaf && (bestAxis < 0 || leafCost <= splitCost)) { c.nodes[self].first = begin; c.nodes[self].count = n; return self; }

    uint32_t mid = begin + n / 2;
    if (bestAxis >= 0) {
        const float lo = Axis(cbox.lo, bestAxis), ext = Axis(cbox.hi, bestAxis) - lo;
        const float k = float(kBins) * (1.0f - 1e-6f) / ext;
        auto* it = std::partition(c.order.data() + begin, c.order.data() + end, [&](uint32_t i) {
            return std::min(kBins - 1, int((Axis(c.cent[i], bestAxis) - lo) * k)) < bestSplit;
        });
        mid = uint32_t(it - c.order.data());
    }
    if (mid == begin || mid == end) {  // degenerado: divide pela mediana do eixo mais longo (ou ao meio)
        int a = 0; const Vec3 e = cbox.Extent();
        if (e.y > e.x && e.y >= e.z) a = 1; else if (e.z > e.x && e.z > e.y) a = 2;
        mid = begin + n / 2;
        std::nth_element(c.order.begin() + begin, c.order.begin() + mid, c.order.begin() + end,
                         [&](uint32_t x, uint32_t y) { return Axis(c.cent[x], a) < Axis(c.cent[y], a); });
    }
    const int l = BuildSah(c, begin, mid);
    const int r = BuildSah(c, mid, end);
    c.nodes[self].left = l; c.nodes[self].right = r;
    return self;
}

// ---------------------------------------------------------------- LBVH (Karras 2012)
uint32_t ExpandBits(uint32_t v) {
    v = (v * 0x00010001u) & 0xFF0000FFu;
    v = (v * 0x00000101u) & 0x0F00F00Fu;
    v = (v * 0x00000011u) & 0xC30C30C3u;
    v = (v * 0x00000005u) & 0x49249249u;
    return v;
}

uint32_t Morton3D(float x, float y, float z) {  // x,y,z em [0,1]
    const auto q = [](float v) { return uint32_t(std::min(std::max(v * 1024.0f, 0.0f), 1023.0f)); };
    return (ExpandBits(q(x)) << 2) | (ExpandBits(q(y)) << 1) | ExpandBits(q(z));
}

struct LbvhCtx {
    std::vector<uint32_t> codes;               // ordenados
    std::vector<int> left, right;              // nós internos 0..n-2; folhas = n-1+i
    std::vector<uint32_t> rangeLo, rangeHi;    // faixa coberta por cada nó interno
    int n = 0;
    int Delta(int i, int j) const {
        if (j < 0 || j >= n) return -1;
        return codes[i] != codes[j] ? Clz32(codes[i] ^ codes[j]) : 32 + Clz32(uint32_t(i) ^ uint32_t(j));
    }
};

void BuildLbvhTopology(LbvhCtx& c) {
    const int n = c.n;
    c.left.assign(size_t(n - 1), 0); c.right.assign(size_t(n - 1), 0);
    c.rangeLo.assign(size_t(n - 1), 0); c.rangeHi.assign(size_t(n - 1), 0);
    for (int i = 0; i < n - 1; ++i) {
        const int d = (c.Delta(i, i + 1) - c.Delta(i, i - 1)) >= 0 ? 1 : -1;
        const int dmin = c.Delta(i, i - d);
        int lmax = 2;
        while (c.Delta(i, i + lmax * d) > dmin) lmax *= 2;
        int l = 0;
        for (int t = lmax / 2; t >= 1; t /= 2)
            if (c.Delta(i, i + (l + t) * d) > dmin) l += t;
        const int j = i + l * d;
        const int dnode = c.Delta(i, j);
        int s = 0;
        for (int t = (l + 1) / 2;; t = (t + 1) / 2) {
            if (c.Delta(i, i + (s + t) * d) > dnode) s += t;
            if (t == 1) break;
        }
        const int gamma = i + s * d + std::min(d, 0);
        const int lo = std::min(i, j), hi = std::max(i, j);
        c.left[i] = lo == gamma ? (n - 1 + gamma) : gamma;
        c.right[i] = hi == gamma + 1 ? (n - 1 + gamma + 1) : gamma + 1;
        c.rangeLo[i] = uint32_t(lo); c.rangeHi[i] = uint32_t(hi);
    }
}

int CollapseLbvh(const LbvhCtx& c, int node, std::vector<BNode>& out, const std::vector<Aabb>& boxesSorted, uint32_t maxLeaf) {
    const int self = int(out.size());
    out.emplace_back();
    const bool isLeaf = node >= c.n - 1;
    const uint32_t lo = isLeaf ? uint32_t(node - (c.n - 1)) : c.rangeLo[node];
    const uint32_t hi = isLeaf ? lo : c.rangeHi[node];
    const uint32_t count = hi - lo + 1;
    Aabb box;
    for (uint32_t i = lo; i <= hi; ++i) box.Grow(boxesSorted[i]);
    out[self].box = box;
    if (isLeaf || count <= maxLeaf) { out[self].first = lo; out[self].count = count; return self; }
    const int l = CollapseLbvh(c, c.left[node], out, boxesSorted, maxLeaf);
    const int r = CollapseLbvh(c, c.right[node], out, boxesSorted, maxLeaf);
    out[self].left = l; out[self].right = r;
    return self;
}

// ---------------------------------------------------------------- achatamento (filhos adjacentes)
void Flatten(const std::vector<BNode>& tree, std::vector<BvhNode>& out, uint32_t& depth) {
    out.clear();
    out.resize(1);
    struct Item { int tree; uint32_t flat; uint32_t depth; };
    std::vector<Item> work{{0, 0, 1}};
    depth = 1;
    while (!work.empty()) {
        const Item it = work.back(); work.pop_back();
        const BNode& b = tree[it.tree];
        BvhNode& o = out[it.flat];
        o.bmin[0] = b.box.lo.x; o.bmin[1] = b.box.lo.y; o.bmin[2] = b.box.lo.z;
        o.bmax[0] = b.box.hi.x; o.bmax[1] = b.box.hi.y; o.bmax[2] = b.box.hi.z;
        depth = std::max(depth, it.depth);
        if (b.left < 0) { o.leftOrFirst = b.first; o.count = b.count; continue; }
        const uint32_t c0 = uint32_t(out.size());
        out.resize(out.size() + 2);
        out[it.flat].leftOrFirst = c0; out[it.flat].count = 0;
        work.push_back({b.left, c0, it.depth + 1});
        work.push_back({b.right, c0 + 1, it.depth + 1});
    }
}

// ---------------------------------------------------------------- travessia
// Interseção raio-caixa (slab). Retorna t de entrada, ou +inf se errar. Robusto a dir = 0 (inv = inf).
inline float SlabNear(const BvhNode& n, const Vec3& o, const Vec3& inv, float tMin, float tMax) {
    float t0 = tMin, t1 = tMax;
    const float lo[3] = {n.bmin[0], n.bmin[1], n.bmin[2]}, hi[3] = {n.bmax[0], n.bmax[1], n.bmax[2]};
    const float org[3] = {o.x, o.y, o.z}, iv[3] = {inv.x, inv.y, inv.z};
    for (int a = 0; a < 3; ++a) {
        float ta = (lo[a] - org[a]) * iv[a], tb = (hi[a] - org[a]) * iv[a];
        if (ta > tb) std::swap(ta, tb);
        tb *= 1.0000004f;                               // folga de arredondamento
        t0 = ta > t0 ? ta : t0;                         // NaN (0*inf) é ignorado pelas comparações
        t1 = tb < t1 ? tb : t1;
    }
    return t0 <= t1 ? t0 : 1e38f;
}

inline Vec3 Inv(const Vec3& d) {
    const auto f = [](float v) { return v != 0.0f ? 1.0f / v : 1e30f; };
    return {f(d.x), f(d.y), f(d.z)};
}

} // namespace

bool IntersectTriangle(const Triangle& t, const Ray& r, float tMax, float& tHit, float& u, float& v) {
    const Vec3 e1 = t.v1 - t.v0, e2 = t.v2 - t.v0;
    const Vec3 p = Cross(r.dir, e2);
    const float det = Dot(e1, p);
    if (std::fabs(det) < 1e-12f) return false;
    const float inv = 1.0f / det;
    const Vec3 s = r.origin - t.v0;
    const float uu = Dot(s, p) * inv;
    if (uu < -1e-6f || uu > 1.0f + 1e-6f) return false;
    const Vec3 q = Cross(s, e1);
    const float vv = Dot(r.dir, q) * inv;
    if (vv < -1e-6f || uu + vv > 1.0f + 1e-6f) return false;
    const float tt = Dot(e2, q) * inv;
    if (tt < r.tMin || tt > tMax) return false;
    tHit = tt; u = uu; v = vv;
    return true;
}

bool IntersectBruteForce(const std::vector<Triangle>& tris, const Ray& r, Hit& hit) {
    float best = r.tMax; bool found = false;
    for (uint32_t i = 0; i < tris.size(); ++i) {
        float t, u, v;
        if (IntersectTriangle(tris[i], r, best, t, u, v)) { best = t; hit = {t, u, v, i}; found = true; }
    }
    return found;
}

void Bvh::Build(const std::vector<Triangle>& tris, BvhBuild mode, uint32_t maxLeaf) {
    m_nodes.clear(); m_tris.clear(); m_triIndex.clear(); m_bounds = Aabb{}; m_depth = 0;
    const uint32_t n = uint32_t(tris.size());
    if (n == 0) return;
    maxLeaf = std::max(maxLeaf, 1u);
    std::vector<Aabb> boxes(n);
    std::vector<Vec3> cent(n);
    for (uint32_t i = 0; i < n; ++i) {
        boxes[i] = TriBox(tris[i]);
        cent[i] = (boxes[i].lo + boxes[i].hi) * 0.5f;
        m_bounds.Grow(boxes[i]);
    }
    std::vector<uint32_t> order(n);
    std::iota(order.begin(), order.end(), 0u);
    std::vector<BNode> tree;
    tree.reserve(size_t(n) * 2);

    if (mode == BvhBuild::BinnedSah || n == 1) {
        SahCtx c{tree, order, boxes, cent, maxLeaf};
        BuildSah(c, 0, n);
    } else {
        const Vec3 ext = m_bounds.Extent();
        const auto norm = [](float v, float lo, float e) { return e > 1e-20f ? (v - lo) / e : 0.0f; };
        std::vector<std::pair<uint32_t, uint32_t>> keyed(n);
        for (uint32_t i = 0; i < n; ++i)
            keyed[i] = {Morton3D(norm(cent[i].x, m_bounds.lo.x, ext.x), norm(cent[i].y, m_bounds.lo.y, ext.y), norm(cent[i].z, m_bounds.lo.z, ext.z)), i};
        std::sort(keyed.begin(), keyed.end());
        LbvhCtx c;
        c.n = int(n);
        c.codes.resize(n);
        std::vector<Aabb> sorted(n);
        for (uint32_t i = 0; i < n; ++i) { c.codes[i] = keyed[i].first; order[i] = keyed[i].second; sorted[i] = boxes[order[i]]; }
        BuildLbvhTopology(c);
        CollapseLbvh(c, 0, tree, sorted, maxLeaf);
    }
    Flatten(tree, m_nodes, m_depth);
    if (m_depth > kStackSize) m_depth = kStackSize; // salvaguarda contra estouro de pilha na travessia
    m_triIndex = order;
    m_tris.resize(n);
    for (uint32_t i = 0; i < n; ++i) m_tris[i] = tris[order[i]];
}

void Bvh::Refit(const std::vector<Triangle>& tris) {
    if (m_nodes.empty() || tris.size() != m_tris.size()) return;
    for (size_t i = 0; i < m_tris.size(); ++i) m_tris[i] = tris[m_triIndex[i]];
    for (size_t k = m_nodes.size(); k-- > 0;) {   // filhos têm índice maior que o pai: ordem reversa é bottom-up
        BvhNode& n = m_nodes[k];
        Aabb b;
        if (n.count > 0) {
            for (uint32_t i = 0; i < n.count; ++i) b.Grow(TriBox(m_tris[n.leftOrFirst + i]));
        } else {
            for (uint32_t c = 0; c < 2; ++c) {
                const BvhNode& ch = m_nodes[n.leftOrFirst + c];
                b.Grow(Vec3{ch.bmin[0], ch.bmin[1], ch.bmin[2]}); b.Grow(Vec3{ch.bmax[0], ch.bmax[1], ch.bmax[2]});
            }
        }
        n.bmin[0] = b.lo.x; n.bmin[1] = b.lo.y; n.bmin[2] = b.lo.z;
        n.bmax[0] = b.hi.x; n.bmax[1] = b.hi.y; n.bmax[2] = b.hi.z;
    }
    const BvhNode& r = m_nodes[0];
    m_bounds.lo = {r.bmin[0], r.bmin[1], r.bmin[2]}; m_bounds.hi = {r.bmax[0], r.bmax[1], r.bmax[2]};
}

bool Bvh::Intersect(const Ray& r, Hit& hit, TraversalStats* st) const {
    if (m_nodes.empty()) return false;
    const Vec3 inv = Inv(r.dir);
    float tMax = r.tMax;
    bool found = false;
    uint32_t stack[kStackSize]; float stackT[kStackSize]; uint32_t sp = 0;
    if (st) ++st->rays;
    float t0 = SlabNear(m_nodes[0], r.origin, inv, r.tMin, tMax);
    if (t0 > tMax) return false;
    uint32_t cur = 0;
    for (;;) {
        const BvhNode& n = m_nodes[cur];
        if (st) ++st->nodesVisited;
        if (n.count > 0) {
            for (uint32_t i = 0; i < n.count; ++i) {
                float t, u, v;
                if (st) ++st->triTests;
                if (IntersectTriangle(m_tris[n.leftOrFirst + i], r, tMax, t, u, v)) {
                    tMax = t; found = true; hit = {t, u, v, m_triIndex[n.leftOrFirst + i]};
                }
            }
        } else {
            const uint32_t c0 = n.leftOrFirst, c1 = c0 + 1;
            const float ta = SlabNear(m_nodes[c0], r.origin, inv, r.tMin, tMax);
            const float tb = SlabNear(m_nodes[c1], r.origin, inv, r.tMin, tMax);
            const bool ha = ta <= tMax, hb = tb <= tMax;
            if (ha && hb) {
                const bool aFirst = ta <= tb;
                stack[sp] = aFirst ? c1 : c0; stackT[sp] = aFirst ? tb : ta; ++sp;
                cur = aFirst ? c0 : c1;
                continue;
            }
            if (ha || hb) { cur = ha ? c0 : c1; continue; }
        }
        // pop (descarta o que ficou atrás de um acerto mais próximo)
        for (;;) {
            if (sp == 0) return found;
            --sp;
            if (stackT[sp] <= tMax) { cur = stack[sp]; break; }
        }
    }
}

bool Bvh::Occluded(const Ray& r, TraversalStats* st) const {
    if (m_nodes.empty()) return false;
    const Vec3 inv = Inv(r.dir);
    uint32_t stack[kStackSize]; uint32_t sp = 0;
    if (st) ++st->rays;
    if (SlabNear(m_nodes[0], r.origin, inv, r.tMin, r.tMax) > r.tMax) return false;
    uint32_t cur = 0;
    for (;;) {
        const BvhNode& n = m_nodes[cur];
        if (st) ++st->nodesVisited;
        if (n.count > 0) {
            for (uint32_t i = 0; i < n.count; ++i) {
                float t, u, v;
                if (st) ++st->triTests;
                if (IntersectTriangle(m_tris[n.leftOrFirst + i], r, r.tMax, t, u, v)) return true;
            }
        } else {
            const uint32_t c0 = n.leftOrFirst, c1 = c0 + 1;
            const bool ha = SlabNear(m_nodes[c0], r.origin, inv, r.tMin, r.tMax) <= r.tMax;
            const bool hb = SlabNear(m_nodes[c1], r.origin, inv, r.tMin, r.tMax) <= r.tMax;
            if (ha && hb) { stack[sp++] = c1; cur = c0; continue; }
            if (ha || hb) { cur = ha ? c0 : c1; continue; }
        }
        if (sp == 0) return false;
        cur = stack[--sp];
    }
}

float Bvh::SahCost() const {
    if (m_nodes.empty()) return 0.0f;
    const auto area = [](const BvhNode& n) {
        const float x = n.bmax[0] - n.bmin[0], y = n.bmax[1] - n.bmin[1], z = n.bmax[2] - n.bmin[2];
        return 2.0f * (x * y + y * z + z * x);
    };
    double cost = 0.0;
    for (const BvhNode& n : m_nodes) cost += double(area(n)) * (n.count > 0 ? double(n.count) : 1.0);
    return float(cost / std::max(double(area(m_nodes[0])), 1e-20));
}

uint64_t Bvh::MemoryBytes() const {
    return m_nodes.size() * sizeof(BvhNode) + m_tris.size() * sizeof(Triangle) + m_triIndex.size() * 4ull;
}

} // namespace LightPrism::Rt
