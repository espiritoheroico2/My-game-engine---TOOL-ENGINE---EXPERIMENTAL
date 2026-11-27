// Fumaca do modulo Newtoon: caixa cai e repousa sobre o chao estatico.
#include "test.hpp"
#include "tool/physics/newtoon.hpp"
#include <cmath>
#include <cstdio>
#include <vector>

using namespace Newtoon;

namespace {
// Adaptador do Check original para o framework de testes da engine.
void Check(bool ok, const char* what) {
  if (!ok) { std::printf("    FALHA: %s\n", what); ++::tt::failures(); }
}
} // namespace

TEST(newtoon_module_smoke) {
  // Chao estatico + caixa dinamica caindo: depois de alguns segundos a caixa deve estar em repouso sobre o chao.
  std::vector<Transform> tf(2);
  std::vector<Rigidbody> rb(2);
  std::vector<Collider> col(2);
  tf[0].position = {0.f, 0.f, 0.f};
  col[0].type = ShapeType::Box;
  col[0].extents = {50.f, 0.5f, 50.f};
  rb[0].SetMass(0.f); // massa 0 = corpo estatico
  tf[1].position = {0.f, 5.f, 0.f};
  col[1].type = ShapeType::Box;
  col[1].extents = {0.5f, 0.5f, 0.5f};
  SetMassFromShape(rb[1], col[1], 1.f);

  PhysicsWorld world;
  for (int i = 0; i < 600; ++i) world.Simulate(tf, rb, col, 1.f / 60.f);

  Check(world.stats.bodies == 2, "o mundo ve os dois corpos");
  Check(world.stats.steps > 0, "a simulacao avancou passos");
  Check(std::isfinite(tf[1].position.y), "posicao final e finita");
  Check(tf[1].position.y > 0.f && tf[1].position.y < 2.f, "a caixa parou perto do chao (y entre 0 e 2)");
}
