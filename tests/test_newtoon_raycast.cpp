// Testes do raycast unico do Newtoon (3D com BVH e 2D com BVH): formas, equivalencia com a forca bruta, RaycastAll,
// filtros (camada, gatilho, entidade, face de tras) e medicao de tempo (impressa, sem limite).
#include "test.hpp"
#include "tool/physics/newtoon.hpp"
#include "tool/physics/newtoon_2d.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace Newtoon;

namespace {

// Gerador pseudo-aleatorio deterministico (LCG): os testes repetem exatamente os mesmos cenarios.
struct Rng {
  uint32_t s = 12345u;
  float next() { s = s * 1664525u + 1013904223u; return static_cast<float>((s >> 8) & 0xFFFFFF) / 16777216.f; }   // [0,1)
  float range(float a, float b) { return a + (b - a) * next(); }
};

// Casco de um cubo de meia aresta 1 (k-DOP de 24 pontos reproduz o cubo exato).
const ConvexHull& cubeHull() {
  static const ConvexHull h = [] {
    std::vector<Vec3> p;
    for (int s = 0; s < 8; ++s) p.push_back({(s & 1) ? 1.f : -1.f, (s & 2) ? 1.f : -1.f, (s & 4) ? 1.f : -1.f});
    return BuildSimplifiedHull(p, 24);
  }();
  return h;
}

// Cena 3D: arrays paralelos de transformacao/corpo/collider. Todos os corpos sao estaticos (massa 0).
struct Scene {
  std::vector<Transform> tf; std::vector<Rigidbody> rb; std::vector<Collider> col;
  size_t add(const Collider& c, const Vec3& pos, const Quat& q = Quat{0.f, 0.f, 0.f, 1.f}) {
    Transform t; t.position = pos; t.rotation = q; tf.push_back(t);
    Rigidbody r; r.SetMass(0.f); rb.push_back(r);
    col.push_back(c);
    return tf.size() - 1;
  }
  size_t box(const Vec3& pos, const Vec3& e) { Collider c; c.type = ShapeType::Box; c.extents = e; return add(c, pos); }
  size_t sphere(const Vec3& pos, float r) { Collider c; c.type = ShapeType::Sphere; c.radius = r; return add(c, pos); }
  size_t cyl(const Vec3& pos, float r, float hh) { Collider c; c.type = ShapeType::Cylinder; c.radius = r; c.halfHeight = hh; return add(c, pos); }
  size_t hull(const Vec3& pos) {
    Collider c; c.type = ShapeType::CustomConvex; c.hull = &cubeHull(); c.extents = {1.f, 1.f, 1.f}; return add(c, pos);
  }
};

bool near(float a, float b, float eps = 0.02f) { return std::fabs(a - b) <= eps; }

}  // namespace

// (a) distancia e normal corretas em cada forma.
TEST(raycast_shapes_distance_and_normal) {
  Scene s;
  const size_t bx = s.box({0.f, 0.f, 5.f}, {1.f, 1.f, 1.f});
  const size_t sp = s.sphere({20.f, 0.f, 5.f}, 1.f);
  const size_t cy = s.cyl({40.f, 0.f, 5.f}, 1.f, 1.f);
  const size_t hu = s.hull({60.f, 0.f, 5.f});
  PhysicsWorld w; w.Rebuild(s.tf, s.rb, s.col);

  RaycastHit h = w.Raycast({0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 100.f);
  CHECK(h.hit && h.entity == bx && near(h.distance, 4.f) && h.normal.z < -0.99f);
  CHECK(near(h.point.z, 4.f));

  h = w.Raycast({20.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 100.f);
  CHECK(h.hit && h.entity == sp && near(h.distance, 4.f) && h.normal.z < -0.99f);

  h = w.Raycast({40.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 100.f);                  // lateral do cilindro
  CHECK(h.hit && h.entity == cy && near(h.distance, 4.f) && h.normal.z < -0.99f);
  h = w.Raycast({40.f, 10.f, 5.f}, {0.f, -1.f, 0.f}, 100.f);                // tampa de cima: y = 1
  CHECK(h.hit && h.entity == cy && near(h.distance, 9.f) && h.normal.y > 0.99f);
  h = w.Raycast({40.5f, -10.f, 5.f}, {0.f, 1.f, 0.f}, 100.f);               // tampa de baixo, fora do centro
  CHECK(h.hit && h.entity == cy && near(h.distance, 9.f) && h.normal.y < -0.99f);
  h = w.Raycast({41.5f, 10.f, 5.f}, {0.f, -1.f, 0.f}, 100.f);               // passa ao lado do cilindro (raio 1)
  CHECK(!h.hit);

  h = w.Raycast({60.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 100.f);
  CHECK(h.hit && h.entity == hu && near(h.distance, 4.f) && h.normal.z < -0.99f);

  h = w.Raycast({0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 3.f);                     // alcance menor que a distancia
  CHECK(!h.hit);
}

// (a2) cilindro girado: eixo apontando para +z (gira 90 graus em torno de X).
TEST(raycast_rotated_cylinder) {
  Scene s;
  const size_t cy = s.cyl({0.f, 0.f, 5.f}, 1.f, 2.f);
  s.tf[cy].rotation = Quat::FromAxisAngle({1.f, 0.f, 0.f}, 1.57079633f);
  PhysicsWorld w; w.Rebuild(s.tf, s.rb, s.col);
  RaycastHit h = w.Raycast({0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 100.f);        // pela tampa: z = 5 - 2 = 3
  CHECK(h.hit && near(h.distance, 3.f) && h.normal.z < -0.99f);
  h = w.Raycast({0.f, 10.f, 5.f}, {0.f, -1.f, 0.f}, 100.f);                 // pela lateral: y = 1
  CHECK(h.hit && near(h.distance, 9.f) && h.normal.y > 0.99f);
}

// Monta 500 formas aleatorias (caixa, esfera, cilindro, casco, com rotacao qualquer) numa regiao de 40 m.
static void buildRandomScene(Scene& s, int count) {
  Rng r;
  for (int i = 0; i < count; ++i) {
    const Vec3 pos{r.range(-20.f, 20.f), r.range(-20.f, 20.f), r.range(-20.f, 20.f)};
    Vec3 ax{r.range(-1.f, 1.f), r.range(-1.f, 1.f), r.range(-1.f, 1.f)};
    if (ax.SqrMagnitude() < 1e-3f) ax = {0.f, 1.f, 0.f};
    const Quat q = Quat::FromAxisAngle(Normalized(ax), r.range(0.f, 6.2831853f));
    const int kind = i % 4;
    Collider c;
    if (kind == 0) { c.type = ShapeType::Box; c.extents = {r.range(0.3f, 1.5f), r.range(0.3f, 1.5f), r.range(0.3f, 1.5f)}; }
    else if (kind == 1) { c.type = ShapeType::Sphere; c.radius = r.range(0.3f, 1.5f); }
    else if (kind == 2) { c.type = ShapeType::Cylinder; c.radius = r.range(0.3f, 1.2f); c.halfHeight = r.range(0.3f, 1.5f); }
    else { c.type = ShapeType::CustomConvex; c.hull = &cubeHull(); c.extents = {1.f, 1.f, 1.f}; c.hullScale = {r.range(0.3f, 1.2f), r.range(0.3f, 1.2f), r.range(0.3f, 1.2f)}; }
    c.layer = static_cast<uint32_t>(i % 5);
    c.isTrigger = (i % 7 == 0);
    s.add(c, pos, q);
  }
}

// (b) BVH = forca bruta em 500 formas aleatorias, com e sem filtro.
TEST(raycast_bvh_equals_brute_force_500_shapes) {
  Scene s; buildRandomScene(s, 500);
  PhysicsWorld w; w.Rebuild(s.tf, s.rb, s.col);
  Rng r; r.s = 777u;
  int hits = 0, mismatches = 0;
  for (int i = 0; i < 1000; ++i) {
    const Vec3 o{r.range(-25.f, 25.f), r.range(-25.f, 25.f), r.range(-25.f, 25.f)};
    Vec3 d{r.range(-1.f, 1.f), r.range(-1.f, 1.f), r.range(-1.f, 1.f)};
    if (d.SqrMagnitude() < 1e-3f) d = {1.f, 0.f, 0.f};
    RaycastFilter f;
    if (i % 3 == 1) { f.layerMask = 0b10110u; f.triggers = QueryTriggerInteraction::Ignore; }
    if (i % 3 == 2) f.ignoreBackface = true;
    const RaycastHit a = w.Raycast(o, d, 60.f, f), b = w.RaycastBruteForce(o, d, 60.f, f);
    if (a.hit != b.hit || (a.hit && (a.entity != b.entity || std::fabs(a.distance - b.distance) > 1e-4f))) ++mismatches;
    if (a.hit) ++hits;
  }
  CHECK(mismatches == 0);
  CHECK(hits > 100);   // o cenario e denso o bastante para o teste ser significativo
}

// (c) RaycastAll: contagem certa e ordenado por distancia (indices embaralhados de proposito).
TEST(raycast_all_sorted_by_distance) {
  Scene s;
  const float zs[5] = {12.f, 3.f, 15.f, 9.f, 6.f};
  for (float z : zs) s.sphere({0.f, 0.f, z}, 1.f);
  s.sphere({10.f, 0.f, 5.f}, 1.f);                                          // fora do raio
  PhysicsWorld w; w.Rebuild(s.tf, s.rb, s.col);
  std::vector<RaycastHit> hits;
  const size_t n = w.RaycastAll({0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 100.f, RaycastFilter{}, hits);
  CHECK(n == 5 && hits.size() == 5);
  for (size_t i = 1; i < hits.size(); ++i) CHECK(hits[i - 1].distance <= hits[i].distance);
  if (hits.size() == 5) { CHECK(near(hits[0].distance, 2.f)); CHECK(near(hits[4].distance, 14.f)); CHECK(hits[0].entity == 1); }
  const size_t m = w.RaycastAll({0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 10.5f, RaycastFilter{}, hits);   // alcance limita a contagem
  CHECK(m == 3);
  // O primeiro de RaycastAll e o mesmo acerto do Raycast.
  const RaycastHit one = w.Raycast({0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 100.f);
  w.RaycastAll({0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 100.f, RaycastFilter{}, hits);
  CHECK(!hits.empty() && hits[0].entity == one.entity && near(hits[0].distance, one.distance, 1e-4f));
}

// (d) filtros: camada, gatilho, entidade ignorada.
TEST(raycast_filters_layer_trigger_ignore) {
  Scene s;
  const size_t nearB = s.box({0.f, 0.f, 5.f}, {1.f, 1.f, 1.f});
  const size_t farB = s.box({0.f, 0.f, 10.f}, {1.f, 1.f, 1.f});
  s.col[nearB].layer = 3;
  PhysicsWorld w; w.Rebuild(s.tf, s.rb, s.col);
  const Vec3 o{0.f, 0.f, 0.f}, d{0.f, 0.f, 1.f};

  RaycastFilter f;
  CHECK(w.Raycast(o, d, 50.f, f).entity == nearB);
  f.layerMask = ~(1u << 3);                                                 // exclui a camada 3
  CHECK(w.Raycast(o, d, 50.f, f).entity == farB);
  f.layerMask = 1u << 3;                                                    // so a camada 3
  CHECK(w.Raycast(o, d, 50.f, f).entity == nearB);
  f.layerMask = 0u;                                                         // nenhuma camada
  CHECK(!w.Raycast(o, d, 50.f, f).hit);
  f = RaycastFilter{}; f.ignore = static_cast<Entity>(nearB);
  CHECK(w.Raycast(o, d, 50.f, f).entity == farB);
  CHECK(w.Raycast(o, d, 50.f, static_cast<Entity>(nearB)).entity == farB);                     // atalho por entidade

  // Gatilho: perto e gatilho.
  s.col[nearB].layer = 0; s.col[nearB].isTrigger = true;
  w.Rebuild(s.tf, s.rb, s.col);
  f = RaycastFilter{};
  CHECK(w.Raycast(o, d, 50.f, f).entity == nearB);                          // UseGlobal + padrao true: acerta
  f.triggers = QueryTriggerInteraction::Ignore;
  CHECK(w.Raycast(o, d, 50.f, f).entity == farB);                           // pedido para ignorar
  f.triggers = QueryTriggerInteraction::Collide;
  CHECK(w.Raycast(o, d, 50.f, f).entity == nearB);
  f.triggers = QueryTriggerInteraction::UseGlobal; w.queryTriggersByDefault = false;
  CHECK(w.Raycast(o, d, 50.f, f).entity == farB);                           // global desliga
  f.triggers = QueryTriggerInteraction::Collide;
  CHECK(w.Raycast(o, d, 50.f, f).entity == nearB);                          // explicito vence o global
}

// (d2) face de tras: raio que nasce DENTRO da forma.
TEST(raycast_ignore_backface) {
  Scene s;
  s.sphere({0.f, 0.f, 0.f}, 2.f); s.box({10.f, 0.f, 0.f}, {2.f, 2.f, 2.f}); s.cyl({20.f, 0.f, 0.f}, 2.f, 2.f); s.hull({30.f, 0.f, 0.f});
  PhysicsWorld w; w.Rebuild(s.tf, s.rb, s.col);
  for (int i = 0; i < 4; ++i) {
    const Vec3 o{10.f * static_cast<float>(i), 0.f, 0.f};                  // centro de cada forma
    RaycastFilter f;
    CHECK(w.Raycast(o, {0.f, 0.f, 1.f}, 50.f, f).hit);                      // padrao: acerta (de dentro)
    f.ignoreBackface = true;
    CHECK(!w.Raycast(o, {0.f, 0.f, 1.f}, 50.f, f).hit);                     // ignorado
  }
}

// Medicao (sem limite): 1000 raios contra 1000 formas, BVH x forca bruta. Imprime os tempos.
TEST(raycast_timing_1000_rays_1000_shapes) {
  Scene s; buildRandomScene(s, 1000);
  PhysicsWorld w; w.Rebuild(s.tf, s.rb, s.col);
  Rng r; r.s = 99u;
  std::vector<Vec3> os, ds;
  for (int i = 0; i < 1000; ++i) {
    os.push_back({r.range(-25.f, 25.f), r.range(-25.f, 25.f), r.range(-25.f, 25.f)});
    Vec3 d{r.range(-1.f, 1.f), r.range(-1.f, 1.f), r.range(-1.f, 1.f)}; if (d.SqrMagnitude() < 1e-3f) d = {1.f, 0.f, 0.f};
    ds.push_back(d);
  }
  const RaycastFilter f;
  int h1 = 0, h2 = 0;
  const auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < 1000; ++i) h1 += w.RaycastBruteForce(os[static_cast<size_t>(i)], ds[static_cast<size_t>(i)], 60.f, f).hit ? 1 : 0;
  const auto t1 = std::chrono::steady_clock::now();
  for (int i = 0; i < 1000; ++i) h2 += w.Raycast(os[static_cast<size_t>(i)], ds[static_cast<size_t>(i)], 60.f, f).hit ? 1 : 0;
  const auto t2 = std::chrono::steady_clock::now();
  const double msBrute = std::chrono::duration<double, std::milli>(t1 - t0).count();
  const double msBvh = std::chrono::duration<double, std::milli>(t2 - t1).count();
  std::printf("    [medicao] 1000 raios x 1000 formas: forca bruta %.3f ms, BVH %.3f ms (acertos %d/%d)\n", msBrute, msBvh, h1, h2);
  CHECK(h1 == h2);
}

// ---------------------------------------------------------------- 2D
// (e) Raycast 2D com BVH = varredura O(N) em 500 formas aleatorias (circulos e caixas girados), com filtros.
TEST(raycast_2d_bvh_equals_brute_force) {
  Rng r;
  std::vector<Transform2D> tf; std::vector<Rigidbody2D> rb; std::vector<Collider2D> col;
  for (int i = 0; i < 500; ++i) {
    Transform2D t; t.position = {r.range(-30.f, 30.f), r.range(-30.f, 30.f)}; t.rotation = r.range(0.f, 6.2831853f);
    Rigidbody2D b; b.SetMass(0.f);
    Collider2D c;
    if (i % 2) { c.type = ShapeType2D::Circle; c.radius = r.range(0.3f, 1.5f); }
    else { c.type = ShapeType2D::Box; c.halfExtents = {r.range(0.3f, 1.5f), r.range(0.3f, 1.5f)}; }
    c.layer = static_cast<uint32_t>(i % 4); c.isTrigger = (i % 6 == 0);
    tf.push_back(t); rb.push_back(b); col.push_back(c);
  }
  PhysicsWorld2D w;
  w.Simulate(tf, rb, col, 0.001f);                                          // monta o cache (0 passos fixos)
  int hits = 0, mismatches = 0;
  for (int i = 0; i < 1000; ++i) {
    const Vec2 o{r.range(-35.f, 35.f), r.range(-35.f, 35.f)};
    Vec2 d{r.range(-1.f, 1.f), r.range(-1.f, 1.f)}; if (d.SqrMagnitude() < 1e-3f) d = {1.f, 0.f};
    RaycastFilter f;
    if (i % 3 == 1) { f.layerMask = 0b0110u; f.triggers = QueryTriggerInteraction::Ignore; }
    const RaycastHit2D a = w.Raycast(o, d, 80.f, f), b = w.RaycastBruteForce(o, d, 80.f, f);
    if (a.hit != b.hit || (a.hit && (a.entity != b.entity || std::fabs(a.distance - b.distance) > 1e-4f))) ++mismatches;
    if (a.hit) ++hits;
  }
  CHECK(mismatches == 0);
  CHECK(hits > 200);
  // Atalho antigo continua valendo.
  const RaycastHit2D one = w.Raycast({-40.f, 0.f}, {1.f, 0.f}, 200.f);
  const RaycastHit2D ref = w.RaycastBruteForce({-40.f, 0.f}, {1.f, 0.f}, 200.f, RaycastFilter{});
  CHECK(one.hit == ref.hit && one.entity == ref.entity);
}

// ---------------------------------------------------------------- sem alocacao e lote
// RaycastNonAlloc: buffer do chamador, ordenado, trunca nos mais proximos; igual ao RaycastAll.
TEST(raycast_nonalloc_sorted_and_truncated) {
  Scene s;
  const float zs[5] = {12.f, 3.f, 15.f, 9.f, 6.f};
  for (float z : zs) s.sphere({0.f, 0.f, z}, 1.f);
  PhysicsWorld w; w.Rebuild(s.tf, s.rb, s.col);
  RaycastHit buf[8];
  CHECK(w.RaycastNonAlloc({0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 100.f, RaycastFilter{}, buf, 8) == 5);
  for (size_t i = 1; i < 5; ++i) CHECK(buf[i - 1].distance <= buf[i].distance);
  RaycastHit small[2];
  CHECK(w.RaycastNonAlloc({0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 100.f, RaycastFilter{}, small, 2) == 2);
  CHECK(near(small[0].distance, 2.f) && near(small[1].distance, 5.f));      // os 2 mais proximos (z=3 e z=6)
  CHECK(w.RaycastNonAlloc({0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 100.f, RaycastFilter{}, nullptr, 0) == 0);
}

// RaycastBatch: mesma saida (e mesma ordem) que raios individuais, com 1 thread (grain = count) e com varias.
TEST(raycast_batch_matches_single_and_times_threads) {
  Scene s; buildRandomScene(s, 1000);
  PhysicsWorld w; w.Rebuild(s.tf, s.rb, s.col);
  Rng r; r.s = 5u;
  const size_t N = 20000;
  std::vector<Vec3> os(N), ds(N);
  for (size_t i = 0; i < N; ++i) {
    os[i] = {r.range(-25.f, 25.f), r.range(-25.f, 25.f), r.range(-25.f, 25.f)};
    ds[i] = {r.range(-1.f, 1.f), r.range(-1.f, 1.f), r.range(-1.f, 1.f)}; if (ds[i].SqrMagnitude() < 1e-3f) ds[i] = {1.f, 0.f, 0.f};
  }
  std::vector<RaycastHit> one(N), many(N);
  const RaycastFilter f;
  const auto t0 = std::chrono::steady_clock::now();
  w.RaycastBatch(os.data(), ds.data(), N, 60.f, f, one.data(), N);          // grain = N: tudo inline numa thread
  const auto t1 = std::chrono::steady_clock::now();
  w.RaycastBatch(os.data(), ds.data(), N, 60.f, f, many.data(), 256);       // varias threads
  const auto t2 = std::chrono::steady_clock::now();
  int bad = 0;
  for (size_t i = 0; i < N; ++i) {
    const RaycastHit ref = w.Raycast(os[i], ds[i], 60.f, f);
    if (one[i].hit != ref.hit || many[i].hit != ref.hit || one[i].entity != ref.entity || many[i].entity != ref.entity ||
        one[i].distance != ref.distance || many[i].distance != ref.distance) ++bad;
  }
  CHECK(bad == 0);
  std::printf("    [medicao] lote de %zu raios x 1000 formas: 1 thread %.3f ms, grain 256 (%zu threads no pool) %.3f ms\n", N,
              std::chrono::duration<double, std::milli>(t1 - t0).count(),
              tool::ParallelPool::get().threads(), std::chrono::duration<double, std::milli>(t2 - t1).count());
}
