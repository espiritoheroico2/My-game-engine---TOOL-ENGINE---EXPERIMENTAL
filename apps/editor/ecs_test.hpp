// ecs_test.hpp - Teste de eficiencia do ECS pelo Console: "test ECS: N".
//
// O comando cria (ou reaproveita) o objeto "ECScall" e arma o SpawnerComp dele. Durante o Play, o sistema cria as
// esferas aos poucos (perFrame por quadro, ~4 s no total), no ceu, como FILHAS do ECScall. Cada esfera tem
// Transform, MeshRenderer, PhysicsComp (Newtoon, colisor de esfera) e Parent. O Stop desfaz tudo (snapshot).
// Use "test ECS: 100000" para mais; o limite e 1.000.000 de esferas (a memoria cresce ~1 KB por esfera).
#pragma once

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace editor {

namespace ecs_detail {
inline uint32_t lcg(uint32_t& s) { s = s * 1664525u + 1013904223u; return s; }
inline float frand(uint32_t& s) { return static_cast<float>(lcg(s) >> 8) * (1.f / 16777216.f); }   // [0,1)
inline double nowSec() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
} // namespace ecs_detail

inline bool Editor::pushRuntimeBody(tool::Entity e) {
  auto& w = scene.world();
  const PhysicsComp* p = w.get<PhysicsComp>(e); const Transform* t = w.get<Transform>(e);
  if (!p || !t || w.has<Inactive>(e) || !sceneInfo.physics.enabled) return false;
  const tool::Mat4 wm = worldMat(e);
  Newtoon::Transform nt; nt.position = {wm.m[3][0], wm.m[3][1], wm.m[3][2]}; nt.rotation = eulerToQuat(t->rot);
  Newtoon::Collider c = colliderFor(e); c.isTrigger = p->trigger;
  c.materialId = 0;   // material padrao (o teste nao precisa de material por objeto)
  Newtoon::Rigidbody r;
  if (p->body == 1) Newtoon::SetMassFromShape(r, c, p->mass); else { r.inverseMass = 0.f; r.inverseInertiaLocal = {0, 0, 0}; r.isKinematic = (p->body == 2); }
  r.useGravity = p->useGravity; r.gravityScale = p->gravityScale; r.linearDamping = p->linearDamping; r.angularDamping = p->angularDamping;
  r.freezeRotation = p->freezeRotation; r.useCCD = p->ccd; r.overrideGravity = p->overrideGravity; r.gravity = {p->gravity[0], p->gravity[1], p->gravity[2]};
  play.indexOf[e.index] = static_cast<uint32_t>(play.ents.size());
  play.ents.push_back(e); play.tf.push_back(nt); play.rb.push_back(r); play.col.push_back(c); play.dead.push_back(0);
  return true;
}

inline void Editor::tickSpawners(float) {
  if (!playing) return;
  auto& w = scene.world();
  std::vector<tool::Entity> spawners;
  w.each<SpawnerComp>([&](tool::Entity e, SpawnerComp& sp) { if (sp.armed) spawners.push_back(e); });
  for (tool::Entity e : spawners) {
    SpawnerComp* sp = w.get<SpawnerComp>(e);
    if (!sp || !sp->armed) continue;
    if (sp->startedAt < 0) { sp->startedAt = ecs_detail::nowSec(); tool::logInfo("ECS test: creating " + std::to_string(sp->total) + " spheres, " + std::to_string(sp->perFrame) + " per frame"); }
    const long long n = std::min<long long>(sp->perFrame, sp->total - sp->spawned);
    const tool::Mat4 pm = worldMat(e);
    bool any = false;
    for (long long i = 0; i < n; ++i) {
      const tool::Entity s = w.create();
      Name nm; std::snprintf(nm.text, sizeof nm.text, "sphere"); w.add<Name>(s, nm);
      Transform tr;
      const float x = (ecs_detail::frand(sp->seed) - 0.5f) * sp->area, z = (ecs_detail::frand(sp->seed) - 0.5f) * sp->area;
      const float y = sp->height + ecs_detail::frand(sp->seed) * 4.f;   // cada esfera nasce em uma altura um pouco diferente
      const float sc = 0.3f + ecs_detail::frand(sp->seed) * 0.3f;
      // posicao no mundo -> local do ECScall (o ECScall nao gira; a escala dele e 1 por padrao)
      tr.pos[0] = x - pm.m[3][0]; tr.pos[1] = y - pm.m[3][1]; tr.pos[2] = z - pm.m[3][2];
      tr.scale[0] = tr.scale[1] = tr.scale[2] = sc;
      w.add<Transform>(s, tr);
      MeshRenderer mr; mr.shape = 1;
      mr.color[0] = 0.3f + ecs_detail::frand(sp->seed) * 0.7f; mr.color[1] = 0.3f + ecs_detail::frand(sp->seed) * 0.7f; mr.color[2] = 0.3f + ecs_detail::frand(sp->seed) * 0.7f;
      mr.roughness = 0.4f;
      w.add<MeshRenderer>(s, mr);
      PhysicsComp ph; ph.collider = 2; ph.mass = 1.f;
      w.add<PhysicsComp>(s, ph);
      w.add<Parent>(s, Parent{e});
      any = pushRuntimeBody(s) || any;
    }
    if (any) { play.world.Rebuild(play.tf, play.rb, play.col); play.built = true; }   // um Rebuild por quadro, nao um por esfera
    sp->spawned += n;
    if (sp->spawned >= sp->total && !sp->logged) {
      sp->logged = true; sp->armed = false;
      const double sec = ecs_detail::nowSec() - sp->startedAt;
      tool::logInfo("ECS test: " + std::to_string(sp->spawned) + " spheres in " + std::to_string(static_cast<int>(sec * 1000.0)) + " ms. Bodies: " +
                    std::to_string(play.ents.size()) + ". Watch the frame time in the Profiler.");
    }
  }
}

inline void Editor::startEcsTest(long long n) {
  auto& w = scene.world();
  tool::Entity call = findByName("ECScall");
  if (!w.alive(call)) call = make("ECScall");
  if (w.has<SpawnerComp>(call)) w.remove<SpawnerComp>(call);
  SpawnerComp sp;
  sp.total = n;
  sp.perFrame = static_cast<int>(std::max<long long>(1, (n + 239) / 240));   // ~4 s no total, a 60 fps
  sp.armed = true;
  sp.seed = 2463534242u ^ static_cast<uint32_t>(n);
  w.add<SpawnerComp>(call, sp);
  if (!playing) startPlay();   // o teste roda no Play; o Stop desfaz as esferas
  else tool::logInfo("ECS test armed while playing.");
}

// Comandos digitados no Console. Mantenha simples: cada comando e uma linha.
inline void runConsoleCommand(Editor& ed, const std::string& raw) {
  std::string c;
  for (char ch : raw) c.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  while (!c.empty() && std::isspace(static_cast<unsigned char>(c.back()))) c.pop_back();
  size_t lead = 0; while (lead < c.size() && std::isspace(static_cast<unsigned char>(c[lead]))) ++lead;
  c = c.substr(lead);
  if (c.empty()) return;
  tool::logInfo("> " + raw);
  if (c == "help") {
    tool::logInfo("test ECS: N   -> N esferas caem do ceu como filhas do objeto ECScall (padrao 10000, maximo 1000000)");
    tool::logInfo("clear         -> limpa o Console");
    return;
  }
  if (c == "clear") { ed.console.clear(); return; }
  if (c.rfind("test ecs", 0) == 0) {
    size_t i = 8;
    while (i < c.size() && (std::isspace(static_cast<unsigned char>(c[i])) || c[i] == ':')) ++i;
    long long n = 10000;
    if (i < c.size()) {
      if (!std::isdigit(static_cast<unsigned char>(c[i]))) { tool::logWarn("test ECS: use a number, e.g. 'test ECS: 10000'"); return; }
      n = 0; for (; i < c.size() && std::isdigit(static_cast<unsigned char>(c[i])); ++i) n = std::min<long long>(n * 10 + (c[i] - '0'), 2000000);
    }
    if (n < 1) n = 1;
    if (n > 1000000) { tool::logWarn("test ECS: maximum is 1000000; using 1000000."); n = 1000000; }
    ed.startEcsTest(n);
    return;
  }
  tool::logWarn("Unknown command: '" + raw + "'. Type 'help'.");
}

} // namespace editor
