// ToolEngine Editor - o que acontece a cada quadro do modo Play: Input Map -> Tools -> fisica Newtoon -> Transforms -> eventos.
// (Metodos de `Editor`; ficam aqui so para manter editor_state.hpp legivel.)
//
// ORDEM DO QUADRO DE PLAY
//   1. le teclado/gamepads (InputSampler) e atualiza o InputSystem com o Input Map do projeto;
//   2. roda os Tools de cada objeto (Jump, Move, Follow, Spin): eles mexem em velocidades/impulsos, nao em posicoes de corpos dinamicos;
//   3. Newtoon::PhysicsWorld::Simulate (passo fixo interno); gravidade/iteracoes vem de Settings > Physics;
//   4. copia posicao/rotacao dos corpos dinamicos de volta para o Transform dos objetos;
//   5. trata eventos de colisao (Tool OnCollision) e apaga objetos pedidos; ao parar o Play a cena volta ao estado de antes.
#pragma once
#include "editor_state.hpp"

namespace editor {

inline Newtoon::Collider Editor::colliderFor(tool::Entity e) {
  Newtoon::Collider c; auto& w = scene.world();
  const Transform* t = w.get<Transform>(e); const PhysicsComp* p = w.get<PhysicsComp>(e); const MeshRenderer* m = w.get<MeshRenderer>(e);
  const float sx = t ? std::fabs(t->scale[0]) : 1.f, sy = t ? std::fabs(t->scale[1]) : 1.f, sz = t ? std::fabs(t->scale[2]) : 1.f;
  int kind = p ? p->collider : 0;
  if (kind == 0) kind = (m && m->shape == 1) ? 2 : 1;       // automatico: esfera -> esfera, o resto -> caixa
  if (kind == 2) { c.type = Newtoon::ShapeType::Sphere; c.radius = 0.5f * std::max({sx, sy, sz, 0.01f}); return c; }
  c.type = Newtoon::ShapeType::Box; c.extents = {0.5f * sx, 0.5f * sy, 0.5f * sz};
  if (m && m->shape == 2) { c.extents = {0.5f * sx, 0.25f, 0.5f * sz}; c.offset = {0.f, -0.25f, 0.f}; } // plano: laje fina cuja face de cima e o plano
  else if (m && m->shape == 3) if (const tool::Mesh* mesh = meshOf(*m); mesh && !mesh->pos.empty()) { // modelo: caixa envolvente da malha
    tool::Vec3 lo = mesh->pos[0], hi = lo;
    for (auto& v : mesh->pos) { lo = {std::min(lo.x, v.x), std::min(lo.y, v.y), std::min(lo.z, v.z)}; hi = {std::max(hi.x, v.x), std::max(hi.y, v.y), std::max(hi.z, v.z)}; }
    c.extents = {0.5f * (hi.x - lo.x) * sx, 0.5f * (hi.y - lo.y) * sy, 0.5f * (hi.z - lo.z) * sz};
    c.offset = {0.5f * (hi.x + lo.x) * sx, 0.5f * (hi.y + lo.y) * sy, 0.5f * (hi.z + lo.z) * sz};
  }
  c.extents.x = std::max(c.extents.x, 0.005f); c.extents.y = std::max(c.extents.y, 0.005f); c.extents.z = std::max(c.extents.z, 0.005f);
  return c;
}

inline void Editor::startPhysics() {
  play.built = false;
  auto& w = scene.world();
  std::vector<std::pair<std::pair<float, float>, uint16_t>> mats; // (atrito, quique) -> id
  play.world.materials.assign(1, Newtoon::PhysicsMaterial{});
  const PhysicsSettings& ps = sceneInfo.physics;
  play.world.gravity = {ps.gravity[0], ps.gravity[1], ps.gravity[2]};
  play.world.solverIterations = ps.solverIterations; play.world.fixedDt = 1.f / static_cast<float>(ps.stepHz);
  w.each<PhysicsComp, Transform>([&](tool::Entity e, PhysicsComp& p, Transform& t) {
    if (w.has<Inactive>(e)) return;
    Newtoon::Transform nt; nt.position = {t.pos[0], t.pos[1], t.pos[2]}; nt.rotation = eulerToQuat(t.rot);
    Newtoon::Collider c = colliderFor(e); c.isTrigger = p.trigger;
    uint16_t id = 0; // material compartilhado por (atrito, quique)
    for (auto& m : mats) if (m.first.first == p.friction && m.first.second == p.bounciness) id = m.second;
    if (id == 0) { Newtoon::PhysicsMaterial pm; pm.friction = p.friction; pm.bounciness = p.bounciness; play.world.materials.push_back(pm); id = static_cast<uint16_t>(play.world.materials.size() - 1); mats.push_back({{p.friction, p.bounciness}, id}); }
    c.materialId = id;
    Newtoon::Rigidbody r;
    if (p.body == 1) Newtoon::SetMassFromShape(r, c, p.mass); else { r.inverseMass = 0.f; r.inverseInertiaLocal = {0, 0, 0}; r.isKinematic = (p.body == 2); }
    r.useGravity = p.useGravity; r.gravityScale = p.gravityScale; r.linearDamping = p.linearDamping; r.angularDamping = p.angularDamping;
    r.freezeRotation = p.freezeRotation; r.useCCD = p.ccd; r.overrideGravity = p.overrideGravity; r.gravity = {p.gravity[0], p.gravity[1], p.gravity[2]};
    play.indexOf[e.index] = static_cast<uint32_t>(play.ents.size());
    play.ents.push_back(e); play.tf.push_back(nt); play.rb.push_back(r); play.col.push_back(c);
  });
  play.dead.assign(play.ents.size(), 0);
  if (!play.ents.empty() && ps.enabled) { play.world.Rebuild(play.tf, play.rb, play.col); play.built = true; }
}

inline void Editor::stepPlay(float dt) {
  auto& w = scene.world();
  dt = std::clamp(dt, 0.f, 0.1f);
  // 1) entrada
  if (!injectInput) sampler.sample(rawInput, !uiTyping);   // injectInput: os testes escrevem rawInput direto
  input.update(rawInput);
  auto schemeOf = [&](int player) { return static_cast<size_t>(std::clamp(player, 1, 8) - 1); };
  auto bodyOf = [&](tool::Entity e) -> int { auto it = play.indexOf.find(e.index); return it == play.indexOf.end() || play.dead[it->second] ? -1 : static_cast<int>(it->second); };
  // 2) Tools
  const auto cams = principalCameras();
  w.each<ToolsComp, Transform>([&](tool::Entity e, ToolsComp& tl, Transform& t) {
    if (w.has<Inactive>(e)) return;
    const int bi = bodyOf(e); Newtoon::Rigidbody* rb = bi >= 0 ? &play.rb[static_cast<size_t>(bi)] : nullptr;
    const bool dyn = rb && rb->IsDynamic();
    for (int i = 0; i < tl.count; ++i) {
      ToolItem& it = tl.items[i]; if (!it.enabled) continue;
      switch (static_cast<ToolKind>(it.kind)) {
        case ToolKind::Jump: { // a = altura do pulo (m), b = 1 permite pular no ar
          if (!dyn || !inputMap.pressed(input, schemeOf(it.player), it.action)) break;
          if (it.b == 0.f && !rb->grounded) break;
          const Newtoon::Vec3 g = rb->overrideGravity ? rb->gravity : play.world.gravity; const float gm = std::max(0.1f, g.Magnitude());
          const Newtoon::Vec3 up = gm > 0.01f ? Newtoon::Vec3{-g.x / gm, -g.y / gm, -g.z / gm} : Newtoon::Vec3{0, 1, 0};
          const float h = it.a > 0.f ? it.a : 1.5f, v = std::sqrt(2.f * gm * h);
          const float cancel = Newtoon::Dot(rb->velocity, up); if (cancel < 0.f) rb->velocity -= up * cancel; // pulo sempre sai com a mesma altura
          rb->AddImpulse(up * (v / rb->inverseMass)); rb->grounded = false;
        } break;
        case ToolKind::Move: { // action = eixo X, target = eixo Y (frente), a = velocidade (m/s), b = 1 vira para a direcao, mode 1 = relativo a camera
          const float ax = inputMap.axis(input, schemeOf(it.player), it.action), ay = inputMap.axis(input, schemeOf(it.player), it.target);
          float mx = ax, mz = -ay; const float len = std::sqrt(mx * mx + mz * mz); if (len > 1.f) { mx /= len; mz /= len; }
          if (it.mode == 1 && !cams.empty()) { if (auto* ct = w.get<Transform>(cams[0])) { const float camYaw = ct->rot[1] * kDeg2Rad, cs = std::cos(camYaw), sn = std::sin(camYaw); const float rx = mx * cs + mz * sn, rz = -mx * sn + mz * cs; mx = rx; mz = rz; } }
          const float speed = it.a > 0.f ? it.a : 5.f;
          if (dyn) { const float k = std::min(1.f, 12.f * dt); rb->velocity.x += (mx * speed - rb->velocity.x) * k; rb->velocity.z += (mz * speed - rb->velocity.z) * k; if (len > 0.01f) rb->WakeUp(); }
          else { t.pos[0] += mx * speed * dt; t.pos[2] += mz * speed * dt; if (bi >= 0) { play.tf[static_cast<size_t>(bi)].position = {t.pos[0], t.pos[1], t.pos[2]}; } }
          if (it.b != 0.f && len > 0.1f) t.rot[1] = std::atan2(-mx, -mz) / kDeg2Rad;
          if (it.b != 0.f && dyn && len > 0.1f) { play.tf[static_cast<size_t>(bi)].rotation = eulerToQuat(t.rot); rb->angularVelocity = {}; }
        } break;
        case ToolKind::FollowTarget: { // target = nome do objeto; (a,b,c) = deslocamento; d = suavizacao (0 = grude); mode 1 = tambem olha para o alvo
          tool::Entity tgt; w.each<Name>([&](tool::Entity o, Name& n) { if (!w.alive(tgt) && o != e && std::strncmp(n.text, it.target, sizeof n.text) == 0) tgt = o; });
          const Transform* tt = w.alive(tgt) ? w.get<Transform>(tgt) : nullptr; if (!tt) break;
          const float want[3] = {tt->pos[0] + it.a, tt->pos[1] + it.b, tt->pos[2] + it.c};
          const float k = it.d > 0.f ? 1.f - std::exp(-it.d * dt) : 1.f;
          for (int a = 0; a < 3; ++a) t.pos[a] += (want[a] - t.pos[a]) * k;
          if (it.mode == 1) { const tool::Vec3 d = tool::normalize(tool::Vec3{tt->pos[0] - t.pos[0], tt->pos[1] - t.pos[1], tt->pos[2] - t.pos[2]});
            t.rot[0] = std::asin(std::clamp(d.y, -1.f, 1.f)) / kDeg2Rad; t.rot[1] = std::atan2(-d.x, -d.z) / kDeg2Rad; t.rot[2] = 0.f; }
          if (bi >= 0 && !dyn) { play.tf[static_cast<size_t>(bi)].position = {t.pos[0], t.pos[1], t.pos[2]}; play.tf[static_cast<size_t>(bi)].rotation = eulerToQuat(t.rot); }
        } break;
        case ToolKind::Spin: { // (a,b,c) = graus por segundo em X/Y/Z
          if (dyn) { rb->angularVelocity = {it.a * kDeg2Rad, it.b * kDeg2Rad, it.c * kDeg2Rad}; rb->WakeUp(); }
          else { t.rot[0] += it.a * dt; t.rot[1] += it.b * dt; t.rot[2] += it.c * dt; if (bi >= 0) play.tf[static_cast<size_t>(bi)].rotation = eulerToQuat(t.rot); }
        } break;
        default: break; // OnCollision (abaixo, apos a fisica) e ScriptLib (ainda sem VM)
      }
    }
  });
  // 3) fisica
  if (play.built) {
    for (size_t i = 0; i < play.ents.size(); ++i) { // corpos estaticos/cinematicos seguem o Transform do editor (gizmo, tools, narrativa)
      if (play.dead[i] || !w.alive(play.ents[i]) || play.rb[i].IsDynamic()) continue;
      if (const Transform* t = w.get<Transform>(play.ents[i])) { play.tf[i].position = {t->pos[0], t->pos[1], t->pos[2]}; play.tf[i].rotation = eulerToQuat(t->rot); }
    }
    play.world.Simulate(play.tf, play.rb, play.col, dt);
    // 4) sincroniza
    for (size_t i = 0; i < play.ents.size(); ++i) {
      if (play.dead[i] || !play.rb[i].IsDynamic() || !w.alive(play.ents[i])) continue;
      Transform* t = w.get<Transform>(play.ents[i]); if (!t) continue;
      t->pos[0] = play.tf[i].position.x; t->pos[1] = play.tf[i].position.y; t->pos[2] = play.tf[i].position.z; quatToEuler(play.tf[i].rotation, t->rot);
    }
    // 5) eventos de colisao -> Tool OnCollision
    for (const auto& ev : play.world.GetCollisionEvents()) {
      if (ev.type != Newtoon::CollisionType::Enter || ev.entityA >= play.ents.size() || ev.entityB >= play.ents.size()) continue;
      for (int side = 0; side < 2; ++side) {
        const size_t self = side ? ev.entityB : ev.entityA, other = side ? ev.entityA : ev.entityB;
        const tool::Entity es = play.ents[self], eo = play.ents[other]; if (!w.alive(es) || !w.alive(eo)) continue;
        auto* tl = w.get<ToolsComp>(es); if (!tl) continue;
        for (int i = 0; i < tl->count; ++i) {
          ToolItem& it = tl->items[i]; if (!it.enabled || it.kind != static_cast<int>(ToolKind::OnCollision)) continue;
          if (it.target[0] && !(scene.tags().find(it.target) != tool::kInvalidIndex && w.tagMask(eo).test(scene.tags().find(it.target)))) continue; // filtro por tag
          const Name* ns = w.get<Name>(es); const Name* no = w.get<Name>(eo);
          if (it.mode == 0) tool::logInfo(std::string("Collision: ") + (ns ? ns->text : "?") + " hit " + (no ? no->text : "?"));
          else if (it.mode == 1) play.destroyQueue.push_back(eo);
          else if (it.mode == 2) play.destroyQueue.push_back(es);
          else if (it.mode == 3 && play.rb[self].IsDynamic()) play.rb[self].AddImpulse({0.f, (it.a > 0.f ? it.a : 5.f) / play.rb[self].inverseMass, 0.f}); // quica: a = velocidade vertical (m/s)
        }
      }
    }
    play.world.ClearEvents();
  }
  // apaga o que foi pedido (os corpos ficam "mortos": longe do mundo e sem colisao)
  for (tool::Entity d : play.destroyQueue) {
    if (!w.alive(d)) continue;
    const int bi = bodyOf(d);
    if (bi >= 0) { auto& r = play.rb[static_cast<size_t>(bi)]; r = Newtoon::Rigidbody{}; r.inverseMass = 0.f; play.tf[static_cast<size_t>(bi)].position = {1e7f, 1e7f, 1e7f}; play.col[static_cast<size_t>(bi)].isTrigger = true; play.dead[static_cast<size_t>(bi)] = 1; }
    destroy(d);
  }
  play.destroyQueue.clear();
}

} // namespace editor
