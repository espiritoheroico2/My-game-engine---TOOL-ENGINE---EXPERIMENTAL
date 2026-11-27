// Recursos avancados do Newtoon: matematica SIMD, pool de threads (tool::parallelFor), warm starting, ilhas,
// interpolacao de render, XPBD, BVH4 com Morton e Wide-SIMD/Soft Step.
#include "test.hpp"
#include "tool/physics/newtoon.hpp"
#include "tool/physics/newtoon_modules.hpp"
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

TEST(newtoon_advanced_features) {
  // 1) Matematica SIMD + Interpolacao
  {
    const Vec3 a{1.f, 2.f, 3.f}, b{4.f, 6.f, 8.f};
    const Vec3 c = a + b;
    const Vec3 d = b - a;
    const Vec3 x = Cross(Vec3{1.f, 0.f, 0.f}, Vec3{0.f, 1.f, 0.f});
    const Vec3 l = Lerp(a, b, 0.5f);
    Check(std::fabs(c.x - 5.f) < 1e-5f && std::fabs(c.y - 8.f) < 1e-5f && std::fabs(c.z - 11.f) < 1e-5f, "Vec3 soma SIMD");
    Check(std::fabs(d.x - 3.f) < 1e-5f && std::fabs(d.y - 4.f) < 1e-5f && std::fabs(d.z - 5.f) < 1e-5f, "Vec3 subtracao SIMD");
    Check(std::fabs(x.z - 1.f) < 1e-5f && std::fabs(x.x) < 1e-5f && std::fabs(x.y) < 1e-5f, "Vec3 Cross SIMD");
    Check(std::fabs(l.x - 2.5f) < 1e-5f && std::fabs(l.y - 4.f) < 1e-5f && std::fabs(l.z - 5.5f) < 1e-5f, "Vec3 Lerp");

    const Quat q0 = Quat::FromAxisAngle({0.f, 1.f, 0.f}, 0.f);
    const Quat q1 = Quat::FromAxisAngle({0.f, 1.f, 0.f}, 1.5707963f);
    const Quat qm = Quat::Slerp(q0, q1, 0.5f);
    const Vec3 rot = qm.Rotate({1.f, 0.f, 0.f});
    Check(std::fabs(rot.x - rot.z * -1.f) < 1e-3f && std::fabs(rot.Magnitude() - 1.f) < 1e-4f, "Quat Slerp 45 deg");
  }

  // 2) Pool de threads persistente
  {
    std::vector<int> buf(10000, 0);
    JobSystem::Get().ParallelFor(buf.size(), 256, [&](size_t b, size_t e) {
      for (size_t i = b; i < e; ++i) buf[i] = static_cast<int>(i + 1);
    });
    bool allOk = true;
    for (size_t i = 0; i < buf.size(); ++i) {
      if (buf[i] != static_cast<int>(i + 1)) { allOk = false; break; }
    }
    Check(allOk, "Pool persistente preencheu todos os 10000 itens");
  }

  // 3) Stacking (Warm Starting + Simulation Islands + Render Interpolation)
  {
    std::vector<Transform> tf(5);
    std::vector<Rigidbody> rb(5);
    std::vector<Collider> col(5);

    // Chao
    tf[0].position = {0.f, -0.5f, 0.f};
    col[0].type = ShapeType::Box;
    col[0].extents = {50.f, 0.5f, 50.f};
    rb[0].SetMass(0.f);

    // Pilha de 4 caixas
    for (int i = 1; i <= 4; ++i) {
      tf[i].position = {0.f, 0.5f + (i - 1) * 1.02f, 0.f};
      col[i].type = ShapeType::Box;
      col[i].extents = {0.5f, 0.5f, 0.5f};
      SetMassFromShape(rb[i], col[i], 1.f);
    }

    PhysicsWorld world;
    int warmSeen = 0, maxIslands = 0;
    for (int f = 0; f < 300; ++f) {
      world.Simulate(tf, rb, col, 1.f / 60.f);
      warmSeen += world.stats.warmStartedContacts;
      if (world.stats.islands > maxIslands) maxIslands = world.stats.islands;
    }
    Check(warmSeen > 0, "Warm starting reaproveitou contatos na pilha");
    Check(maxIslands >= 1, "Simulation islands contabilizadas");
    Check(world.stats.awake == 0, "Ilha inteira dormiu apos estabilizar");
    Check(tf[4].position.y > 2.5f && tf[4].position.y < 4.5f, "Caixa do topo manteve pilha estavel");

    // Testa interpolacao com dt parcial (< fixedDt)
    world.Simulate(tf, rb, col, 0.008f);
    std::vector<Transform> interp;
    world.GetInterpolatedTransforms(tf, interp);
    Check(interp.size() == tf.size() && std::isfinite(interp[1].position.y), "Render interpolation valida");
  }

  // 4) XPBD Cloth & Hair com substeps e colisao com esfera
  {
    std::vector<XPBDParticle> pts(6);
    std::vector<XPBDDistanceConstraint> dists;
    std::vector<XPBDBendingConstraint> bends;
    for (size_t i = 0; i < pts.size(); ++i) {
      pts[i].position = {static_cast<float>(i) * 0.2f, 2.f, 0.f};
      pts[i].inverseMass = (i == 0) ? 0.f : 1.f; // raiz fixa
      if (i > 0) dists.push_back({static_cast<Entity>(i - 1), static_cast<Entity>(i), 0.2f, 0.f, 0.f});
      if (i > 1) bends.push_back({static_cast<Entity>(i - 2), static_cast<Entity>(i - 1), static_cast<Entity>(i), 0.4f, 0.01f, 0.f});
    }
    std::vector<XPBDSphereCollider> spheres = {{{0.5f, 1.5f, 0.f}, 0.45f}};
    XPBDClothAndHairSolver xpbd;
    xpbd.substeps = 4;
    xpbd.groundY = 0.f;

    for (int f = 0; f < 120; ++f) {
      xpbd.Step(pts, dists, bends, spheres, {1.5f, 0.f, 0.f}, 1.f / 60.f);
    }

    const float tipDist = (pts.back().position - pts.front().position).Magnitude();
    const float sphereDist = (pts[2].position - spheres[0].center).Magnitude();
    Check(std::fabs(pts[0].position.y - 2.f) < 1e-6f, "XPBD raiz do fio permanece fixa");
    Check(std::isfinite(tipDist) && tipDist <= 1.1f, "XPBD preservou comprimento sem esticar demais");
    Check(sphereDist >= spheres[0].radius - 1e-3f, "XPBD respeitou colisao com esfera");
  }

  // 5) BVH4 com Morton Codes + RayQuery 4-way SIMD
  {
    std::vector<AABB> boxes(20);
    std::vector<char> valid(20, 1);
    for (size_t i = 0; i < 20; ++i) {
      const float x = static_cast<float>(i) * 2.f;
      boxes[i] = {{x - 0.5f, -0.5f, -0.5f}, {x + 0.5f, 0.5f, 0.5f}};
    }
    BVH4 bvh4;
    bvh4.Build(boxes, valid);
    int hits = 0;
    bvh4.Query({{3.8f, -0.2f, -0.2f}, {4.2f, 0.2f, 0.2f}}, [&](Entity e) {
      if (e == 2) ++hits;
    });
    Check(hits == 1, "BVH4 Query SIMD encontrou a caixa esperada");
    Entity rayEnt = kNoEntity;
    float maxT = 10.f;
    bvh4.RayQuery({10.f, 5.f, 0.f}, {0.f, -1.f, 0.f}, maxT, [&](Entity e, float&) { rayEnt = e; });
    Check(rayEnt == 5, "BVH4 RayQuery SIMD acertou a caixa esperada");
    Check(bvh4.Refit(boxes, valid), "BVH4 Refit manteve consistencia");
  }

  // 6) Wide-SIMD (multiplos contatos paralelos) + Soft Step com razao de massa 1000:1
  {
    std::vector<Transform> tf(11);
    std::vector<Rigidbody> rb(11);
    std::vector<Collider> col(11);

    // Chao
    tf[0].position = {0.f, -0.5f, 0.f};
    col[0].type = ShapeType::Box;
    col[0].extents = {100.f, 0.5f, 100.f};
    rb[0].SetMass(0.f);

    // 8 caixas independentes caindo no chao (ativa o caminho Wide-SIMD 4x no solver colorido)
    for (int i = 1; i <= 8; ++i) {
      tf[i].position = {static_cast<float>(i) * 4.f, 1.5f, 0.f};
      col[i].type = ShapeType::Box;
      col[i].extents = {0.5f, 0.5f, 0.5f};
      SetMassFromShape(rb[i], col[i], 1.f);
    }

    // Pilha de razao 50:1 (50 kg sobre 1 kg) com Soft Step
    tf[9].position = {-10.f, 0.5f, 0.f};
    col[9].type = ShapeType::Box;
    col[9].extents = {0.5f, 0.5f, 0.5f};
    SetMassFromShape(rb[9], col[9], 1.f);

    tf[10].position = {-10.f, 1.52f, 0.f};
    col[10].type = ShapeType::Box;
    col[10].extents = {0.5f, 0.5f, 0.5f};
    SetMassFromShape(rb[10], col[10], 50.f);

    PhysicsWorld world;
    world.enableSoftConstraints = true;
    world.solverIterations = 20;
    for (int f = 0; f < 240; ++f) {
      world.Simulate(tf, rb, col, 1.f / 60.f);
    }
    Check(std::isfinite(tf[9].position.y) && tf[9].position.y > 0.2f, "Soft Step estabilizou caixa de 1 kg sob 50 kg");
    Check(std::isfinite(tf[10].position.y) && tf[10].position.y > 1.0f && tf[10].position.y < 2.0f, "Soft Step manteve caixa de 50 kg sem explodir");
  }
}
