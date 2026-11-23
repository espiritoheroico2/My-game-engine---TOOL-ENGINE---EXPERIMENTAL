#pragma once
// Simplificacao de malha por colapso de arestas (Garland-Heckbert, quadricas de erro), sem criar buracos.
//
// Uso: malhas distantes ou pouco visiveis viram uma versao com menos faces. Nao e um LOD de verdade (nao troca
// de modelo por distancia): quem chama escolhe quando usar a versao simplificada e guarda o resultado em cache.
//
// Regras que mantem a malha fechada e a silhueta:
//   - Vertices soldados por posicao: a costura de UV/normal do arquivo nao abre buracos.
//   - Bordas de malha aberta (uma face so) e arestas nao-manifold travam os vertices: a borda nao anda.
//   - Vertices de quina (faces vizinhas com normais diferentes, acima de ~35 graus) travam: o volume nao arredonda.
//   - Vertices com UV diferente entre faces (costura de textura) travam: a textura nao se estica.
//   - Colapso so entra se a condicao de link vale (dois vertices em comum) e nenhuma face vizinha inverte.
// Atributos do resultado: posicao e UV do vertice que sobrou; normal = media das faces em volta (suave).
#include "tool/render/soft_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace tool::render {

namespace simplify_detail {

// Quadrica simetrica 4x4 guardada em 10 valores (triangulo superior).
struct Quadric {
    double a[10]{};
    void addPlane(double nx, double ny, double nz, double d, double w) {
        const double v[10] = {nx * nx, nx * ny, nx * nz, nx * d, ny * ny, ny * nz, ny * d, nz * nz, nz * d, d * d};
        for (int i = 0; i < 10; ++i) a[i] += w * v[i];
    }
    void add(const Quadric& o) { for (int i = 0; i < 10; ++i) a[i] += o.a[i]; }
    // Erro v^T Q v para o ponto (x, y, z, 1).
    double error(double x, double y, double z) const {
        return a[0] * x * x + 2 * a[1] * x * y + 2 * a[2] * x * z + 2 * a[3] * x + a[4] * y * y + 2 * a[5] * y * z +
               2 * a[6] * y + a[7] * z * z + 2 * a[8] * z + a[9];
    }
};

struct Key { std::uint32_t x, y, z; bool operator==(const Key& o) const { return x == o.x && y == o.y && z == o.z; } };
struct KeyHash { size_t operator()(const Key& k) const { return k.x * 73856093u ^ k.y * 19349663u ^ k.z * 83492791u; } };

inline std::uint32_t bitsOf(float f) { std::uint32_t b; std::memcpy(&b, &f, 4); return b; }

inline Vec3 vsub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 vcross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline double vdot(Vec3 a, Vec3 b) { return double(a.x) * b.x + double(a.y) * b.y + double(a.z) * b.z; }

} // namespace simplify_detail

// Reduz as faces de `in` ate cerca de keepRatio * (faces originais), em (0, 1]. Devolve uma malha nova.
// keepRatio >= 1 ou malha sem triangulos: devolve uma copia sem simplificar.
inline Mesh SimplifyMesh(const Mesh& in, float keepRatio) {
    using namespace simplify_detail;
    if (in.idx.size() < 3 || in.pos.empty() || keepRatio >= 1.f) return in;
    const bool hasUv = in.uv.size() >= in.pos.size() * 2;
    const size_t origFaces = in.idx.size() / 3;
    const size_t targetFaces = std::max<size_t>(1, static_cast<size_t>(std::llround(double(origFaces) * std::max(0.f, keepRatio))));

    // 1) Solda por posicao. vmap leva indice original -> soldado; cada soldado guarda posicao, UV e a primeira ocorrencia.
    std::unordered_map<Key, std::uint32_t, KeyHash> weld;
    std::vector<std::uint32_t> vmap(in.pos.size());
    std::vector<Vec3> pos;
    std::vector<std::pair<float, float>> uv;
    std::vector<char> uvSeam;
    for (size_t i = 0; i < in.pos.size(); ++i) {
        const Vec3& p = in.pos[i];
        const Key k{bitsOf(p.x), bitsOf(p.y), bitsOf(p.z)};
        auto [it, fresh] = weld.try_emplace(k, static_cast<std::uint32_t>(pos.size()));
        if (fresh) {
            pos.push_back(p);
            uv.push_back(hasUv ? std::pair{in.uv[i * 2], in.uv[i * 2 + 1]} : std::pair{0.f, 0.f});
            uvSeam.push_back(0);
        }
        vmap[i] = it->second;
    }
    const std::uint32_t nv = static_cast<std::uint32_t>(pos.size());

    // 2) Faces soldadas (sem triangulos degenerados) e primeira UV de cada vertice. UV diferente = costura.
    struct Tri { std::uint32_t v[3]; Vec3 n; bool alive = true; };
    std::vector<Tri> tris;
    tris.reserve(origFaces);
    for (size_t t = 0; t + 2 < in.idx.size(); t += 3) {
        std::uint32_t w[3];
        bool ok = true;
        for (int c = 0; c < 3; ++c) { if (in.idx[t + c] >= in.pos.size()) ok = false; else w[c] = vmap[in.idx[t + c]]; }
        if (!ok || w[0] == w[1] || w[1] == w[2] || w[0] == w[2]) continue;
        if (hasUv)
            for (int c = 0; c < 3; ++c) {
                const std::uint32_t src = in.idx[t + c];
                if (in.uv[src * 2] != uv[w[c]].first || in.uv[src * 2 + 1] != uv[w[c]].second) uvSeam[w[c]] = 1;
            }
        const Vec3 n = vcross(vsub(pos[w[1]], pos[w[0]]), vsub(pos[w[2]], pos[w[0]]));
        const double len = std::sqrt(vdot(n, n));
        if (len <= 1e-20) continue;   // area zero: nao entra
        tris.push_back({{w[0], w[1], w[2]}, {float(n.x / len), float(n.y / len), float(n.z / len)}, true});
    }
    if (tris.empty()) return in;

    // 3) Arestas: quantas faces em cada uma. Vertices de borda, nao-manifold, quina ou costura ficam travados.
    std::vector<std::vector<std::uint32_t>> vfaces(nv);
    std::unordered_map<std::uint64_t, std::uint32_t> edgeCount;   // quantas faces usam a aresta
    std::unordered_map<std::uint64_t, std::uint32_t> edgeFace;    // primeira face da aresta (para o angulo diedro)
    auto ek = [](std::uint32_t a, std::uint32_t b) { return a < b ? (std::uint64_t(a) << 32) | b : (std::uint64_t(b) << 32) | a; };
    for (std::uint32_t f = 0; f < tris.size(); ++f) {
        for (int c = 0; c < 3; ++c) {
            vfaces[tris[f].v[c]].push_back(f);
            const std::uint64_t k = ek(tris[f].v[c], tris[f].v[(c + 1) % 3]);
            if (++edgeCount[k] == 1) edgeFace[k] = f;
        }
    }
    std::vector<char> locked(nv, 0);
    for (const auto& [key, cnt] : edgeCount)
        if (cnt != 2) { locked[key >> 32] = 1; locked[key & 0xffffffffu] = 1; }
    // Aresta de quina: as duas faces formam mais de ~35 graus. Os dois vertices dela ficam presos.
    const double cosHard = std::cos(35.0 * 3.14159265358979 / 180.0);
    for (std::uint32_t f = 0; f < tris.size(); ++f) {
        for (int c = 0; c < 3; ++c) {
            const std::uint32_t a = tris[f].v[c], b = tris[f].v[(c + 1) % 3];
            const std::uint64_t k = ek(a, b);
            if (edgeCount[k] == 2 && edgeFace[k] != f && vdot(tris[f].n, tris[edgeFace[k]].n) < cosHard) { locked[a] = 1; locked[b] = 1; }
        }
    }
    for (std::uint32_t v = 0; v < nv; ++v) if (uvSeam[v]) locked[v] = 1;

    // 4) Quadricas: soma dos planos das faces em volta de cada vertice (ponderado por area: o plano ja usa normal unitaria).
    std::vector<Quadric> Q(nv);
    for (const Tri& t : tris) {
        const Vec3 p0 = pos[t.v[0]];
        const double d = -(double(t.n.x) * p0.x + double(t.n.y) * p0.y + double(t.n.z) * p0.z);
        for (int c = 0; c < 3; ++c) Q[t.v[c]].addPlane(t.n.x, t.n.y, t.n.z, d, 1.0);
    }

    // 5) Vizinhos por vertice (conjunto, sem repeticao).
    std::vector<std::unordered_set<std::uint32_t>> nb(nv);
    for (const Tri& t : tris)
        for (int c = 0; c < 3; ++c) { nb[t.v[c]].insert(t.v[(c + 1) % 3]); nb[t.v[(c + 1) % 3]].insert(t.v[c]); }

    std::vector<std::uint32_t> version(nv, 0);
    std::vector<char> removed(nv, 0);

    // Melhor posicao para o colapso: entre as extremidades e o ponto medio, a de menor erro.
    auto bestPlace = [&](std::uint32_t a, std::uint32_t b, Vec3& out) {
        const Quadric q = [&] { Quadric s = Q[a]; s.add(Q[b]); return s; }();
        const Vec3 cand[3] = {pos[a], pos[b], {(pos[a].x + pos[b].x) * 0.5f, (pos[a].y + pos[b].y) * 0.5f, (pos[a].z + pos[b].z) * 0.5f}};
        double best = std::numeric_limits<double>::max();
        for (const Vec3& c : cand) {
            const double e = q.error(c.x, c.y, c.z);
            if (e < best) { best = e; out = c; }
        }
        return best;
    };

    struct Cand { double cost; std::uint32_t a, b; std::uint32_t va, vb; bool operator>(const Cand& o) const { return cost > o.cost; } };
    std::priority_queue<Cand, std::vector<Cand>, std::greater<Cand>> pq;
    auto push = [&](std::uint32_t a, std::uint32_t b) {
        if (locked[a] || locked[b]) return;
        Vec3 p;
        const double cost = bestPlace(a, b, p);
        pq.push({cost, a, b, version[a], version[b]});
    };
    for (std::uint32_t v = 0; v < nv; ++v)
        for (std::uint32_t u : nb[v]) if (v < u) push(v, u);

    // Teste de inversao: depois de mover a para p (e b para a), nenhuma face em volta pode virar do avesso.
    auto flips = [&](std::uint32_t a, std::uint32_t b, const Vec3& p) {
        for (int side = 0; side < 2; ++side) {
            const std::uint32_t keep = side == 0 ? a : b, gone = side == 0 ? b : a;
            for (std::uint32_t f : vfaces[gone]) {
                const Tri& t = tris[f];
                if (!t.alive) continue;
                if (std::find(t.v, t.v + 3, keep) != t.v + 3) continue;   // face que tem a e b: sera apagada
                Vec3 q[3];
                for (int c = 0; c < 3; ++c) q[c] = t.v[c] == gone ? p : pos[t.v[c]];
                const Vec3 n = vcross(vsub(q[1], q[0]), vsub(q[2], q[0]));
                if (vdot(n, n) < 1e-20 || vdot(n, t.n) <= 0.2 * std::sqrt(vdot(n, n))) return true;
            }
        }
        return false;
    };

    std::uint32_t aliveFaces = static_cast<std::uint32_t>(tris.size());
    while (aliveFaces > targetFaces && !pq.empty()) {
        const Cand c = pq.top();
        pq.pop();
        if (removed[c.a] || removed[c.b] || version[c.a] != c.va || version[c.b] != c.vb) continue;
        // Link: vertices em comum tem de ser exatamente os das faces que contem a aresta (2 para aresta interna).
        size_t common = 0;
        for (std::uint32_t u : nb[c.a]) if (nb[c.b].count(u)) ++common;
        if (common != 2) continue;
        Vec3 p;
        bestPlace(c.a, c.b, p);
        if (flips(c.a, c.b, p)) continue;

        // Aplica: b some; a passa a ser o vertice unico e recebe a posicao p e a quadrica somada.
        pos[c.a] = p;
        Q[c.a].add(Q[c.b]);
        std::uint32_t killed = 0;
        for (std::uint32_t f : vfaces[c.b]) {
            Tri& t = tris[f];
            if (!t.alive) continue;
            if (std::find(t.v, t.v + 3, c.a) != t.v + 3) { t.alive = false; ++killed; continue; }
            for (int k = 0; k < 3; ++k) if (t.v[k] == c.b) t.v[k] = c.a;
            vfaces[c.a].push_back(f);
        }
        aliveFaces -= killed;
        for (std::uint32_t u : nb[c.b]) { nb[u].erase(c.b); if (u != c.a) { nb[u].insert(c.a); nb[c.a].insert(u); } }
        nb[c.a].erase(c.b);
        nb[c.a].erase(c.a);
        nb[c.b].clear();
        vfaces[c.b].clear();
        removed[c.b] = 1;
        ++version[c.a];
        // Recalcula as arestas em volta de a (custo e versao mudaram).
        for (std::uint32_t u : nb[c.a]) push(c.a, u);
    }

    // 6) Saida: faces vivas com os vertices que sobraram, em ordem; normal = media suave das faces em volta.
    std::vector<std::uint32_t> outIndex(nv, std::numeric_limits<std::uint32_t>::max());
    Mesh out;
    std::vector<Vec3> acc;
    auto indexOf = [&](std::uint32_t v) {
        if (outIndex[v] == std::numeric_limits<std::uint32_t>::max()) {
            outIndex[v] = static_cast<std::uint32_t>(out.pos.size());
            out.pos.push_back(pos[v]);
            if (hasUv) { out.uv.push_back(uv[v].first); out.uv.push_back(uv[v].second); }
            acc.push_back({0, 0, 0});
        }
        return outIndex[v];
    };
    for (const Tri& t : tris) {
        if (!t.alive) continue;
        const Vec3 n = vcross(vsub(pos[t.v[1]], pos[t.v[0]]), vsub(pos[t.v[2]], pos[t.v[0]]));
        for (int c = 0; c < 3; ++c) {
            const std::uint32_t o = indexOf(t.v[c]);
            out.idx.push_back(o);
            acc[o].x += n.x; acc[o].y += n.y; acc[o].z += n.z;
        }
    }
    out.normal.reserve(out.pos.size());
    for (const Vec3& a : acc) {
        const float len = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
        out.normal.push_back(len > 1e-12f ? Vec3{a.x / len, a.y / len, a.z / len} : Vec3{0, 1, 0});
    }
    return out;
}

// Contagem de faces de borda (uma so) e de arestas nao-manifold (mais de duas): 0 e 0 para malha fechada e limpa.
inline void MeshEdgeIssues(const Mesh& m, size_t& boundary, size_t& nonManifold) {
    std::unordered_map<std::uint64_t, std::uint32_t> cnt;
    auto ek = [](std::uint32_t a, std::uint32_t b) { return a < b ? (std::uint64_t(a) << 32) | b : (std::uint64_t(b) << 32) | a; };
    for (size_t t = 0; t + 2 < m.idx.size(); t += 3)
        for (int c = 0; c < 3; ++c) ++cnt[ek(m.idx[t + c], m.idx[t + (c + 1) % 3])];
    boundary = nonManifold = 0;
    for (const auto& [k, c] : cnt) { if (c == 1) ++boundary; else if (c > 2) ++nonManifold; }
}

} // namespace tool::render
