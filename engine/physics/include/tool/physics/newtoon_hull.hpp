// Newtoon - "capa de colisao" simplificada: casco convexo com poucos pontos, gerado a partir dos vertices de uma malha.
//
// METODO (k-DOP): para um conjunto fixo de direcoes (6 eixos, depois 8 cantos, depois 12 arestas) acha-se o maior
// produto escalar de TODOS os vertices com cada direcao. O poliedro {x : d_i . x <= h_i} contem todos os vertices
// (por construcao) e seus vertices (intersecao de 3 planos) formam o casco. Um poliedro com F faces tem no maximo
// 2F-4 vertices; por isso F e escolhido a partir de maxPoints (padrao 24 -> F=14 -> ate 24 pontos).
// Vantagens: O(n * F), deterministico e independente da ordem dos vertices (so usa maximos), robusto a malhas
// degeneradas (coplanares, pontos repetidos). Desvantagem: e uma capa externa levemente mais folgada que o casco exato.
#pragma once
#include "newtoon_math.hpp"
#include "newtoon_types.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <tuple>
#include <vector>

namespace Newtoon {

// Limite padrao de pontos do casco simplificado (documentado em docs/estudos/FISICA_OTIMIZACAO.md).
constexpr int kDefaultHullMaxPoints = 24;

// Resultado: pontos do casco (no espaco local da malha) e caixa englobante deles.
struct ConvexHull {
  std::vector<Vec3> points;
  AABB bounds{{0.f, 0.f, 0.f}, {0.f, 0.f, 0.f}};
  bool Empty() const { return points.empty(); }
};

namespace hull_detail {

// Direcoes nao normalizadas, na ordem de prioridade: 6 eixos, 8 cantos, 12 arestas.
inline const std::vector<Vec3>& Directions() {
  static const std::vector<Vec3> d = [] {
    std::vector<Vec3> v = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (int s = 0; s < 8; ++s) v.push_back({(s & 1) ? 1.f : -1.f, (s & 2) ? 1.f : -1.f, (s & 4) ? 1.f : -1.f});
    for (int i = 0; i < 3; ++i) for (int a = 0; a < 2; ++a) for (int b = 0; b < 2; ++b) {
      Vec3 e{0, 0, 0}; float* c = &e.x;
      const int i1 = (i + 1) % 3, i2 = (i + 2) % 3;
      c[i1] = a ? 1.f : -1.f; c[i2] = b ? 1.f : -1.f; v.push_back(e);
    }
    return v;
  }();
  return d;
}

} // namespace hull_detail

// Gera o casco simplificado de n pontos. maxPoints < 8 e elevado a 8 (uma caixa). Entrada vazia -> casco vazio.
inline ConvexHull BuildSimplifiedHull(const Vec3* pts, size_t n, int maxPoints = kDefaultHullMaxPoints) {
  ConvexHull out;
  if (!pts || n == 0) return out;
  maxPoints = std::clamp(maxPoints, 8, 48);
  // Ignora pontos nao finitos (nao quebram o resultado).
  std::vector<Vec3> valid; valid.reserve(n);
  for (size_t i = 0; i < n; ++i) if (std::isfinite(pts[i].x) && std::isfinite(pts[i].y) && std::isfinite(pts[i].z)) valid.push_back(pts[i]);
  if (valid.empty()) return out;

  const auto& dirs = hull_detail::Directions();
  const size_t faces = std::min<size_t>(dirs.size(), static_cast<size_t>((maxPoints + 4) / 2)); // V <= 2F-4
  std::vector<float> h(faces, -1e30f);
  for (const Vec3& p : valid) for (size_t f = 0; f < faces; ++f) h[f] = std::max(h[f], Dot(dirs[f], p));

  // Caixa dos pontos de entrada (primeiras 6 direcoes) e escala para tolerancias.
  const Vec3 lo{-h[1], -h[3], -h[5]}, hi{h[0], h[2], h[4]};
  const float scale = std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z, 1e-3f});
  const float eps = 1e-4f * scale;
  for (float& x : h) x += eps * 0.5f; // folga minima: garante "dentro" apesar do arredondamento

  std::vector<Vec3> verts;
  for (size_t a = 0; a < faces; ++a) for (size_t b = a + 1; b < faces; ++b) for (size_t c = b + 1; c < faces; ++c) {
    const Vec3 &na = dirs[a], &nb = dirs[b], &nc = dirs[c];
    const Vec3 bc = Cross(nb, nc);
    const float det = Dot(na, bc);
    if (std::fabs(det) < 1e-6f) continue; // planos paralelos/dependentes
    const Vec3 p = (bc * h[a] + Cross(nc, na) * h[b] + Cross(na, nb) * h[c]) * (1.f / det);
    bool ok = true;
    for (size_t f = 0; f < faces && ok; ++f) ok = Dot(dirs[f], p) <= h[f] + eps;
    if (ok) verts.push_back(p);
  }
  // Ordena (lexicografico, quantizado) e funde vertices quase iguais: resultado independente da ordem de geracao.
  auto less = [](const Vec3& l, const Vec3& r) { return std::tie(l.x, l.y, l.z) < std::tie(r.x, r.y, r.z); };
  std::sort(verts.begin(), verts.end(), less);
  std::vector<Vec3> uniq;
  for (const Vec3& v : verts) {
    bool dup = false;
    for (const Vec3& u : uniq) if ((u - v).SqrMagnitude() <= eps * eps * 4.f) { dup = true; break; }
    if (!dup) uniq.push_back(v);
  }
  // Falha numerica ou excesso de pontos: cai para os 8 cantos da caixa (sempre valida e contem tudo).
  if (uniq.empty() || static_cast<int>(uniq.size()) > maxPoints) {
    uniq.clear();
    for (int s = 0; s < 8; ++s) uniq.push_back({(s & 1) ? hi.x : lo.x, (s & 2) ? hi.y : lo.y, (s & 4) ? hi.z : lo.z});
  }
  out.points = std::move(uniq);
  Vec3 bl = out.points[0], bh = bl;
  for (const Vec3& p : out.points) {
    bl = {std::min(bl.x, p.x), std::min(bl.y, p.y), std::min(bl.z, p.z)};
    bh = {std::max(bh.x, p.x), std::max(bh.y, p.y), std::max(bh.z, p.z)};
  }
  out.bounds = {bl, bh};
  return out;
}

inline ConvexHull BuildSimplifiedHull(const std::vector<Vec3>& pts, int maxPoints = kDefaultHullMaxPoints) {
  return BuildSimplifiedHull(pts.data(), pts.size(), maxPoints);
}

// Cache por malha: chave = (ponteiro da malha, revisao/numero de vertices, maxPoints). Se a malha mudar (revisao
// diferente) o casco e recalculado; caso contrario a mesma instancia e devolvida (nao recalcula a cada Play).
// A chave e o ponteiro + "revision" informada por quem chama (o editor usa o numero de vertices quando a malha nao
// tem contador de revisao). Seguro entre threads.
class HullCache {
 public:
  // `build` so e chamado em cache miss e deve devolver os pontos da malha.
  template <class PointsFn>
  std::shared_ptr<const ConvexHull> Get(const void* meshKey, uint64_t revision, int maxPoints, PointsFn&& build) {
    const Key k{meshKey, revision, maxPoints};
    { std::lock_guard<std::mutex> lk(mu_); auto it = map_.find(k); if (it != map_.end()) return it->second; }
    const std::vector<Vec3> pts = build();
    auto hull = std::make_shared<const ConvexHull>(BuildSimplifiedHull(pts, maxPoints));
    std::lock_guard<std::mutex> lk(mu_);
    // Remove entradas antigas do mesmo ponteiro (revisoes velhas) para nao acumular memoria.
    for (auto it = map_.begin(); it != map_.end();) { if (std::get<0>(it->first) == meshKey) it = map_.erase(it); else ++it; }
    map_[k] = hull;
    return hull;
  }
  size_t Size() const { std::lock_guard<std::mutex> lk(mu_); return map_.size(); }
  void Clear() { std::lock_guard<std::mutex> lk(mu_); map_.clear(); }

 private:
  using Key = std::tuple<const void*, uint64_t, int>;
  mutable std::mutex mu_;
  std::map<Key, std::shared_ptr<const ConvexHull>> map_;
};

// Instancia global do cache (usada pelo editor).
inline HullCache& GlobalHullCache() { static HullCache c; return c; }

} // namespace Newtoon
