// Simplificacao de malha (tool/render/mesh_simplify.hpp): reduz faces sem criar buracos e sem mexer em quinas e bordas.
#include "tool/render/mesh_simplify.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>
#include <set>
#include <string>
#include <utility>

namespace {

int gFailures = 0;

void check(bool ok, const char* what) {
    std::printf("  %s %s\n", ok ? "ok:  " : "FAIL:", what);
    if (!ok) ++gFailures;
}

// Esfera por subdivisao de um icosaedro. Vertices compartilhados (malha fechada e soldada).
tool::Mesh MakeIcosphere(int level) {
    using tool::Vec3;
    const float t = (1.f + std::sqrt(5.f)) * 0.5f;
    std::vector<Vec3> v = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t},
                           {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
    for (Vec3& p : v) { const float l = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z); p = {p.x / l, p.y / l, p.z / l}; }
    std::vector<std::array<uint32_t, 3>> f = {{0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4},
                                              {11, 10, 2}, {10, 7, 6}, {7, 1, 8}, {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8},
                                              {3, 8, 9}, {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}};
    for (int l = 0; l < level; ++l) {
        std::map<std::pair<uint32_t, uint32_t>, uint32_t> mid;
        auto midpoint = [&](uint32_t a, uint32_t b) {
            const auto key = std::minmax(a, b);
            auto it = mid.find({key.first, key.second});
            if (it != mid.end()) return it->second;
            Vec3 p{(v[a].x + v[b].x) * 0.5f, (v[a].y + v[b].y) * 0.5f, (v[a].z + v[b].z) * 0.5f};
            const float len = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
            p = {p.x / len, p.y / len, p.z / len};
            v.push_back(p);
            const uint32_t id = static_cast<uint32_t>(v.size() - 1);
            mid[{key.first, key.second}] = id;
            return id;
        };
        std::vector<std::array<uint32_t, 3>> nf;
        for (auto& tri : f) {
            const uint32_t a = midpoint(tri[0], tri[1]), b = midpoint(tri[1], tri[2]), c = midpoint(tri[2], tri[0]);
            nf.push_back({tri[0], a, c});
            nf.push_back({tri[1], b, a});
            nf.push_back({tri[2], c, b});
            nf.push_back({a, b, c});
        }
        f = nf;
    }
    tool::Mesh m;
    m.pos = v;
    m.normal = v;
    for (const auto& tri : f) m.idx.insert(m.idx.end(), tri.begin(), tri.end());
    return m;
}

// Malha plana (grade n x n de quads), com borda.
tool::Mesh MakeGrid(int n) {
    tool::Mesh m;
    for (int y = 0; y <= n; ++y)
        for (int x = 0; x <= n; ++x) m.pos.push_back({float(x), 0.f, float(y)});
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            const uint32_t a = y * (n + 1) + x, b = a + 1, c = a + (n + 1) + 1, d = a + (n + 1);
            m.idx.insert(m.idx.end(), {a, b, c, a, c, d});
        }
    return m;
}

// Euler de uma malha fechada: V - E + F (esfera = 2). Conta so os vertices usados.
long EulerOf(const tool::Mesh& m) {
    std::set<uint32_t> used(m.idx.begin(), m.idx.end());
    std::set<std::pair<uint32_t, uint32_t>> edges;
    for (size_t t = 0; t + 2 < m.idx.size(); t += 3)
        for (int c = 0; c < 3; ++c) {
            uint32_t a = m.idx[t + c], b = m.idx[t + (c + 1) % 3];
            edges.insert(std::minmax(a, b));
        }
    return static_cast<long>(used.size()) - static_cast<long>(edges.size()) + static_cast<long>(m.idx.size() / 3);
}

// Nenhuma face com area zero.
bool NoDegenerate(const tool::Mesh& m) {
    for (size_t t = 0; t + 2 < m.idx.size(); t += 3) {
        const tool::Vec3 a = m.pos[m.idx[t]], b = m.pos[m.idx[t + 1]], c = m.pos[m.idx[t + 2]];
        const float ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z, vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
        const float cx = uy * vz - uz * vy, cy = uz * vx - ux * vz, cz = ux * vy - uy * vx;
        if (cx * cx + cy * cy + cz * cz < 1e-12f) return false;
    }
    return true;
}

float MaxRadius(const tool::Mesh& m) {
    float r = 0.f;
    for (const tool::Vec3& p : m.pos) r = std::max(r, std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z));
    return r;
}

} // namespace

int main() {
    std::printf("Mesh simplification\n");

    {
        const tool::Mesh sphere = MakeIcosphere(3);   // 1280 faces, fechada
        size_t bnd = 0, nm = 0;
        tool::render::MeshEdgeIssues(sphere, bnd, nm);
        check(bnd == 0 && nm == 0 && EulerOf(sphere) == 2, "icosphere input is closed and manifold");

        const tool::Mesh half = tool::render::SimplifyMesh(sphere, 0.5f);
        const size_t fOrig = sphere.idx.size() / 3, fHalf = half.idx.size() / 3;
        std::printf("    faces: %zu -> %zu\n", fOrig, fHalf);
        check(fHalf < fOrig * 0.6 && fHalf > fOrig * 0.3, "half ratio removes about half the faces");
        tool::render::MeshEdgeIssues(half, bnd, nm);
        check(bnd == 0 && nm == 0, "simplified sphere has no holes and no non-manifold edges");
        check(EulerOf(half) == 2, "simplified sphere keeps topology (Euler characteristic 2)");
        check(NoDegenerate(half), "simplified sphere has no degenerate faces");
        check(std::fabs(MaxRadius(half) - 1.f) < 0.05f, "simplified sphere keeps its radius");
        bool idxOk = true;
        for (uint32_t i : half.idx) idxOk &= i < half.pos.size();
        check(idxOk && half.normal.size() == half.pos.size(), "indices in range and one normal per vertex");
    }

    {
        const tool::Mesh sphere = MakeIcosphere(3);
        const tool::Mesh small = tool::render::SimplifyMesh(sphere, 0.1f);
        size_t bnd = 0, nm = 0;
        tool::render::MeshEdgeIssues(small, bnd, nm);
        check(small.idx.size() / 3 < sphere.idx.size() / 3 / 4 && bnd == 0 && nm == 0 && EulerOf(small) == 2,
              "aggressive ratio still closed and smaller");
    }

    {
        const tool::Mesh cube = tool::Mesh::cube(0.5f);   // quinas vivas: nada deve colapsar
        const tool::Mesh out = tool::render::SimplifyMesh(cube, 0.25f);
        check(out.idx.size() == cube.idx.size(), "cube with hard edges is not simplified");
    }

    {
        const tool::Mesh grid = MakeGrid(10);   // plano com borda: a borda nao anda
        size_t bndIn = 0, nmIn = 0;
        tool::render::MeshEdgeIssues(grid, bndIn, nmIn);
        const tool::Mesh out = tool::render::SimplifyMesh(grid, 0.4f);
        size_t bnd = 0, nm = 0;
        tool::render::MeshEdgeIssues(out, bnd, nm);
        std::printf("    plane faces: %zu -> %zu, border edges %zu -> %zu\n", grid.idx.size() / 3, out.idx.size() / 3, bndIn, bnd);
        check(out.idx.size() < grid.idx.size() && bnd == bndIn && nm == 0, "flat grid shrinks and keeps its border");
    }

    {
        const tool::Mesh sphere = MakeIcosphere(2);
        const tool::Mesh same = tool::render::SimplifyMesh(sphere, 1.f);
        check(same.idx.size() == sphere.idx.size(), "ratio 1 returns the mesh unchanged");
    }

    std::printf(gFailures == 0 ? "All mesh simplification checks passed.\n" : "%d check(s) FAILED.\n", gFailures);
    return gFailures == 0 ? 0 : 1;
}
