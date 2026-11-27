// Modulo 2D do Newtoon: matematica 2D, PhysicsWorld2D (SAP, SAT, warm starting, raycast, interpolacao).
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

TEST(newtoon_2d_module) {
  // 1) Matematica 2D e Rot2
  {
    const Vec2 a{3.f, 4.f}, b{1.f, 2.f};
    Check(std::fabs(a.Magnitude() - 5.f) < 1e-5f, "Vec2 Magnitude");
    Check(std::fabs(Dot(a, b) - 11.f) < 1e-5f, "Vec2 Dot");
    Check(std::fabs(Cross(Vec2{1.f, 0.f}, Vec2{0.f, 1.f}) - 1.f) < 1e-5f, "Vec2 Cross escalar");
    const Rot2 r = Rot2::FromAngle(1.5707963f);
    const Vec2 rv = r.Rotate({1.f, 0.f});
    Check(std::fabs(rv.x) < 1e-4f && std::fabs(rv.y - 1.f) < 1e-4f, "Rot2 90 deg");
    const Vec2 irv = r.InvRotate(rv);
    Check(std::fabs(irv.x - 1.f) < 1e-4f && std::fabs(irv.y) < 1e-4f, "Rot2 InvRotate");
  }

  // 2) Simulacao 2D: chao + pilha de caixas + circulo
  {
    std::vector<Transform2D> tf(4);
    std::vector<Rigidbody2D> rb(4);
    std::vector<Collider2D> col(4);

    // Chao estatico 2D
    tf[0].position = {0.f, -0.5f};
    col[0].type = ShapeType2D::Box;
    col[0].halfExtents = {50.f, 0.5f};
    rb[0].SetMass(0.f);

    // Duas caixas empilhadas
    for (int i = 1; i <= 2; ++i) {
      tf[i].position = {0.f, 0.5f + (i - 1) * 1.02f};
      col[i].type = ShapeType2D::Box;
      col[i].halfExtents = {0.5f, 0.5f};
      SetMassFromShape2D(rb[i], col[i], 1.f);
    }

    // Circulo caindo ao lado
    tf[3].position = {3.f, 4.f};
    col[3].type = ShapeType2D::Circle;
    col[3].radius = 0.5f;
    SetMassFromShape2D(rb[3], col[3], 1.f);

    PhysicsWorld2D world2d;
    int warmSeen = 0;
    for (int f = 0; f < 300; ++f) {
      world2d.Simulate(tf, rb, col, 1.f / 60.f);
      warmSeen += world2d.stats.warmStartedContacts;
    }

    Check(world2d.stats.bodies == 4, "PhysicsWorld2D ve os 4 corpos");
    Check(warmSeen > 0, "PhysicsWorld2D Warm Starting reaproveitou contatos");
    Check(tf[2].position.y > 1.0f && tf[2].position.y < 2.5f, "Caixa 2D superior manteve pilha estavel");
    Check(tf[3].position.y > 0.f && tf[3].position.y < 1.f, "Circulo 2D repousou sobre o chao");

    const RaycastHit2D hit = world2d.Raycast({3.f, 5.f}, {0.f, -1.f}, 10.f);
    Check(hit.hit && hit.entity == 3, "Raycast 2D acertou o circulo");

    world2d.Simulate(tf, rb, col, 0.008f);
    std::vector<Transform2D> interp;
    world2d.GetInterpolatedTransforms(tf, interp);
    Check(interp.size() == tf.size() && std::isfinite(interp[3].position.y), "Render interpolation 2D valida");
  }
}

// Broadphase 2D (sweep limitado aos ativos) deve achar exatamente os mesmos pares que a forca bruta.
// Gravidade e velocidade zero: no broadphase as posicoes sao as iniciais, entao a contagem e exata.
TEST(newtoon_2d_broadphase_matches_brute_force) {
  constexpr size_t kN = 400;
  std::vector<Transform2D> tf(kN);
  std::vector<Rigidbody2D> rb(kN);
  std::vector<Collider2D> col(kN);
  unsigned seed = 12345u;   // gerador congruencial: resultado reproduzivel
  auto rnd = [&seed]() {
    seed = seed * 1664525u + 1013904223u;
    return static_cast<float>(seed >> 8) / static_cast<float>(1u << 24);
  };
  for (size_t i = 0; i < kN; ++i) {
    col[i].type = ShapeType2D::Circle;
    col[i].radius = 0.5f + rnd();
    tf[i].position = {rnd() * 30.f, rnd() * 30.f};
    if (i % 3 == 0) SetMassFromShape2D(rb[i], col[i], 1.f);  // dinamico (1/3 dos corpos)
    else rb[i].SetMass(0.f);                                 // estatico
  }

  // Forca bruta ANTES do passo: o solver move os corpos depois do broadphase, entao o gabarito usa as posicoes
  // iniciais (mesmo instante que o broadphase le). Caixas sobrepostas (criterio de toque) e pelo menos um dinamico.
  std::size_t expected = 0;
  for (size_t i = 0; i < kN; ++i) {
    for (size_t j = i + 1; j < kN; ++j) {
      const bool dynI = (i % 3 == 0), dynJ = (j % 3 == 0);
      if (!dynI && !dynJ) continue;
      const float ri = col[i].radius, rj = col[j].radius;
      const bool overlapX = !(tf[i].position.x + ri < tf[j].position.x - rj) && !(tf[j].position.x + rj < tf[i].position.x - ri);
      const bool overlapY = !(tf[i].position.y + ri < tf[j].position.y - rj) && !(tf[j].position.y + rj < tf[i].position.y - ri);
      if (overlapX && overlapY) ++expected;
    }
  }

  PhysicsWorld2D world;
  world.gravity = {0.f, 0.f};
  world.enableInterpolation = false;
  world.Simulate(tf, rb, col, 1.f / 60.f);

  Check(expected > 0, "cenario de teste tem pares de contato");
  Check(static_cast<std::size_t>(world.stats.pairs) == expected, "broadphase 2D acha os mesmos pares que a forca bruta");
}
