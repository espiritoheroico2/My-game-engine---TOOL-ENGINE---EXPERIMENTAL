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
#include <new>
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
  play.ents.push_back(e); play.tf.push_back(nt); play.rb.push_back(r); play.col.push_back(c);
  play.dead.push_back(0); play.culled.push_back(0);
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
    try {
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
      // As esferas projetam sombra (como em qualquer objeto da cena): o teste mede o custo real, com sombras ligadas.
      // Para medir sem sombras, desmarque "Cast shadows" no MeshRenderer ou use a cena de --instances.
      mr.castShadow = true;
      mr.color[0] = 0.3f + ecs_detail::frand(sp->seed) * 0.7f; mr.color[1] = 0.3f + ecs_detail::frand(sp->seed) * 0.7f; mr.color[2] = 0.3f + ecs_detail::frand(sp->seed) * 0.7f;
      mr.roughness = 0.4f;
      w.add<MeshRenderer>(s, mr);
      w.add<Parent>(s, Parent{e});
      if (sp->physics) {
        // "test ecsf": corpo Newtoon (esfera) que cai e colide.
        PhysicsComp ph; ph.collider = 2; ph.mass = 1.f;
        w.add<PhysicsComp>(s, ph);
        any = pushRuntimeBody(s) || any;
      } else {
        // "test ecs": sem fisica. A instancia oscila em torno do ponto de nascimento (MoverComp, tickMovers).
        MoverComp mv;
        mv.base[0] = tr.pos[0]; mv.base[1] = tr.pos[1]; mv.base[2] = tr.pos[2];
        mv.amp = 2.f + ecs_detail::frand(sp->seed) * 6.f;
        mv.freq = 0.2f + ecs_detail::frand(sp->seed) * 0.6f;
        mv.phase = ecs_detail::frand(sp->seed) * 6.2831853f;
        w.add<MoverComp>(s, mv);
      }
    }
    } catch (const std::bad_alloc&) {
      // Memoria acabou: para o teste aqui, mantendo o editor aberto (o Stop desfaz as esferas).
      sp->armed = false; sp->logged = true;
      tool::logWarn("ECS test: out of memory after " + std::to_string(sp->spawned) + " spheres. Stopped the test; press Stop to clear them.");
      continue;
    }
    if (any) play.built = true;   // Simulate prepara a BVH no proximo passo; nao reconstruir uma vez extra por quadro
    sp->spawned += n;
    if (sp->spawned >= sp->total && !sp->logged) {
      sp->logged = true; sp->armed = false;
      const double sec = ecs_detail::nowSec() - sp->startedAt;
      tool::logInfo("ECS test: " + std::to_string(sp->spawned) + " spheres in " + std::to_string(static_cast<int>(sec * 1000.0)) + " ms. Bodies: " +
                    std::to_string(play.ents.size()) + ". Watch the frame time in the Profiler.");
    }
  }
}

// Movimento do "test ecs" (sem fisica): avanca o tempo de cada MoverComp e recoloca o Transform sobre o caminho.
// Roda so no Play (o Stop restaura a cena e os componentes somem junto com as instancias).
inline void Editor::tickMovers(float dt) {
  if (!playing) return;
  scene.world().each<MoverComp, Transform>([&](tool::Entity, MoverComp& m, Transform& tr) {
    m.t += dt;
    const float a = m.t * m.freq + m.phase;   // fase do caminho
    tr.pos[0] = m.base[0] + m.amp * std::sin(a);
    tr.pos[1] = m.base[1] + 0.5f * m.amp * std::sin(2.f * a);
    tr.pos[2] = m.base[2] + m.amp * std::cos(a);
  });
}

inline void Editor::startEcsTest(long long n, bool physics) {
  // Medido em 2026-11-18: ~7 KB de memoria por esfera (50 mil esferas = ~370 MB). Avisa antes de comecar.
  const double estimateMB = static_cast<double>(n) * 7.0 / 1024.0;
  if (estimateMB > 1024.0) tool::logWarn("test ECS: " + std::to_string(n) + " spheres need about " + std::to_string(static_cast<int>(estimateMB)) + " MB of RAM. If the editor closes, use a smaller N.");
  auto& w = scene.world();
  tool::Entity call = findByName("ECScall");
  if (!w.alive(call)) call = make("ECScall");
  if (w.has<SpawnerComp>(call)) w.remove<SpawnerComp>(call);
  SpawnerComp sp;
  sp.total = n;
  sp.physics = physics;
  sp.perFrame = static_cast<int>(std::max<long long>(1, (n + 239) / 240));   // ~4 s no total, a 60 fps
  sp.armed = true;
  sp.seed = 2463534242u ^ static_cast<uint32_t>(n);
  w.add<SpawnerComp>(call, sp);
  if (!playing) {
    startPlay();   // o teste roda no Play; o Stop desfaz as esferas
  } else {
    tool::logInfo("ECS test armed while playing.");
  }
  play.ents.reserve(play.ents.size() + static_cast<size_t>(n));
  play.tf.reserve(play.tf.size() + static_cast<size_t>(n));
  play.rb.reserve(play.rb.size() + static_cast<size_t>(n));
  play.col.reserve(play.col.size() + static_cast<size_t>(n));
  play.dead.reserve(play.dead.size() + static_cast<size_t>(n));
  play.culled.reserve(play.culled.size() + static_cast<size_t>(n));
  play.indexOf.reserve(play.indexOf.size() + static_cast<size_t>(n));
}

// --instances N (benchmark de render, sem fisica): N entidades planas (sem Parent) com Name, Transform e MeshRenderer,
// numa grade com jitter (~4 m), centrada na origem, com semente fixa. `shape`: 0 = cubo, 1 = esfera. Cria tudo de uma vez
// e devolve quantas foram criadas (menos que N se faltar memoria).
inline long long createInstanceScene(Editor& ed, long long n, int shape) {
  // ~0,7 KB por entidade (o MeshRenderer carrega dois caminhos de 160 bytes). Avisa antes de comecar.
  const double estimateMB = static_cast<double>(n) * 0.7 / 1024.0;
  if (estimateMB > 1024.0) tool::logWarn("--instances: " + std::to_string(n) + " entities need about " + std::to_string(static_cast<int>(estimateMB)) + " MB of RAM. If the editor closes, use a smaller N.");
  auto& w = ed.scene.world();
  const long long side = static_cast<long long>(std::ceil(std::sqrt(static_cast<double>(n))));
  const float spacing = 4.f, half = static_cast<float>(side - 1) * 0.5f;
  uint32_t seed = 2463534242u ^ static_cast<uint32_t>(n);
  long long made = 0;
  const double t0 = ecs_detail::nowSec();
  try {
    for (long long i = 0; i < n; ++i) {
      const tool::Entity e = w.create();
      Name nm; std::snprintf(nm.text, sizeof nm.text, "inst"); w.add<Name>(e, nm);
      Transform tr;
      const float sc = 0.5f + ecs_detail::frand(seed);   // 0,5 a 1,5
      const float jx = (ecs_detail::frand(seed) - 0.5f) * 2.f, jz = (ecs_detail::frand(seed) - 0.5f) * 2.f;
      tr.pos[0] = (static_cast<float>(i % side) - half) * spacing + jx;
      tr.pos[1] = 0.5f * sc;   // apoiado no chao (malha de 1 m centrada)
      tr.pos[2] = (static_cast<float>(i / side) - half) * spacing + jz;
      tr.rot[1] = ecs_detail::frand(seed) * 360.f;   // yaw em graus
      tr.scale[0] = tr.scale[1] = tr.scale[2] = sc;
      w.add<Transform>(e, tr);
      MeshRenderer mr; mr.shape = shape; mr.castShadow = false;
      mr.color[0] = 0.3f + ecs_detail::frand(seed) * 0.7f; mr.color[1] = 0.3f + ecs_detail::frand(seed) * 0.7f; mr.color[2] = 0.3f + ecs_detail::frand(seed) * 0.7f;
      mr.roughness = 0.5f;
      w.add<MeshRenderer>(e, mr);
      ++made;
    }
  } catch (const std::bad_alloc&) {
    tool::logWarn("--instances: out of memory after " + std::to_string(made) + " of " + std::to_string(n) + " entities.");
  }
  const double ms = (ecs_detail::nowSec() - t0) * 1000.0;
  tool::logInfo("--instances: " + std::to_string(made) + " entities (" + (shape == 1 ? "spheres" : "cubes") + ", grid " + std::to_string(side) + "x" + std::to_string(side) + ") created in " + std::to_string(static_cast<int>(ms)) + " ms.");
  std::printf("ECS bench: created %lld instances in %.0f ms\n", made, ms);
  return made;
}

// Camera fixa do benchmark de render. `k` escala a distancia com o lado da grade (1 = grade de ~100 mil, lado ~316).
// Com `orbit`, o olho gira em torno do centro 0,5 grau por QUADRO (reproduzivel, nao depende do relogio).
inline tool::Camera benchCamera(float k, int frameIndex, bool orbit) {
  tool::Camera c;
  const float ang = orbit ? static_cast<float>(frameIndex) * 0.5f * 3.14159265f / 180.f : 0.f;
  const float r = 120.f * k, h = 60.f * k;
  c.eye = {std::sin(ang) * r, h, -std::cos(ang) * r};
  c.target = {0.f, 0.f, 0.f};
  c.up = {0.f, 1.f, 0.f};
  c.nearZ = std::max(0.1f, 0.1f * k);
  c.farZ = std::max(100.f, 6.f * r);
  return c;
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
    tool::logInfo("test ecs: N   -> N instancias SEM fisica, oscilando pelo espaco (filhas de ECScall; padrao 10000, maximo 1000000)");
    tool::logInfo("test ecsf: N  -> N esferas COM fisica, caindo e colidindo (filhas de ECScall; padrao 10000, maximo 1000000)");
    tool::logInfo("clear         -> limpa o Console");
    tool::logInfo("scripts       -> verifica os scripts de todos os objetos da cena (erros e avisos com linha)");
    return;
  }
  if (c == "clear") { ed.console.clear(); ++ed.consoleSerial; return; }
  if (c == "scripts") { ed.checkAllScripts(); return; }
  // "test ecsf: N" = com fisica (esferas caindo); "test ecs: N" = sem fisica (instancias movendo no espaco).
  const bool withPhysics = c.rfind("test ecsf", 0) == 0;
  if (withPhysics || c.rfind("test ecs", 0) == 0) {
    size_t i = withPhysics ? 9 : 8;
    const std::string label = withPhysics ? "test ecsf" : "test ecs";
    while (i < c.size() && (std::isspace(static_cast<unsigned char>(c[i])) || c[i] == ':')) ++i;
    long long n = 10000;
    if (i < c.size()) {
      if (!std::isdigit(static_cast<unsigned char>(c[i]))) { tool::logWarn(label + ": use a number, e.g. '" + label + ": 10000'"); return; }
      n = 0; for (; i < c.size() && std::isdigit(static_cast<unsigned char>(c[i])); ++i) n = std::min<long long>(n * 10 + (c[i] - '0'), 2000000);
    }
    if (n < 1) n = 1;
    if (n > 1000000) { tool::logWarn(label + ": maximum is 1000000; using 1000000."); n = 1000000; }
    ed.startEcsTest(n, withPhysics);
    return;
  }
  tool::logWarn("Unknown command: '" + raw + "'. Type 'help'.");
}

} // namespace editor
