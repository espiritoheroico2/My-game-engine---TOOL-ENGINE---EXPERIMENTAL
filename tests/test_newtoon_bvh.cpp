// Testes de F-05: contrato do BVH (tamanhos, caixas invalidas, arvore grande e pilha de percurso).
#include "test.hpp"
#include "tool/physics/newtoon_bvh.hpp"

#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <vector>

using namespace Newtoon;

namespace {

AABB box(float x, float y, float z, float s) {
  return AABB{Vec3{x - s, y - s, z - s}, Vec3{x + s, y + s, z + s}};
}

// Gerador deterministico (LCG) para nao depender de <random> nem de sementes do sistema.
struct Lcg {
  unsigned s;
  float next() { s = s * 1664525u + 1013904223u; return static_cast<float>(s >> 8) / 16777216.0f; }
};

} // namespace

TEST(bvh_rejects_mismatched_sizes) {
  std::vector<AABB> boxes(3, box(0, 0, 0, 1));
  bool threwShort = false, threwLong = false;
  try { BVH b; b.Build(boxes, std::vector<char>(2, 1)); } catch (const std::invalid_argument&) { threwShort = true; }
  try { BVH b; b.Build(boxes, std::vector<char>(4, 1)); } catch (const std::invalid_argument&) { threwLong = true; }
  CHECK(threwShort && threwLong);
  // Com tamanhos iguais, nada e lancado.
  BVH ok; bool noThrow = true;
  try { ok.Build(boxes, std::vector<char>(3, 1)); } catch (...) { noThrow = false; }
  CHECK(noThrow);
}

TEST(bvh_discards_nan_inf_and_inverted_boxes) {
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();
  std::vector<AABB> boxes = {
      box(0, 0, 0, 1),                                  // 0: valida
      AABB{Vec3{nan, 0, 0}, Vec3{1, 1, 1}},             // 1: NaN
      AABB{Vec3{0, 0, 0}, Vec3{inf, 1, 1}},             // 2: Inf
      AABB{Vec3{5, 5, 5}, Vec3{-5, -5, -5}},            // 3: min > max
      box(10, 0, 0, 1),                                 // 4: valida
  };
  BVH bvh;
  bvh.Build(boxes, std::vector<char>(boxes.size(), 1));
  CHECK(bvh.RejectedCount() == 3);
  std::set<Entity> found;
  bvh.Query(AABB{Vec3{-100, -100, -100}, Vec3{100, 100, 100}}, [&](Entity e) { found.insert(e); });
  CHECK(found == (std::set<Entity>{0, 4}));  // so as validas aparecem; a arvore nao foi corrompida
}

TEST(bvh_large_tree_matches_brute_force) {
  // Arvore grande: a pilha de percurso precisa cobrir toda a profundidade sem perder folhas.
  const size_t n = 50000;
  std::vector<AABB> boxes(n);
  std::vector<char> valid(n, 1);
  Lcg rng{12345u};
  for (size_t i = 0; i < n; ++i) boxes[i] = box(rng.next() * 500, rng.next() * 500, rng.next() * 500, 0.5f + rng.next());
  valid[7] = 0; // uma entidade ignorada
  BVH bvh;
  bvh.Build(boxes, valid);
  CHECK(!bvh.Empty() && bvh.RejectedCount() == 0);

  for (int q = 0; q < 40; ++q) {
    const AABB query = box(rng.next() * 500, rng.next() * 500, rng.next() * 500, 10.f + 20.f * rng.next());
    std::set<Entity> viaTree, brute;
    bvh.Query(query, [&](Entity e) { viaTree.insert(e); });
    for (size_t i = 0; i < n; ++i) if (valid[i] && Overlaps(boxes[i], query)) brute.insert(static_cast<Entity>(i));
    CHECK(viaTree == brute);
  }
}

TEST(bvh_identical_boxes_build_and_query_without_losing_entities) {
  // Caixas identicas produzem extensao zero em todos os eixos: caso degenerado para a escolha do eixo.
  const size_t n = 4096;
  std::vector<AABB> boxes(n, box(3, 3, 3, 1));
  BVH bvh;
  bvh.Build(boxes, std::vector<char>(n, 1));
  size_t hits = 0;
  bvh.Query(box(3, 3, 3, 0.5f), [&](Entity) { ++hits; });
  CHECK(hits == n);
}

TEST(bvh_raycast_reports_the_nearest_hit) {
  std::vector<AABB> boxes = {box(0, 0, 10, 1), box(0, 0, 20, 1), box(0, 0, 30, 1)};
  BVH bvh;
  bvh.Build(boxes, std::vector<char>(3, 1));
  float maxT = 1000.f;
  Entity best = kNoEntity;
  bvh.RayQuery(Vec3{0, 0, 0}, Vec3{0, 0, 1}, maxT, [&](Entity e, float& mt) {
    // Visita o acerto mais proximo: reduz o alcance do raio (o caso mais comum do raycast da fisica).
    const float entry = 10.f * static_cast<float>(e + 1) - 1.f; // entrada aproximada da caixa e
    if (entry < mt) { mt = entry; best = e; }
  });
  CHECK(best == 0);
}

TEST(bvh_empty_input_is_empty) {
  BVH bvh;
  bvh.Build({}, {});
  CHECK(bvh.Empty());
  int calls = 0;
  bvh.Query(box(0, 0, 0, 1), [&](Entity) { ++calls; });
  CHECK(calls == 0);
}
