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

// Limites da topologia do casco usada na narrowphase (buffers de tamanho fixo na pilha: nenhuma alocacao por quadro).
// 48 vertices = maximo de BuildSimplifiedHull; 64 faces (k-DOP tem no maximo 26 direcoes); 144 arestas >= 3V-6.
constexpr int kMaxHullVerts = 48;
constexpr int kMaxHullFaces = 64;
constexpr int kMaxHullEdges = 144;

// Face do casco: plano {x : n.x == d} com todos os vertices em n.x <= d (n aponta para fora). Os vertices da face
// ficam em ConvexHull::faceVerts[first .. first+count), em ordem anti-horaria vista de fora.
struct HullFace { Vec3 n; float d = 0.f; uint16_t first = 0; uint8_t count = 0; };
// Aresta: indices dos vertices a->b (sentido da face f0) e as duas faces adjacentes (usadas no teste de Gauss do SAT).
struct HullEdge { uint8_t a = 0, b = 0, f0 = 0, f1 = 0; };

// Resultado: pontos do casco (no espaco local da malha), caixa englobante deles e a topologia (faces/arestas)
// que a narrowphase usa no SAT. A topologia e calculada UMA vez na construcao (fica no cache, nunca por quadro).
struct ConvexHull {
  std::vector<Vec3> points;
  AABB bounds{{0.f, 0.f, 0.f}, {0.f, 0.f, 0.f}};
  std::vector<HullFace> faces;
  std::vector<HullEdge> edges;
  std::vector<uint8_t> faceVerts;
  bool Empty() const { return points.empty(); }
  // Solido fechado utilizavel pela narrowphase (>= 4 faces, >= 6 arestas). Senao o corpo cai para a caixa.
  bool Solid() const { return faces.size() >= 4 && edges.size() >= 6; }
};

// ------------------------------------------------------------------ LOD de colisao por distancia
// Configuracao do LOD: ate `nearDistance` usa `nearPoints` pontos, ate `midDistance` usa `midPoints`, acima usa
// `farPoints` (8 pontos = caixa de 6 faces). Padrao documentado em docs/planos/20_COLISAO_CASCO_LOD.md.
struct ColliderLodConfig {
  float nearDistance = 20.f;  // metros: ate aqui, casco completo
  float midDistance = 60.f;   // metros: ate aqui, casco medio
  int nearPoints = 24;
  int midPoints = 12;
  int farPoints = 8;
};

// Escolhe o maximo de pontos do casco a partir da distancia do corpo ao foco. Funcao PURA: mesma entrada, mesma saida.
// A saida nunca cresce quando a distancia cresce (mesmo com configuracao fora de ordem: usa minimos encadeados).
// Distancia NaN ou negativa conta como "perto" (o maximo).
inline int chooseColliderLod(float distance, const ColliderLodConfig& cfg = ColliderLodConfig{}) {
  const int nearP = std::clamp(cfg.nearPoints, 8, 48);
  const int midP = std::min(std::clamp(cfg.midPoints, 8, 48), nearP);
  const int farP = std::min(std::clamp(cfg.farPoints, 8, 48), midP);
  if (!(distance > cfg.nearDistance)) return nearP;      // inclui NaN e negativos
  if (!(distance > cfg.midDistance)) return midP;
  return farP;
}

// Conjunto de niveis de LOD de uma malha: um casco por nivel (ordem decrescente de maxPoints). Os cascos pertencem ao
// HullCache (shared_ptr); o Collider guarda so um ponteiro cru para este conjunto.
struct HullLodSet {
  static constexpr int kMaxLevels = 4;
  int count = 0;
  int maxPoints[kMaxLevels] = {0, 0, 0, 0};
  std::shared_ptr<const ConvexHull> hull[kMaxLevels];
  // Nivel mais detalhado (o primeiro).
  const ConvexHull* Max() const { return count > 0 ? hull[0].get() : nullptr; }
  // Maior nivel cujo maxPoints cabe em `wanted`; se nenhum couber, o menor nivel.
  const ConvexHull* Pick(int wanted) const {
    if (count == 0) return nullptr;
    for (int i = 0; i < count; ++i) if (maxPoints[i] <= wanted) return hull[i].get();
    return hull[count - 1].get();
  }
};

namespace hull_detail {

// Calcula faces (planos de apoio) e arestas a partir dos pontos do casco. O(n^4) com n <= 48, so na construcao.
// Cada plano de apoio e achado por 3 pontos nao colineares: todos os pontos devem ficar de um lado. Planos duplicados
// (mesma normal e mesmo offset) sao fundidos. Vertices de cada face sao ordenados por angulo (anti-horario por fora).
inline void BuildTopology(ConvexHull& h) {
  h.faces.clear(); h.edges.clear(); h.faceVerts.clear();
  const size_t n = h.points.size();
  if (n < 4 || n > static_cast<size_t>(kMaxHullVerts)) return;
  const Vec3 ext = h.bounds.max - h.bounds.min;
  const float scale = std::max({ext.x, ext.y, ext.z, 1e-3f});
  const float tol = 4e-4f * scale, minCross = 1e-3f * scale * scale;
  const auto& P = h.points;

  for (size_t i = 0; i < n; ++i) for (size_t j = i + 1; j < n; ++j) for (size_t k = j + 1; k < n; ++k) {
    Vec3 nrm = Cross(P[j] - P[i], P[k] - P[i]);
    const float len = nrm.Magnitude();
    if (len < minCross) continue;                    // triangulo degenerado (colinear)
    nrm = nrm * (1.f / len);
    float mx = -1e30f, mn = 1e30f;
    for (const Vec3& p : P) { const float s = Dot(nrm, p); mx = std::max(mx, s); mn = std::min(mn, s); }
    const float d0 = Dot(nrm, P[i]);
    if (mx - d0 <= tol) { /* todos abaixo do plano: normal ja aponta para fora */ }
    else if (d0 - mn <= tol) { nrm = -nrm; mx = -mn; }   // todos acima: inverte
    else continue;                                   // plano corta o casco: nao e face
    bool dup = false;
    for (const HullFace& f : h.faces) if (Dot(f.n, nrm) > 0.99995f && std::fabs(f.d - mx) <= 2.f * tol) { dup = true; break; }
    if (dup || h.faces.size() >= static_cast<size_t>(kMaxHullFaces)) continue;
    HullFace f; f.n = nrm; f.d = mx; h.faces.push_back(f);
  }

  // Vertices de cada face, ordenados por angulo em torno do centroide.
  std::vector<HullFace> kept;
  std::vector<std::pair<float, int>> order;
  for (HullFace f : h.faces) {
    order.clear();
    Vec3 c{0, 0, 0}; int cnt = 0;
    for (size_t v = 0; v < n; ++v) if (f.d - Dot(f.n, P[v]) <= tol) { c += P[v]; ++cnt; }
    if (cnt < 3) continue;
    c = c * (1.f / static_cast<float>(cnt));
    const Vec3 axis = std::fabs(f.n.x) < 0.6f ? Vec3{1, 0, 0} : (std::fabs(f.n.y) < 0.6f ? Vec3{0, 1, 0} : Vec3{0, 0, 1});
    const Vec3 u = Normalized(Cross(f.n, axis)), w = Cross(f.n, u);   // (u, w, n) destro: angulo cresce anti-horario
    for (size_t v = 0; v < n; ++v) if (f.d - Dot(f.n, P[v]) <= tol) {
      const Vec3 r = P[v] - c;
      order.push_back({std::atan2(Dot(r, w), Dot(r, u)), static_cast<int>(v)});
    }
    std::sort(order.begin(), order.end());          // empate de angulo: menor indice (deterministico)
    f.first = static_cast<uint16_t>(h.faceVerts.size()); f.count = static_cast<uint8_t>(order.size());
    for (const auto& o : order) h.faceVerts.push_back(static_cast<uint8_t>(o.second));
    kept.push_back(f);
  }
  h.faces.swap(kept);

  // Arestas: cada aresta aparece em duas faces, em sentidos opostos.
  for (size_t fi = 0; fi < h.faces.size(); ++fi) {
    const HullFace& f = h.faces[fi];
    for (int i = 0; i < f.count; ++i) {
      const uint8_t a = h.faceVerts[f.first + i], b = h.faceVerts[f.first + (i + 1) % f.count];
      bool found = false;
      for (HullEdge& e : h.edges) if (e.a == b && e.b == a && e.f1 == 255) { e.f1 = static_cast<uint8_t>(fi); found = true; break; }
      if (!found && h.edges.size() < static_cast<size_t>(kMaxHullEdges)) { HullEdge e; e.a = a; e.b = b; e.f0 = static_cast<uint8_t>(fi); e.f1 = 255; h.edges.push_back(e); }
    }
  }
  // Remove arestas abertas (so uma face): sem as duas faces o teste de Gauss nao se aplica.
  h.edges.erase(std::remove_if(h.edges.begin(), h.edges.end(), [](const HullEdge& e) { return e.f1 == 255; }), h.edges.end());
}

}  // namespace hull_detail

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
  hull_detail::BuildTopology(out);   // faces/arestas para a narrowphase (uma vez, aqui; vai para o cache)
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
    // Revisoes velhas do mesmo ponteiro saem do mapa (nao acumulam), mas NAO sao destruidas: vao para `retired_`,
    // porque um Collider em jogo pode ainda apontar para elas (ponteiro cru). So Clear() libera.
    // Entradas da MESMA revisao (outros maxPoints, ou seja outros niveis de LOD) permanecem.
    for (auto it = map_.begin(); it != map_.end();) {
      if (std::get<0>(it->first) == meshKey && std::get<1>(it->first) != revision) { retired_.push_back(it->second); it = map_.erase(it); }
      else ++it;
    }
    map_[k] = hull;
    return hull;
  }

  // Devolve os niveis de LOD (cfg.nearPoints / midPoints / farPoints, sem repetir) de uma malha. Cada nivel vem do
  // cache por (malha, revisao, maxPoints); `build` so roda uma vez, no primeiro miss. O conjunto tambem e cacheado,
  // entao o ponteiro devolvido e estavel enquanto a malha nao mudar.
  template <class PointsFn>
  std::shared_ptr<const HullLodSet> GetLods(const void* meshKey, uint64_t revision, const ColliderLodConfig& cfg, PointsFn&& build) {
    const LodKey lk{meshKey, revision, cfg.nearPoints, cfg.midPoints, cfg.farPoints};
    { std::lock_guard<std::mutex> g(mu_); auto it = lodMap_.find(lk); if (it != lodMap_.end()) return it->second; }
    std::vector<Vec3> pts; bool built = false;
    auto lazy = [&] { if (!built) { pts = build(); built = true; } return pts; };
    auto set = std::make_shared<HullLodSet>();
    int levels[3] = {chooseColliderLod(0.f, cfg), chooseColliderLod(cfg.nearDistance + 0.5f * (cfg.midDistance - cfg.nearDistance), cfg),
                     chooseColliderLod(cfg.midDistance + 1.f, cfg)};
    for (int lv : levels) {
      bool dup = false; for (int i = 0; i < set->count; ++i) dup = dup || set->maxPoints[i] == lv;
      if (dup || set->count >= HullLodSet::kMaxLevels) continue;
      set->maxPoints[set->count] = lv; set->hull[set->count] = Get(meshKey, revision, lv, lazy); ++set->count;
    }
    std::shared_ptr<const HullLodSet> out = set;
    std::lock_guard<std::mutex> g(mu_);
    for (auto it = lodMap_.begin(); it != lodMap_.end();) {
      if (std::get<0>(it->first) == meshKey && std::get<1>(it->first) != revision) { retiredLods_.push_back(it->second); it = lodMap_.erase(it); }
      else ++it;
    }
    lodMap_[lk] = out;
    return out;
  }

  size_t Size() const { std::lock_guard<std::mutex> lk(mu_); return map_.size(); }
  // Libera tudo (inclusive o que foi aposentado). Chame so quando nenhum mundo de fisica usa mais os cascos.
  void Clear() { std::lock_guard<std::mutex> lk(mu_); map_.clear(); lodMap_.clear(); retired_.clear(); retiredLods_.clear(); }

 private:
  using Key = std::tuple<const void*, uint64_t, int>;
  using LodKey = std::tuple<const void*, uint64_t, int, int, int>;
  mutable std::mutex mu_;
  std::map<Key, std::shared_ptr<const ConvexHull>> map_;
  std::map<LodKey, std::shared_ptr<const HullLodSet>> lodMap_;
  std::vector<std::shared_ptr<const ConvexHull>> retired_;
  std::vector<std::shared_ptr<const HullLodSet>> retiredLods_;
};

// Instancia global do cache (usada pelo editor).
inline HullCache& GlobalHullCache() { static HullCache c; return c; }

} // namespace Newtoon
