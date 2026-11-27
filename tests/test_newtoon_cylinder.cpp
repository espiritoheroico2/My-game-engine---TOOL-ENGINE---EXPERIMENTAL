// Testes do cilindro do Newtoon (ShapeType::Cylinder): massa/inercia, contatos (normal e profundidade) contra
// cilindro, caixa, esfera e casco, repouso no chao e pilha estavel. Tambem mede 200 cilindros empilhados.
#include "test.hpp"
#include "tool/physics/newtoon.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace Newtoon;

namespace {

// Casco de cubo (meia aresta 1), usado como "casco" generico nos testes de contato.
const ConvexHull& cubeHull() {
  static const ConvexHull h = [] {
    std::vector<Vec3> p;
    for (int s = 0; s < 8; ++s) p.push_back({(s & 1) ? 1.f : -1.f, (s & 2) ? 1.f : -1.f, (s & 4) ? 1.f : -1.f});
    return BuildSimplifiedHull(p, 24);
  }();
  return h;
}

bool finite3(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

struct Scene {
  std::vector<Transform> tf; std::vector<Rigidbody> rb; std::vector<Collider> col;
  void ground() {   // corpo 0: chao (caixa estatica grande), face de cima em y = 0
    tf.push_back({}); rb.push_back({}); col.push_back({});
    tf[0].position = {0.f, -0.5f, 0.f}; col[0].type = ShapeType::Box; col[0].extents = {50.f, 0.5f, 50.f}; rb[0].SetMass(0.f);
  }
  // Cilindro dinamico de raio r e meia altura hh.
  size_t addCyl(const Vec3& pos, float r = 0.5f, float hh = 0.5f, float mass = 1.f) {
    Transform t; t.position = pos; tf.push_back(t);
    Collider c; c.type = ShapeType::Cylinder; c.radius = r; c.halfHeight = hh; col.push_back(c);
    Rigidbody b; SetMassFromShape(b, c, mass); rb.push_back(b);
    return tf.size() - 1;
  }
};

}  // namespace

// Massa e inercia do cilindro solido: Iy = m r^2/2, Ix = Iz = m (3 r^2 + h^2)/12.
TEST(cylinder_mass_and_inertia) {
  Rigidbody rb; Collider c; c.type = ShapeType::Cylinder; c.radius = 0.5f; c.halfHeight = 0.5f;
  SetMassFromShape(rb, c, 2.f);
  CHECK(std::fabs(rb.inverseInertiaLocal.y - 1.f / 0.25f) < 1e-3f);                   // Iy = 2 * 0.25 / 2 = 0.25
  CHECK(std::fabs(rb.inverseInertiaLocal.x - 1.f / (2.f * 1.75f / 12.f)) < 1e-3f);    // Ixz = 2 * (0.75 + 1) / 12
  CHECK(std::fabs(rb.inverseInertiaLocal.x - rb.inverseInertiaLocal.z) < 1e-6f);
  // Sem halfHeight (negativo): usa extents.y como meia altura.
  Rigidbody rb2; Collider c2; c2.type = ShapeType::Cylinder; c2.radius = 0.5f; c2.extents = {0.5f, 0.5f, 0.5f};
  SetMassFromShape(rb2, c2, 2.f);
  CHECK(std::fabs(rb2.inverseInertiaLocal.x - rb.inverseInertiaLocal.x) < 1e-3f);
}

// (f) cilindro x esfera, caixa, casco e cilindro: normal (de A para B) e profundidade corretas.
TEST(cylinder_contact_normals) {
  const Mat3 I;
  ConvexView cv, ov;
  ConvexContact cc[kConvexMaxContacts];
  BuildCylinderView(I, {0.f, 0.f, 0.f}, 0.5f, 0.5f, cv);

  // Esfera ao lado (+x): normal da ESFERA para o cilindro aponta para -x.
  ConvexContact sc;
  CHECK(SphereConvexCollide({0.95f, 0.f, 0.f}, 0.5f, cv, sc));
  CHECK(sc.n.x < -0.99f && std::fabs(sc.depth - 0.05f) < 0.01f);
  // Esfera em cima: normal -y.
  CHECK(SphereConvexCollide({0.f, 0.95f, 0.f}, 0.5f, cv, sc));
  CHECK(sc.n.y < -0.99f && std::fabs(sc.depth - 0.05f) < 0.01f);
  CHECK(!SphereConvexCollide({1.2f, 0.f, 0.f}, 0.5f, cv, sc));                          // separada

  // Cilindro (A) x caixa (B) ao lado (+x): normal +x. Ordem trocada: -x.
  BuildBoxView(I, {0.95f, 0.f, 0.f}, {0.5f, 0.5f, 0.5f}, ov);
  int n = ConvexCollide(cv, ov, cc);
  // A face do prisma que toca a caixa tem pontos de profundidades diferentes (a aresta vertical em x=0.5 afunda
  // 0.05; os vertices vizinhos menos): o MAIOR valor e a penetracao real.
  float maxDepth = 0.f;
  CHECK(n >= 1); for (int i = 0; i < n; ++i) { CHECK(cc[i].n.x > 0.99f); maxDepth = std::max(maxDepth, cc[i].depth); }
  CHECK(std::fabs(maxDepth - 0.05f) < 0.01f);
  n = ConvexCollide(ov, cv, cc);
  CHECK(n >= 1); for (int i = 0; i < n; ++i) CHECK(cc[i].n.x < -0.99f);

  // Cilindro x casco (cubo escalado 0.5) em cima (+y): normal +y.
  BuildConvexView(cubeHull(), I, {0.f, 0.95f, 0.f}, {0.5f, 0.5f, 0.5f}, ov);
  n = ConvexCollide(cv, ov, cc);
  CHECK(n >= 1); for (int i = 0; i < n; ++i) { CHECK(cc[i].n.y > 0.99f); CHECK(std::fabs(cc[i].depth - 0.05f) < 0.01f); }
  // Casco abaixo (-y): normal -y.
  BuildConvexView(cubeHull(), I, {0.f, -0.95f, 0.f}, {0.5f, 0.5f, 0.5f}, ov);
  n = ConvexCollide(cv, ov, cc);
  CHECK(n >= 1); for (int i = 0; i < n; ++i) CHECK(cc[i].n.y < -0.99f);

  // Cilindro x cilindro lado a lado (+x) e empilhados (+y).
  BuildCylinderView(I, {0.95f, 0.f, 0.f}, 0.5f, 0.5f, ov);
  n = ConvexCollide(cv, ov, cc);
  // Vertice contra vertice: a normal do prisma de 16 lados pode inclinar ate 11.25 graus (cos = 0.981).
  CHECK(n >= 1); for (int i = 0; i < n; ++i) CHECK(cc[i].n.x > 0.97f);
  BuildCylinderView(I, {0.f, 0.95f, 0.f}, 0.5f, 0.5f, ov);
  n = ConvexCollide(cv, ov, cc);
  CHECK(n >= 1); for (int i = 0; i < n; ++i) { CHECK(cc[i].n.y > 0.99f); CHECK(std::fabs(cc[i].depth - 0.05f) < 0.01f); }
  BuildCylinderView(I, {2.f, 0.f, 0.f}, 0.5f, 0.5f, ov);                                // separados
  CHECK(ConvexCollide(cv, ov, cc) == 0);
}

// (e) cilindro em pe repousa no chao sem atravessar e sem explodir.
TEST(cylinder_rests_on_ground_standing) {
  Scene s; s.ground(); const size_t b = s.addCyl({0.f, 1.2f, 0.f});
  PhysicsWorld w;
  for (int f = 0; f < 300; ++f) w.Simulate(s.tf, s.rb, s.col, 1.f / 60.f);
  const Vec3 p = s.tf[b].position;
  CHECK(finite3(p));
  CHECK(std::fabs(p.y - 0.5f) < 0.02f);                      // centro a meia altura do chao
  CHECK(std::fabs(p.x) < 0.05f && std::fabs(p.z) < 0.05f);
  CHECK(s.rb[b].velocity.Magnitude() < 0.05f);
}

// (e2) cilindro deitado (eixo horizontal) apoia numa face do prisma: altura ~ raio (0.49..0.5).
TEST(cylinder_rests_on_ground_lying) {
  Scene s; s.ground(); const size_t b = s.addCyl({0.f, 1.2f, 0.f});
  s.tf[b].rotation = Quat::FromAxisAngle({0.f, 0.f, 1.f}, 1.57079633f);
  PhysicsWorld w;
  float maxSpeed = 0.f;
  for (int f = 0; f < 600; ++f) { w.Simulate(s.tf, s.rb, s.col, 1.f / 60.f); maxSpeed = std::max(maxSpeed, s.rb[b].velocity.Magnitude()); }
  const Vec3 p = s.tf[b].position;
  CHECK(finite3(p) && maxSpeed < 12.f);
  CHECK(p.y > 0.46f && p.y < 0.53f);
  CHECK(s.rb[b].velocity.Magnitude() < 0.1f);
}

// (e3) pilha de 3 cilindros estavel e com a altura certa; a pilha dorme.
TEST(cylinder_stack_of_three_is_stable) {
  Scene s; s.ground();
  size_t id[3]; for (int i = 0; i < 3; ++i) id[i] = s.addCyl({0.f, 0.5f + static_cast<float>(i) * 1.02f, 0.f});
  PhysicsWorld w;
  for (int f = 0; f < 420; ++f) w.Simulate(s.tf, s.rb, s.col, 1.f / 60.f);
  for (int i = 0; i < 3; ++i) {
    const Vec3 p = s.tf[id[i]].position;
    CHECK(finite3(p));
    CHECK(std::fabs(p.y - (0.5f + static_cast<float>(i))) < 0.08f);
    CHECK(std::fabs(p.x) < 0.1f && std::fabs(p.z) < 0.1f);
    CHECK(s.rb[id[i]].velocity.Magnitude() < 0.1f);
  }
  CHECK(w.stats.awake == 0);
}

// Esfera cai sobre um cilindro estatico (Collide com esfera nas duas ordens): assenta em cima.
TEST(cylinder_static_holds_sphere) {
  Scene s; s.ground();
  s.tf.push_back({}); s.tf.back().position = {0.f, 0.5f, 0.f};                           // cilindro estatico: topo em y = 1
  Collider cc; cc.type = ShapeType::Cylinder; cc.radius = 1.f; cc.halfHeight = 0.5f; s.col.push_back(cc);
  Rigidbody sb; sb.SetMass(0.f); s.rb.push_back(sb);
  s.tf.push_back({}); s.tf.back().position = {0.1f, 2.5f, 0.f};
  Collider sc; sc.type = ShapeType::Sphere; sc.radius = 0.4f; s.col.push_back(sc);
  Rigidbody sr; SetMassFromShape(sr, sc, 1.f); s.rb.push_back(sr);
  PhysicsWorld w;
  for (int f = 0; f < 300; ++f) w.Simulate(s.tf, s.rb, s.col, 1.f / 60.f);
  const Vec3 p = s.tf[2].position;
  CHECK(finite3(p));
  CHECK(p.y > 1.35f && p.y < 1.5f);                                                     // topo (1.0) + raio (0.4)
}

// Medicao: 200 cilindros (grade 5x5, 8 andares) caindo e assentando. Imprime ms por passo; exige so ausencia de NaN/queda.
TEST(cylinder_pile_of_200_timing) {
  Scene s; s.ground();
  for (int layer = 0; layer < 8; ++layer)
    for (int ix = 0; ix < 5; ++ix)
      for (int iz = 0; iz < 5; ++iz)
        s.addCyl({static_cast<float>(ix) * 1.3f - 2.6f, 0.5f + static_cast<float>(layer) * 1.05f, static_cast<float>(iz) * 1.3f - 2.6f});
  PhysicsWorld w;
  const auto t0 = std::chrono::steady_clock::now();
  const int frames = 300;
  for (int f = 0; f < frames; ++f) w.Simulate(s.tf, s.rb, s.col, 1.f / 60.f);
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  int bad = 0, awake = 0; float minY = 1e9f;
  for (size_t i = 1; i < s.tf.size(); ++i) {
    if (!finite3(s.tf[i].position)) ++bad;
    minY = std::min(minY, s.tf[i].position.y);
    if (!s.rb[i].isSleeping) ++awake;
  }
  std::printf("    [medicao] 200 cilindros, %d passos: %.2f ms total, %.3f ms/passo (minY %.3f, acordados %d)\n", frames, ms, ms / frames, static_cast<double>(minY), awake);
  CHECK(s.tf.size() == 201);
  CHECK(bad == 0);
  CHECK(minY > 0.3f);                                                                    // nenhum atravessou o chao
}
