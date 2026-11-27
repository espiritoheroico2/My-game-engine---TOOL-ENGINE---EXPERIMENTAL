// Testes da colisao convexa do Newtoon: casco (ShapeType::CustomConvex) x chao/caixa/casco pelo SAT, LOD por distancia.
#include "test.hpp"
#include "tool/physics/newtoon.hpp"

#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

using namespace Newtoon;

namespace {

// Cilindro de 16 lados (raio 0.5, altura 1, centro na origem): tem tampa e fundo planos, entao repousa e empilha.
const ConvexHull& cylinderHull() {
  static const ConvexHull h = [] {
    std::vector<Vec3> p;
    for (int i = 0; i < 16; ++i) {
      const float a = 6.2831853f * static_cast<float>(i) / 16.f;
      p.push_back({0.5f * std::cos(a), -0.5f, 0.5f * std::sin(a)});
      p.push_back({0.5f * std::cos(a), 0.5f, 0.5f * std::sin(a)});
    }
    return BuildSimplifiedHull(p, 24);
  }();
  return h;
}

// Collider de casco com a caixa envolvente como extents (inercia e fallback).
Collider hullCollider(const ConvexHull& h) {
  Collider c; c.type = ShapeType::CustomConvex; c.hull = &h;
  c.extents = {0.5f * (h.bounds.max.x - h.bounds.min.x), 0.5f * (h.bounds.max.y - h.bounds.min.y), 0.5f * (h.bounds.max.z - h.bounds.min.z)};
  return c;
}

struct Scene {
  std::vector<Transform> tf; std::vector<Rigidbody> rb; std::vector<Collider> col;
  void ground() {   // corpo 0: chao (caixa estatica grande), face de cima em y = 0
    tf.push_back({}); rb.push_back({}); col.push_back({});
    tf[0].position = {0.f, -0.5f, 0.f}; col[0].type = ShapeType::Box; col[0].extents = {50.f, 0.5f, 50.f}; rb[0].SetMass(0.f);
  }
  size_t addHull(const ConvexHull& h, const Vec3& pos, float mass = 1.f) {
    Transform t; t.position = pos; tf.push_back(t);
    Collider c = hullCollider(h); col.push_back(c);
    Rigidbody r; SetMassFromShape(r, c, mass); rb.push_back(r);
    return tf.size() - 1;
  }
};

bool finite3(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

}  // namespace

// (a) um casco solto sobre o chao repousa sem atravessar e sem explodir.
TEST(convex_hull_rests_on_ground) {
  const ConvexHull& h = cylinderHull();
  CHECK(h.Solid());
  Scene s; s.ground(); const size_t b = s.addHull(h, {0.f, 1.2f, 0.f});
  PhysicsWorld w;
  for (int f = 0; f < 300; ++f) w.Simulate(s.tf, s.rb, s.col, 1.f / 60.f);
  const Vec3 p = s.tf[b].position;
  CHECK(finite3(p));
  CHECK(std::fabs(p.y - 0.5f) < 0.02f);                 // centro a meia altura do chao (nao atravessou)
  CHECK(std::fabs(p.x) < 0.05f && std::fabs(p.z) < 0.05f);
  CHECK(s.rb[b].velocity.Magnitude() < 0.05f);
}

// (b) pilha de 3 cascos estavel e com a altura certa.
TEST(convex_hull_stack_of_three_is_stable) {
  const ConvexHull& h = cylinderHull();
  Scene s; s.ground();
  size_t id[3]; for (int i = 0; i < 3; ++i) id[i] = s.addHull(h, {0.f, 0.5f + static_cast<float>(i) * 1.02f, 0.f});
  PhysicsWorld w;
  for (int f = 0; f < 420; ++f) w.Simulate(s.tf, s.rb, s.col, 1.f / 60.f);
  for (int i = 0; i < 3; ++i) {
    const Vec3 p = s.tf[id[i]].position;
    CHECK(finite3(p));
    CHECK(std::fabs(p.y - (0.5f + static_cast<float>(i))) < 0.08f);   // altura total: 3 cilindros de 1 m
    CHECK(std::fabs(p.x) < 0.1f && std::fabs(p.z) < 0.1f);
    CHECK(s.rb[id[i]].velocity.Magnitude() < 0.1f);
  }
  CHECK(w.stats.awake == 0);                             // a pilha dormiu: ficou parada
}

// Extra: casco inclinado e pesado (50 kg) cai sobre o chao e assenta sem explodir (contatos de aresta/vertice + face).
TEST(convex_tilted_heavy_hull_settles) {
  const ConvexHull& h = cylinderHull();
  Scene s; s.ground(); const size_t b = s.addHull(h, {0.f, 1.5f, 0.f}, 50.f);
  s.tf[b].rotation = Quat::FromAxisAngle({0.3f, 0.f, 1.f}, 0.5f);
  PhysicsWorld w;
  float maxSpeed = 0.f;
  for (int f = 0; f < 600; ++f) { w.Simulate(s.tf, s.rb, s.col, 1.f / 60.f); maxSpeed = std::max(maxSpeed, s.rb[b].velocity.Magnitude()); }
  const Vec3 p = s.tf[b].position;
  CHECK(finite3(p) && maxSpeed < 15.f);                  // queda de ~1.5 m: nunca passa de ~6 m/s sem explodir
  CHECK(p.y > 0.3f && p.y < 0.9f);                       // apoiado no chao, nao atravessou
  CHECK(s.rb[b].velocity.Magnitude() < 0.1f);
}

// (c) casco contra caixa: normal com sinal e direcao corretos (de A para B).
TEST(convex_hull_vs_box_contact_normal) {
  const ConvexHull& h = cylinderHull();
  const Mat3 I;
  ConvexView hv, bv;
  // Caixa ao lado (+x): sobreposicao de 0.05 em x.
  BuildConvexView(h, I, {0.f, 0.f, 0.f}, {1.f, 1.f, 1.f}, hv);
  BuildBoxView(I, {0.95f, 0.f, 0.f}, {0.5f, 0.5f, 0.5f}, bv);
  ConvexContact cc[kConvexMaxContacts];
  int n = ConvexCollide(hv, bv, cc);
  CHECK(n >= 1);
  for (int i = 0; i < n; ++i) { CHECK(cc[i].n.x > 0.99f); CHECK(std::fabs(cc[i].depth - 0.05f) < 0.01f); }
  // Ordem trocada (caixa = A): a normal inverte.
  n = ConvexCollide(bv, hv, cc);
  CHECK(n >= 1);
  for (int i = 0; i < n; ++i) CHECK(cc[i].n.x < -0.99f);
  // Caixa em cima (+y): normal +y.
  BuildBoxView(I, {0.f, 0.95f, 0.f}, {0.5f, 0.5f, 0.5f}, bv);
  n = ConvexCollide(hv, bv, cc);
  CHECK(n >= 1);
  for (int i = 0; i < n; ++i) CHECK(cc[i].n.y > 0.99f);
  // Caixa abaixo (-y): normal -y.
  BuildBoxView(I, {0.f, -0.95f, 0.f}, {0.5f, 0.5f, 0.5f}, bv);
  n = ConvexCollide(hv, bv, cc);
  CHECK(n >= 1);
  for (int i = 0; i < n; ++i) CHECK(cc[i].n.y < -0.99f);
  // Separados: sem contato.
  BuildBoxView(I, {1.2f, 0.f, 0.f}, {0.5f, 0.5f, 0.5f}, bv);
  CHECK(ConvexCollide(hv, bv, cc) == 0);
  // Prova que o casco NAO e tratado como caixa: um octaedro (raio 1) tem as AABBs sobrepostas com a caixa em (1.2,1.2,0),
  // mas as superficies estao separadas (o canto da caixa em x+y = 1.4 > 1). Perto o bastante, ha contato.
  {
    const std::vector<Vec3> oct = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    const ConvexHull oh = BuildSimplifiedHull(oct, 24);
    CHECK(oh.Solid());
    ConvexView ov; BuildConvexView(oh, I, {0.f, 0.f, 0.f}, {1.f, 1.f, 1.f}, ov);
    BuildBoxView(I, {1.2f, 1.2f, 0.f}, {0.5f, 0.5f, 0.5f}, bv);
    CHECK(ConvexCollide(ov, bv, cc) == 0);
    BuildBoxView(I, {0.9f, 0.9f, 0.f}, {0.5f, 0.5f, 0.5f}, bv);        // corner em x+y = 0.8 < 1: penetra
    n = ConvexCollide(ov, bv, cc);
    CHECK(n >= 1);
    for (int i = 0; i < n; ++i) CHECK(cc[i].n.x > 0.3f && cc[i].n.y > 0.3f);   // normal diagonal (x+y), nao um eixo
  }
  // Casco x casco, mesma regra (B a +x de A).
  ConvexView h2; BuildConvexView(h, I, {0.9f, 0.f, 0.f}, {1.f, 1.f, 1.f}, h2);
  n = ConvexCollide(hv, h2, cc);
  CHECK(n >= 1 && n <= kConvexMaxContacts);
  for (int i = 0; i < n; ++i) CHECK(cc[i].n.x > 0.99f);
}

// Casco x esfera e raio contra casco (caminhos auxiliares da narrowphase).
TEST(convex_hull_vs_sphere_and_ray) {
  const ConvexHull& h = cylinderHull();
  const Mat3 I; ConvexView hv; BuildConvexView(h, I, {0.f, 0.f, 0.f}, {1.f, 1.f, 1.f}, hv);
  ConvexContact c;
  CHECK(SphereConvexCollide({0.f, 0.7f, 0.f}, 0.3f, hv, c));     // esfera encostando na tampa: normal esfera->casco = -y
  CHECK(c.n.y < -0.99f && std::fabs(c.depth - 0.1f) < 0.02f);
  CHECK(!SphereConvexCollide({0.f, 1.0f, 0.f}, 0.3f, hv, c));
  float t; Vec3 n;
  CHECK(RayConvex(h, I, {0.f, 0.f, 0.f}, {1.f, 1.f, 1.f}, {0.f, 3.f, 0.f}, {0.f, -1.f, 0.f}, 10.f, t, n));
  CHECK(std::fabs(t - 2.5f) < 0.01f && n.y > 0.99f);
}

// (d) LOD: pontos nao crescem com a distancia; sem foco usa o maximo.
TEST(convex_lod_selection_and_focus) {
  const ColliderLodConfig cfg;
  CHECK(chooseColliderLod(0.f, cfg) == 24 && chooseColliderLod(20.f, cfg) == 24);
  CHECK(chooseColliderLod(20.01f, cfg) == 12 && chooseColliderLod(60.f, cfg) == 12);
  CHECK(chooseColliderLod(60.01f, cfg) == 8 && chooseColliderLod(1e6f, cfg) == 8);
  CHECK(chooseColliderLod(std::nanf(""), cfg) == 24);
  int prev = 1000;
  for (float d = 0.f; d < 200.f; d += 0.5f) { const int m = chooseColliderLod(d, cfg); CHECK(m <= prev); prev = m; }
  ColliderLodConfig odd; odd.nearPoints = 12; odd.midPoints = 24; odd.farPoints = 48;   // fora de ordem: continua monotonico
  prev = 1000;
  for (float d = 0.f; d < 200.f; d += 0.5f) { const int m = chooseColliderLod(d, odd); CHECK(m <= prev); prev = m; }

  // Conjunto de LODs vindo do cache (chave malha + maxPoints) e escolha pelo mundo.
  std::vector<Vec3> mesh;   // elipsoide: k-DOP de 24 pontos tem mais pontos que o de 12 e o de 8
  for (int i = 0; i < 400; ++i) {
    const float u = 6.2831853f * static_cast<float>((i * 37) % 400) / 400.f, v = 3.1415926f * (static_cast<float>(i) + 0.5f) / 400.f;
    mesh.push_back({2.f * std::sin(v) * std::cos(u), 1.f * std::cos(v), 3.f * std::sin(v) * std::sin(u)});
  }
  HullCache cache; int builds = 0; int key = 0;
  auto build = [&] { ++builds; return mesh; };
  auto lods = cache.GetLods(&key, 1, cfg, build);
  CHECK(builds == 1);
  CHECK(cache.GetLods(&key, 1, cfg, build) == lods && builds == 1);   // segundo pedido: cache
  CHECK(lods->count == 3);
  const size_t nNear = lods->Pick(24)->points.size(), nMid = lods->Pick(12)->points.size(), nFar = lods->Pick(8)->points.size();
  CHECK(nNear >= nMid && nMid >= nFar && nFar == 8 && nNear > nFar);
  CHECK(lods->Max() == lods->Pick(48));

  Transform t; t.position = {10.f, 0.f, 0.f};
  Collider c; c.type = ShapeType::CustomConvex; c.hullLods = lods.get(); c.extents = {2.f, 1.f, 3.f};
  Rigidbody r; SetMassFromShape(r, c, 1.f); r.useGravity = false;
  std::vector<Transform> tf{t}; std::vector<Rigidbody> rb{r}; std::vector<Collider> col{c};
  PhysicsWorld w;
  w.Rebuild(tf, rb, col);
  CHECK(w.ActiveHullPoints(0) == nNear);                  // sem foco: LOD desligado, usa o maximo
  w.setCollisionFocus({0.f, 0.f, 0.f});                   // 10 m: perto
  w.Simulate(tf, rb, col, 1.f / 60.f);
  CHECK(w.ActiveHullPoints(0) == nNear);
  w.setCollisionFocus({-40.f, 0.f, 0.f});                 // 50 m: medio
  CHECK(w.ActiveHullPoints(0) == nNear);                  // a troca so vale no proximo passo fixo
  w.Simulate(tf, rb, col, 1.f / 60.f);
  CHECK(w.ActiveHullPoints(0) == nMid);
  w.setCollisionFocus({-200.f, 0.f, 0.f});                // longe
  w.Simulate(tf, rb, col, 1.f / 60.f);
  CHECK(w.ActiveHullPoints(0) == nFar);
  w.clearCollisionFocus();
  w.Simulate(tf, rb, col, 1.f / 60.f);
  CHECK(w.ActiveHullPoints(0) == nNear);
}

// (e) determinismo: a mesma entrada produz exatamente a mesma saida.
TEST(convex_is_deterministic) {
  const ConvexHull& h = cylinderHull();
  auto run = [&](std::vector<Transform>& out) {
    Scene s; s.ground();
    for (int i = 0; i < 4; ++i) s.addHull(h, {0.1f * static_cast<float>(i), 0.6f + static_cast<float>(i) * 1.1f, 0.05f * static_cast<float>(i)});
    PhysicsWorld w;
    for (int f = 0; f < 200; ++f) w.Simulate(s.tf, s.rb, s.col, 1.f / 60.f);
    out = s.tf;
  };
  std::vector<Transform> a, b; run(a); run(b);
  CHECK(a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(Transform)) == 0);
  // Mesma consulta SAT duas vezes: contatos identicos.
  const Mat3 I; ConvexView hv, h2; BuildConvexView(h, I, {0.f, 0.f, 0.f}, {1.f, 1.f, 1.f}, hv); BuildConvexView(h, I, {0.8f, 0.2f, 0.1f}, {1.f, 1.f, 1.f}, h2);
  ConvexContact c1[kConvexMaxContacts], c2[kConvexMaxContacts];
  const int n1 = ConvexCollide(hv, h2, c1), n2 = ConvexCollide(hv, h2, c2);
  CHECK(n1 == n2 && n1 > 0);
  for (int i = 0; i < n1; ++i) CHECK(c1[i].p.x == c2[i].p.x && c1[i].p.y == c2[i].p.y && c1[i].p.z == c2[i].p.z && c1[i].depth == c2[i].depth);
}
