// ToolEngine Editor - hierarquia pai/filho dos objetos.
//
// REGRAS
//   - O Transform de um objeto COM `Parent` e local (relativo ao pai); sem `Parent` e o proprio mundo.
//   - Matriz de mundo = matriz do pai * matriz local (recursivo, no maximo 32 niveis: ciclos sao cortados).
//   - Mover/girar/escalar o pai move os filhos. Apagar o pai apaga a subarvore.
//   - Ao reparentar com keepWorld, o objeto nao "pula": o local e recalculado (inversa da matriz do pai).
//   - Fisica: ao dar Play, objetos com Parent e corpo fisico sao soltos do pai mantendo o mundo (o Play e restaurado ao parar).
// Custo: objetos sem pai nao pagam nada alem de um teste `has<Parent>`.
#pragma once
#include "editor_state.hpp"

namespace editor {

// Decompoe uma matriz afim T*Ry*Rx*Rz*S em posicao / rotacao (graus, ordem do editor) / escala.
inline void decomposeTRS(const tool::Mat4& m, float pos[3], float rot[3], float scale[3]) {
  pos[0] = m.m[3][0]; pos[1] = m.m[3][1]; pos[2] = m.m[3][2];
  for (int c = 0; c < 3; ++c) { const float l = std::sqrt(m.m[c][0] * m.m[c][0] + m.m[c][1] * m.m[c][1] + m.m[c][2] * m.m[c][2]); scale[c] = l > 1e-8f ? l : 1.f; }
  auto R = [&](int row, int col) { return m.m[col][row] / scale[col]; };
  constexpr float d = 180.f / 3.14159265f;
  const float sx = std::clamp(-R(1, 2), -1.f, 1.f);
  rot[0] = std::asin(sx) * d;
  if (std::fabs(sx) < 0.99999f) { rot[1] = std::atan2(R(0, 2), R(2, 2)) * d; rot[2] = std::atan2(R(1, 0), R(1, 1)) * d; }
  else { rot[1] = std::atan2(-R(2, 0), R(0, 0)) * d; rot[2] = 0.f; }
}

// Inversa de uma matriz afim (3x3 + translacao) por cofatores.
inline tool::Mat4 inverseAffine(const tool::Mat4& a) {
  const float (*m)[4] = a.m; // m[col][row]
  const float a00 = m[0][0], a01 = m[1][0], a02 = m[2][0], a10 = m[0][1], a11 = m[1][1], a12 = m[2][1], a20 = m[0][2], a21 = m[1][2], a22 = m[2][2];
  const float c00 = a11 * a22 - a12 * a21, c01 = a12 * a20 - a10 * a22, c02 = a10 * a21 - a11 * a20;
  const float det = a00 * c00 + a01 * c01 + a02 * c02;
  tool::Mat4 r; if (std::fabs(det) < 1e-12f) return r;
  const float id = 1.f / det;
  const float i00 = c00 * id, i01 = (a02 * a21 - a01 * a22) * id, i02 = (a01 * a12 - a02 * a11) * id;
  const float i10 = c01 * id, i11 = (a00 * a22 - a02 * a20) * id, i12 = (a02 * a10 - a00 * a12) * id;
  const float i20 = c02 * id, i21 = (a01 * a20 - a00 * a21) * id, i22 = (a00 * a11 - a01 * a10) * id;
  r.m[0][0] = i00; r.m[1][0] = i01; r.m[2][0] = i02; r.m[0][1] = i10; r.m[1][1] = i11; r.m[2][1] = i12; r.m[0][2] = i20; r.m[1][2] = i21; r.m[2][2] = i22;
  const float tx = m[3][0], ty = m[3][1], tz = m[3][2];
  r.m[3][0] = -(i00 * tx + i01 * ty + i02 * tz); r.m[3][1] = -(i10 * tx + i11 * ty + i12 * tz); r.m[3][2] = -(i20 * tx + i21 * ty + i22 * tz);
  return r;
}

inline tool::Entity Editor::parentOf(tool::Entity e) {
  auto* p = scene.world().get<Parent>(e);
  return p && scene.world().alive(p->e) && p->e != e ? p->e : tool::Entity{};
}

inline tool::Mat4 Editor::worldMat(tool::Entity e) {
  auto& w = scene.world();
  tool::Mat4 m; bool first = true; tool::Entity cur = e;
  // sobe ate a raiz multiplicando: world = P_n * ... * P_1 * local
  for (int depth = 0; depth < 32 && w.alive(cur); ++depth) {
    const Transform* t = w.get<Transform>(cur);
    if (t) { m = first ? worldMatrix(*t) : worldMatrix(*t) * m; first = false; }
    cur = parentOf(cur);
    if (!w.alive(cur)) break;
  }
  return m;
}
inline tool::Vec3 Editor::worldPos(tool::Entity e) {
  if (!scene.world().has<Parent>(e)) { const Transform* t = scene.world().get<Transform>(e); return t ? tool::Vec3{t->pos[0], t->pos[1], t->pos[2]} : tool::Vec3{}; }
  const tool::Mat4 m = worldMat(e); return {m.m[3][0], m.m[3][1], m.m[3][2]};
}

inline bool Editor::isDescendant(tool::Entity e, tool::Entity anc) {
  tool::Entity cur = e;
  for (int i = 0; i < 64 && scene.world().alive(cur); ++i) { if (cur == anc) return true; cur = parentOf(cur); }
  return false;
}

inline bool Editor::setParent(tool::Entity child, tool::Entity parent, bool keepWorld) {
  auto& w = scene.world();
  if (!w.alive(child)) return false;
  if (w.alive(parent) && isDescendant(parent, child)) return false; // ciclo (inclui pai == filho)
  Transform* t = w.get<Transform>(child); if (!t) return false;
  const tool::Mat4 world = worldMat(child);
  if (w.has<Parent>(child)) w.remove<Parent>(child);
  if (w.alive(parent)) w.add<Parent>(child, Parent{parent});
  if (keepWorld) {
    const tool::Mat4 local = w.alive(parent) ? inverseAffine(worldMat(parent)) * world : world;
    decomposeTRS(local, t->pos, t->rot, t->scale);
  }
  return true;
}

inline void Editor::bakeWorld(tool::Entity e) {
  auto& w = scene.world();
  if (!w.has<Parent>(e)) return;
  Transform* t = w.get<Transform>(e);
  if (t) decomposeTRS(worldMat(e), t->pos, t->rot, t->scale);
  w.remove<Parent>(e);
}

inline void Editor::destroy(tool::Entity e) {
  auto& w = scene.world(); if (!w.alive(e)) return;
  std::vector<tool::Entity> all; // e + descendentes (BFS)
  all.push_back(e);
  for (size_t i = 0; i < all.size() && all.size() < 100000; ++i)
    w.each<Parent>([&](tool::Entity c, Parent& p) { if (p.e == all[i] && std::find(all.begin(), all.end(), c) == all.end()) all.push_back(c); });
  for (auto d : all) { if (!w.alive(d)) continue; releaseScript(d); w.destroy(d); if (selected == d) selected = {}; }
}

inline tool::Camera Editor::cameraFor(tool::Entity e) {
  auto& w = scene.world(); const CameraComp* c = w.get<CameraComp>(e); const Transform* t = w.get<Transform>(e);
  tool::Camera cam; if (!c || !t) return cam;
  if (!w.has<Parent>(e)) return cameraOf(*t, *c);
  const tool::Mat4 m = worldMat(e);
  auto dir = [&](tool::Vec3 v) { const tool::Vec4 r = m.mul({v.x, v.y, v.z, 0.f}); return tool::normalize({r.x, r.y, r.z}); };
  cam.eye = {m.m[3][0], m.m[3][1], m.m[3][2]}; cam.target = cam.eye + dir({0, 0, -1}); cam.up = dir({0, 1, 0});
  cam.fovY = c->fovDeg * kDeg2Rad; cam.nearZ = c->nearZ; cam.farZ = c->farZ;
  return cam;
}

} // namespace editor
