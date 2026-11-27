// Alvo separado (fora do ctest padrao): 100k corpos 3D + 100k 2D para medir throughput. Executar manualmente.
// Teste de escala massiva (100.000 corpos 3D + 100.000 corpos 2D) para validar throughput e baixo uso de memoria.
#include "tool/physics/newtoon.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace Newtoon;

static int gFailures = 0;
static void Check(bool ok, const char* what) {
  std::printf("%s: %s\n", ok ? "ok" : "FALHOU", what);
  if (!ok) ++gFailures;
}

int main() {
  // 1) 100.000 corpos 3D (98.000 estaticos/dormindo + 2.000 dinamicos ativos)
  {
    constexpr size_t kTotal = 100000;
    constexpr size_t kActive = 2000;
    std::vector<Transform> tf(kTotal);
    std::vector<Rigidbody> rb(kTotal);
    std::vector<Collider> col(kTotal);

    for (size_t i = 0; i < kTotal; ++i) {
      const float x = static_cast<float>(i % 500) * 3.f;
      const float z = static_cast<float>(i / 500) * 3.f;
      col[i].type = ShapeType::Box;
      col[i].extents = {0.5f, 0.5f, 0.5f};
      if (i < kActive) {
        tf[i].position = {x, 5.f, z};
        SetMassFromShape(rb[i], col[i], 1.f);
      } else {
        tf[i].position = {x, 0.f, z};
        rb[i].SetMass(0.f);
      }
    }

    PhysicsWorld world;
    world.enableInterpolation = false; // economia maxima de banda de memoria
    world.Rebuild(tf, rb, col);

    const auto t0 = std::chrono::steady_clock::now();
    for (int f = 0; f < 10; ++f) {
      world.Simulate(tf, rb, col, 1.f / 60.f);
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("3D 100k corpos (10 passos): %.2f ms total (%.2f ms/passo)\n", ms, ms / 10.0);
    Check(world.stats.bodies == static_cast<int>(kTotal), "3D 100k corpos contabilizados");
    Check(world.stats.awake <= static_cast<int>(kActive), "3D apenas corpos ativos simulados");
  }

  // 2) 100.000 corpos 2D (98.000 estaticos/dormindo + 2.000 dinamicos ativos)
  {
    constexpr size_t kTotal = 100000;
    constexpr size_t kActive = 2000;
    std::vector<Transform2D> tf(kTotal);
    std::vector<Rigidbody2D> rb(kTotal);
    std::vector<Collider2D> col(kTotal);

    for (size_t i = 0; i < kTotal; ++i) {
      const float x = static_cast<float>(i % 500) * 3.f;
      const float y = static_cast<float>(i / 500) * 3.f;
      col[i].type = ShapeType2D::Circle;
      col[i].radius = 0.5f;
      if (i < kActive) {
        tf[i].position = {x, y + 5.f};
        SetMassFromShape2D(rb[i], col[i], 1.f);
      } else {
        tf[i].position = {x, y};
        rb[i].SetMass(0.f);
      }
    }

    PhysicsWorld2D world2d;
    world2d.enableInterpolation = false;
    // Aquecimento: o primeiro Simulate monta o cache (equivalente ao Rebuild do 3D). Fica fora do cronometro.
    world2d.Simulate(tf, rb, col, 1.f / 60.f);
    const auto t0 = std::chrono::steady_clock::now();
    for (int f = 0; f < 10; ++f) {
      world2d.Simulate(tf, rb, col, 1.f / 60.f);
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("2D 100k corpos (10 passos): %.2f ms total (%.2f ms/passo)\n", ms, ms / 10.0);
    Check(world2d.stats.bodies == static_cast<int>(kTotal), "2D 100k corpos contabilizados");
    Check(world2d.stats.awake <= static_cast<int>(kActive), "2D apenas corpos ativos simulados");
  }

  // 3) 200 corpos em 20 colunas de 10 empilhados: mede ms/passo da caixa (colisor de caixa) contra o casco convexo.
  //    Caso "cubo": o casco de 8 pontos tem a MESMA geometria e o mesmo numero de contatos da caixa (so muda o algoritmo).
  //    Caso "cilindro": casco de 16 lados (24 pontos), contatos de face com mais pontos.
  {
    std::vector<Vec3> cubePts, cylPts;
    for (int s = 0; s < 8; ++s) cubePts.push_back({(s & 1) ? 0.5f : -0.5f, (s & 2) ? 0.5f : -0.5f, (s & 4) ? 0.5f : -0.5f});
    for (int i = 0; i < 16; ++i) {
      const float a = 6.2831853f * static_cast<float>(i) / 16.f;
      cylPts.push_back({0.5f * std::cos(a), -0.5f, 0.5f * std::sin(a)}); cylPts.push_back({0.5f * std::cos(a), 0.5f, 0.5f * std::sin(a)});
    }
    const ConvexHull cubeHull = BuildSimplifiedHull(cubePts, 24), cylHull = BuildSimplifiedHull(cylPts, 24);
    struct Case { const char* name; const ConvexHull* hull; };
    const Case cases[] = {{"caixa (antes: colisor de caixa)", nullptr}, {"casco cubo 8 pts", &cubeHull}, {"casco cilindro 24 pts", &cylHull}};
    for (const Case& cs : cases) {
      constexpr int kCols = 20, kLayers = 10;
      std::vector<Transform> tf(1 + kCols * kLayers); std::vector<Rigidbody> rb(tf.size()); std::vector<Collider> col(tf.size());
      tf[0].position = {0.f, -0.5f, 0.f}; col[0].type = ShapeType::Box; col[0].extents = {100.f, 0.5f, 100.f}; rb[0].SetMass(0.f);
      for (int c = 0; c < kCols; ++c) for (int l = 0; l < kLayers; ++l) {
        const size_t i = 1 + static_cast<size_t>(c * kLayers + l);
        tf[i].position = {static_cast<float>(c) * 2.f, 0.5f + static_cast<float>(l) * 1.01f, 0.f};
        if (cs.hull) { col[i].type = ShapeType::CustomConvex; col[i].hull = cs.hull; } else col[i].type = ShapeType::Box;
        col[i].extents = {0.5f, 0.5f, 0.5f};
        SetMassFromShape(rb[i], col[i], 1.f);
        rb[i].sleepThreshold = 0.f; // nunca dorme: todos os casos medem o custo cheio com os mesmos pares
      }
      PhysicsWorld world; world.enableInterpolation = false; world.enableIslands = false;
      world.Rebuild(tf, rb, col);
      for (int f = 0; f < 30; ++f) world.Simulate(tf, rb, col, 1.f / 60.f); // aquecimento: a pilha assenta
      constexpr int kSteps = 120; double contacts = 0, narrowMs = 0;
      const auto t0 = std::chrono::steady_clock::now();
      for (int f = 0; f < kSteps; ++f) { world.Simulate(tf, rb, col, 1.f / 60.f); contacts += world.stats.contacts; narrowMs += world.stats.narrowphaseMs; }
      const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
      std::printf("200 corpos [%s]: %.3f ms/passo (narrowphase %.3f ms), %.0f contatos/passo\n", cs.name, ms / kSteps, narrowMs / kSteps, contacts / kSteps);
      Check(std::isfinite(tf[kLayers].position.y) && tf[kLayers].position.y < 12.f, "200 corpos: pilha nao explodiu");
    }
  }

  std::printf("Newtoon scale: %d falha(s)\n", gFailures);
  return gFailures == 0 ? 0 : 1;
}
