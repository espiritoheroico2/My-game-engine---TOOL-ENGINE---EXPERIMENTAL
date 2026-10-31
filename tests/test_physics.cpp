#include "test.hpp"
#include "tool/physics/newtoon.hpp"
#include "tool/physics/newtoon_modules.hpp"

using namespace Newtoon;

namespace {
struct Sim {
  std::vector<Transform> t; std::vector<Rigidbody> r; std::vector<Collider> c; PhysicsWorld w;
  size_t add(Vec3 p, ShapeType s, Vec3 ext, float mass, float rad = 0.5f) {
    Transform tr; tr.position = p; Collider co; co.type = s; co.extents = ext; co.radius = rad; Rigidbody rb; SetMassFromShape(rb, co, mass);
    t.push_back(tr); c.push_back(co); r.push_back(rb); return t.size() - 1;
  }
  void run(float seconds) { for (int i = 0; i < static_cast<int>(seconds * 60); ++i) w.Simulate(t, r, c, 1.f / 60.f); }
};
} // namespace

TEST(newtoon_free_fall_matches_gravity) {
  Sim s; const size_t b = s.add({0, 100, 0}, ShapeType::Sphere, {}, 1.f);
  s.run(1.f);
  CHECK(std::fabs(s.t[b].position.y - (100.f - 0.5f * 9.81f)) < 0.2f); // ~ -4.9 m em 1 s (Euler semi-implicito)
  CHECK(s.r[b].velocity.y < -9.f && s.r[b].velocity.y > -10.2f);
}

TEST(newtoon_vector_physics_overrides_world_gravity) {
  Sim s; const size_t a = s.add({0, 0, 0}, ShapeType::Sphere, {}, 1.f), b = s.add({5, 0, 0}, ShapeType::Sphere, {}, 1.f), z = s.add({10, 0, 0}, ShapeType::Sphere, {}, 1.f);
  s.r[b].overrideGravity = true; s.r[b].gravity = {3.f, 0, 0};   // cai para o lado
  s.r[z].gravityScale = 0.f; s.w.gravity = {0, -9.81f, 0};
  s.run(1.f);
  CHECK(s.t[a].position.y < -4.f && std::fabs(s.t[a].position.x) < 1e-3f);
  CHECK(s.t[b].position.x > 5.f + 1.f && std::fabs(s.t[b].position.y) < 1e-3f);
  CHECK(std::fabs(s.t[z].position.y) < 1e-4f);
  s.w.gravity = {0, 0, -5.f}; s.t[a].position = {}; s.r[a].velocity = {}; s.r[a].WakeUp(); s.run(1.f);
  CHECK(s.t[a].position.z < -2.f); // gravidade do mundo em qualquer eixo
}

TEST(newtoon_box_rests_on_ground_and_sleeps) {
  Sim s; s.add({0, -0.5f, 0}, ShapeType::Box, {20, 0.5f, 20}, 0.f);
  const size_t b = s.add({0, 2, 0}, ShapeType::Box, {0.5f, 0.5f, 0.5f}, 1.f);
  s.run(4.f);
  CHECK(std::fabs(s.t[b].position.y - 0.5f) < 0.03f);
  CHECK(s.r[b].isSleeping);
  CHECK(s.r[b].grounded);
}

TEST(newtoon_sphere_bounces_with_restitution) {
  Sim s; s.add({0, -0.5f, 0}, ShapeType::Box, {20, 0.5f, 20}, 0.f);
  const size_t b = s.add({0, 3, 0}, ShapeType::Sphere, {}, 1.f);
  s.w.materials[0].bounciness = 0.8f; s.w.materials[0].bounceCombine = CombineMode::Maximum;
  float maxUp = 0; bool bounced = false;
  for (int i = 0; i < 240; ++i) { s.w.Simulate(s.t, s.r, s.c, 1.f / 60.f); if (s.r[b].velocity.y > 1.f) { bounced = true; maxUp = std::max(maxUp, s.r[b].velocity.y); } }
  CHECK(bounced); CHECK(maxUp > 3.f);
}

TEST(newtoon_box_stack_is_stable) {
  Sim s; s.add({0, -0.5f, 0}, ShapeType::Box, {20, 0.5f, 20}, 0.f);
  const size_t a = s.add({0, 0.5f, 0}, ShapeType::Box, {0.5f, 0.5f, 0.5f}, 1.f), b = s.add({0, 1.5f, 0}, ShapeType::Box, {0.5f, 0.5f, 0.5f}, 1.f);
  s.run(4.f);
  CHECK(std::fabs(s.t[a].position.y - 0.5f) < 0.05f); CHECK(std::fabs(s.t[b].position.y - 1.5f) < 0.08f);
  CHECK(std::fabs(s.t[b].position.x) < 0.1f && std::fabs(s.t[b].position.z) < 0.1f);
}

TEST(newtoon_friction_stops_a_sliding_box) {
  Sim s; s.add({0, -0.5f, 0}, ShapeType::Box, {50, 0.5f, 50}, 0.f);
  const size_t b = s.add({0, 0.5f, 0}, ShapeType::Box, {0.5f, 0.5f, 0.5f}, 1.f); s.r[b].velocity = {6.f, 0, 0};
  s.run(3.f);
  CHECK(std::fabs(s.r[b].velocity.x) < 0.1f); CHECK(s.t[b].position.x > 1.f && s.t[b].position.x < 6.f);
}

TEST(newtoon_events_enter_stay_exit_and_triggers) {
  Sim s; const size_t g = s.add({0, -0.5f, 0}, ShapeType::Box, {20, 0.5f, 20}, 0.f); (void)g;
  const size_t ball = s.add({0, 1.5f, 0}, ShapeType::Sphere, {}, 1.f);
  const size_t zone = s.add({5, 0.5f, 0}, ShapeType::Box, {1, 1, 1}, 0.f); s.c[zone].isTrigger = true;
  bool enter = false, stay = false;
  for (int i = 0; i < 120; ++i) { s.w.Simulate(s.t, s.r, s.c, 1.f / 60.f); for (auto& e : s.w.GetCollisionEvents()) if (e.entityA == 0 || e.entityB == 0) { if (e.type == CollisionType::Enter) enter = true; if (e.type == CollisionType::Stay) stay = true; } s.w.ClearEvents(); }
  CHECK(enter); CHECK(stay);
  s.t[ball].position = {5, 1.2f, 0}; s.r[ball].velocity = {}; s.r[ball].WakeUp();
  bool trigEnter = false, exit = false; (void)exit;
  for (int i = 0; i < 30; ++i) { s.w.Simulate(s.t, s.r, s.c, 1.f / 60.f); for (auto& e : s.w.GetCollisionEvents()) if (e.trigger && e.type == CollisionType::Enter) trigEnter = true; s.w.ClearEvents(); }
  CHECK(trigEnter);
  CHECK(s.t[ball].position.y < 5.f && s.t[ball].position.y > 0.4f); // gatilho nao empurra: a bola cai atraves dele ate o chao
}

TEST(newtoon_raycast_hits_nearest_shape_and_reports_normal) {
  Sim s; const size_t a = s.add({0, 0, -5}, ShapeType::Box, {1, 1, 1}, 0.f), b = s.add({0, 0, -10}, ShapeType::Sphere, {}, 0.f, 1.f);
  s.w.Rebuild(s.t, s.r, s.c);
  auto h = s.w.Raycast({0, 0, 0}, {0, 0, -1}, 100.f);
  CHECK(h.hit && h.entity == a && std::fabs(h.distance - 4.f) < 1e-3f && h.normal.z > 0.99f);
  CHECK(!s.w.Raycast({0, 0, 0}, {0, 0, -1}, 3.f).hit);                                   // alcance curto
  CHECK(s.w.Raycast({0, 0, 0}, {0, 0, -1}, 100.f, a).entity == b);                      // ignora o primeiro
  CHECK(!s.w.Raycast({5, 5, 0}, {0, 0, -1}, 100.f).hit);
}

TEST(newtoon_ccd_prevents_tunneling) {
  Sim s; s.add({0, -0.1f, 0}, ShapeType::Box, {20, 0.1f, 20}, 0.f);
  const size_t b = s.add({0, 5, 0}, ShapeType::Sphere, {}, 1.f, 0.2f); s.r[b].velocity = {0, -400.f, 0}; s.r[b].useCCD = true;
  s.run(0.2f);
  CHECK(s.t[b].position.y > -0.5f);
  Sim s2; s2.add({0, -0.1f, 0}, ShapeType::Box, {20, 0.1f, 20}, 0.f);
  const size_t b2 = s2.add({0, 5, 0}, ShapeType::Sphere, {}, 1.f, 0.2f); s2.r[b2].velocity = {0, -400.f, 0};
  s2.run(0.2f); CHECK(s2.t[b2].position.y < -0.5f); // sem CCD atravessa (prova que o teste e valido)
}

TEST(newtoon_many_bodies_is_deterministic_and_fast) {
  auto make = [](Sim& s) { s.add({0, -0.5f, 0}, ShapeType::Box, {50, 0.5f, 50}, 0.f);
    for (int i = 0; i < 400; ++i) s.add({static_cast<float>(i % 20) * 1.3f - 13.f, 1.f + static_cast<float>(i / 20) * 1.2f, static_cast<float>((i * 7) % 11) * 0.9f - 5.f}, i % 2 ? ShapeType::Box : ShapeType::Sphere, {0.5f, 0.5f, 0.5f}, 1.f); };
  Sim a, b; make(a); make(b); a.run(2.f); b.run(2.f);
  bool same = true; for (size_t i = 0; i < a.t.size(); ++i) if (a.t[i].position.x != b.t[i].position.x || a.t[i].position.y != b.t[i].position.y) same = false;
  CHECK(same);
  bool sane = true; for (auto& t : a.t) if (!(t.position.y > -1.f && t.position.y < 40.f)) sane = false; CHECK(sane);
}

TEST(newtoon_verlet_chain_hangs_from_fixed_bone) {
  std::vector<DynamicBone> bones(4); std::vector<BoneConstraint> cs;
  for (int i = 0; i < 4; ++i) { bones[static_cast<size_t>(i)].position = bones[static_cast<size_t>(i)].previousPosition = {static_cast<float>(i), 0, 0}; bones[static_cast<size_t>(i)].mass = i == 0 ? 0.f : 1.f; if (i) cs.push_back({static_cast<Entity>(i - 1), static_cast<Entity>(i), 1.f, 1.f}); }
  SkeletalPhysicsSolver sv; sv.iterations = 8; for (int i = 0; i < 600; ++i) sv.Step(bones, cs, {}, 1.f / 60.f);
  CHECK(bones[0].position.x == 0.f); CHECK(bones[3].position.y < -2.0f);
  const float L = (bones[3].position - bones[2].position).Magnitude(); CHECK(std::fabs(L - 1.f) < 0.15f);
}
