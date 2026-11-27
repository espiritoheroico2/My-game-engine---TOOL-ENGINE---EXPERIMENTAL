// Alvo separado (fora do ctest padrao): 100k corpos 3D + 100k 2D para medir throughput. Executar manualmente.
// Teste de escala massiva (100.000 corpos 3D + 100.000 corpos 2D) para validar throughput e baixo uso de memoria.
#include "tool/physics/newtoon.hpp"

#include <chrono>
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

  std::printf("Newtoon scale: %d falha(s)\n", gFailures);
  return gFailures == 0 ? 0 : 1;
}
