// Testes da capa de colisao simplificada (casco com poucos pontos) do Newtoon.
#include "test.hpp"
#include "tool/physics/newtoon_hull.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace Newtoon;

namespace {

// Gerador deterministico (LCG): nao depende de <random>.
struct Lcg {
  unsigned s;
  float next() { s = s * 1664525u + 1013904223u; return static_cast<float>(s >> 8) / 16777216.0f; }
};

// Nuvem tipo esfera/elipsoide com muitos vertices (simula uma malha densa).
std::vector<Vec3> blob(int n, unsigned seed) {
  Lcg r{seed}; std::vector<Vec3> v;
  for (int i = 0; i < n; ++i) {
    Vec3 d{r.next() * 2.f - 1.f, r.next() * 2.f - 1.f, r.next() * 2.f - 1.f};
    const float m = d.Magnitude(); if (m < 1e-3f) continue;
    v.push_back({d.x / m * 2.f + 1.f, d.y / m * 1.f, d.z / m * 3.f - 0.5f});
  }
  return v;
}

// Um ponto esta dentro do casco se estiver na caixa dele e a maior folga dos pontos nao o exclui.
// Verificacao independente: o casco devolvido e k-DOP; basta testar as 6 faces da caixa e os cantos (8 direcoes) via
// maximos dos proprios pontos do casco.
bool insideHull(const ConvexHull& h, const Vec3& p, float tol) {
  static const Vec3 dirs[] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1},{1,1,1},{-1,-1,-1},{1,1,-1},{-1,-1,1},{1,-1,1},{-1,1,-1},{-1,1,1},{1,-1,-1}};
  for (const Vec3& d : dirs) {
    float mx = -1e30f; for (const Vec3& q : h.points) mx = std::max(mx, Dot(d, q));
    if (Dot(d, p) > mx + tol) return false;
  }
  return true;
}

} // namespace

TEST(hull_has_at_most_n_points) {
  const auto pts = blob(5000, 1);
  for (int n : {8, 16, 24}) {
    const ConvexHull h = BuildSimplifiedHull(pts, n);
    CHECK(!h.Empty());
    CHECK(static_cast<int>(h.points.size()) <= n);
  }
}

TEST(hull_contains_all_source_points) {
  const auto pts = blob(3000, 7);
  const ConvexHull h = BuildSimplifiedHull(pts, 24);
  int outside = 0;
  for (const Vec3& p : pts) if (!insideHull(h, p, 1e-3f)) ++outside;
  CHECK(outside == 0);
  // A caixa englobante do casco cobre a da malha.
  for (const Vec3& p : pts) CHECK(p.x >= h.bounds.min.x - 1e-3f && p.x <= h.bounds.max.x + 1e-3f);
}

TEST(hull_is_deterministic_and_order_independent) {
  auto pts = blob(2000, 3);
  const ConvexHull a = BuildSimplifiedHull(pts, 24), b = BuildSimplifiedHull(pts, 24);
  CHECK(a.points.size() == b.points.size());
  bool same = a.points.size() == b.points.size();
  for (size_t i = 0; same && i < a.points.size(); ++i) same = a.points[i].x == b.points[i].x && a.points[i].y == b.points[i].y && a.points[i].z == b.points[i].z;
  CHECK(same);
  std::reverse(pts.begin(), pts.end());
  const ConvexHull c = BuildSimplifiedHull(pts, 24);
  bool same2 = c.points.size() == a.points.size();
  for (size_t i = 0; same2 && i < a.points.size(); ++i) same2 = c.points[i].x == a.points[i].x && c.points[i].y == a.points[i].y && c.points[i].z == a.points[i].z;
  CHECK(same2);
}

TEST(hull_handles_four_coplanar_points) {
  const std::vector<Vec3> quad = {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}};
  const ConvexHull h = BuildSimplifiedHull(quad, 24);
  CHECK(!h.Empty());
  CHECK(h.points.size() <= 24);
  for (const Vec3& p : quad) CHECK(insideHull(h, p, 1e-3f));
  // Casos degenerados extremos: um ponto, vazio e pontos repetidos nao quebram.
  CHECK(BuildSimplifiedHull(std::vector<Vec3>{}, 24).Empty());
  CHECK(!BuildSimplifiedHull(std::vector<Vec3>{{2, 3, 4}}, 24).Empty());
  CHECK(!BuildSimplifiedHull(std::vector<Vec3>{{1, 1, 1}, {1, 1, 1}, {1, 1, 1}}, 16).Empty());
}

TEST(hull_cache_reuses_by_mesh_key_and_revision) {
  HullCache cache; int builds = 0; int key = 0;
  auto build = [&] { ++builds; return std::vector<Vec3>{{0, 0, 0}, {1, 2, 3}, {-1, 0, 2}, {0, 1, 0}}; };
  auto h1 = cache.Get(&key, 4, 16, build); auto h2 = cache.Get(&key, 4, 16, build);
  CHECK(builds == 1 && h1 == h2);
  auto h3 = cache.Get(&key, 5, 16, build);   // revisao nova -> recalcula
  CHECK(builds == 2 && h3 != h1 && cache.Size() == 1);
}
