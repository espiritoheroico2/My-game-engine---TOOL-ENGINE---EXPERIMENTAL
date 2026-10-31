// ToolEngine Editor - dados de execucao do modo Play: mundo de fisica Newtoon + mapeamento entidade <-> corpo.
// Nada aqui e salvo: e criado em startPlay() e descartado em stopPlay() (a cena e restaurada do snapshot).
#pragma once
#include "tool/ecs/world.hpp"
#include "tool/physics/newtoon.hpp"

#include <cmath>
#include <unordered_map>
#include <vector>

namespace editor {

// Euler (graus, ordem Ry*Rx*Rz do editor) <-> quaternion da Newtoon. Formulas conferidas no selftest (ida e volta).
inline Newtoon::Quat eulerToQuat(const float r[3]) {
  constexpr float d = 3.14159265f / 180.f;
  using Newtoon::Quat; using Newtoon::Vec3;
  return (Quat::FromAxisAngle({0, 1, 0}, r[1] * d) * Quat::FromAxisAngle({1, 0, 0}, r[0] * d) * Quat::FromAxisAngle({0, 0, 1}, r[2] * d)).Normalized();
}
inline void quatToEuler(const Newtoon::Quat& q, float r[3]) {
  const Newtoon::Mat3 m = q.ToMat3();                    // R = Ry*Rx*Rz
  constexpr float d = 180.f / 3.14159265f;
  const float sx = std::fmax(-1.f, std::fmin(1.f, -m.r[1].z));
  r[0] = std::asin(sx) * d;
  if (std::fabs(sx) < 0.99999f) { r[1] = std::atan2(m.r[0].z, m.r[2].z) * d; r[2] = std::atan2(m.r[1].x, m.r[1].y) * d; }
  else { r[1] = std::atan2(-m.r[2].x, m.r[0].x) * d; r[2] = 0.f; } // gimbal lock: reparte tudo em Y
}

struct PlayRuntime {
  Newtoon::PhysicsWorld world;
  std::vector<Newtoon::Transform> tf;
  std::vector<Newtoon::Rigidbody> rb;
  std::vector<Newtoon::Collider> col;
  std::vector<tool::Entity> ents;                  // indice do corpo -> entidade
  std::unordered_map<uint32_t, uint32_t> indexOf;  // entity.index -> indice do corpo
  std::vector<char> dead;                          // corpo "apagado" durante o Play
  std::vector<tool::Entity> destroyQueue;
  std::string snapshot; int snapshotSel = -1; bool hasSnapshot = false;
  bool built = false;
  void clear() { *this = PlayRuntime{}; }
};

} // namespace editor
