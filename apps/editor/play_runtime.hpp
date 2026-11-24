// ToolEngine Editor - dados de execucao do modo Play: mundo de fisica Newtoon + mapeamento entidade <-> corpo.
// Nada aqui e salvo: e criado em startPlay() e descartado em stopPlay() (a cena e restaurada do snapshot).
#pragma once
#include "components.hpp"
#include "tool/core/parallel.hpp"
#include "tool/ecs/world.hpp"
#include "tool/physics/newtoon.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace editor {

inline Newtoon::Quat eulerToQuat(const float r[3]) {
  constexpr float d = 3.14159265f / 180.f;
  using Newtoon::Quat;
  return (Quat::FromAxisAngle({0, 1, 0}, r[1] * d) * Quat::FromAxisAngle({1, 0, 0}, r[0] * d) * Quat::FromAxisAngle({0, 0, 1}, r[2] * d)).Normalized();
}

inline void quatToEuler(const Newtoon::Quat& q, float r[3]) {
  const Newtoon::Mat3 m = q.ToMat3();
  constexpr float d = 180.f / 3.14159265f;
  const float sx = std::fmax(-1.f, std::fmin(1.f, -m.r[1].z));
  r[0] = std::asin(sx) * d;
  if (std::fabs(sx) < 0.99999f) {
    r[1] = std::atan2(m.r[0].z, m.r[2].z) * d;
    r[2] = std::atan2(m.r[1].x, m.r[1].y) * d;
  } else {
    r[1] = std::atan2(-m.r[2].x, m.r[0].x) * d;
    r[2] = 0.f;
  }
}

// SLERP curto entre dois quaternions para interpolacao visual suave entre ticks fisicos.
inline Newtoon::Quat slerpQuat(const Newtoon::Quat& a, Newtoon::Quat b, float t) {
  float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
  if (dot < 0.f) {
    b.x = -b.x; b.y = -b.y; b.z = -b.z; b.w = -b.w;
    dot = -dot;
  }
  if (dot > 0.9995f) {
    return Newtoon::Quat{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t}.Normalized();
  }
  const float theta0 = std::acos(std::clamp(dot, -1.f, 1.f));
  const float theta = theta0 * t;
  const float sinTheta = std::sin(theta);
  const float sinTheta0 = std::sin(theta0);
  const float s0 = std::cos(theta) - dot * sinTheta / sinTheta0;
  const float s1 = sinTheta / sinTheta0;
  return Newtoon::Quat{a.x * s0 + b.x * s1, a.y * s0 + b.y * s1, a.z * s0 + b.z * s1, a.w * s0 + b.w * s1}.Normalized();
}

struct PlayRuntime {
  Newtoon::PhysicsWorld world;
  std::vector<Newtoon::Transform> prevTf;          // pose no inicio do tick fixo anterior (para interpolacao visual)
  std::vector<Newtoon::Transform> tf;
  std::vector<Newtoon::Rigidbody> rb;
  std::vector<Newtoon::Collider> col;
  std::vector<tool::Entity> ents;                  // indice do corpo -> entidade
  std::unordered_map<uint32_t, uint32_t> indexOf;  // entity.index -> indice do corpo
  std::vector<char> dead;                          // corpo "apagado" durante o Play
  std::vector<char> culled;                        // corpo parado pelo culling de simulacao
  std::vector<char> wasSleeping;                   // evita reescrever Transform de corpos ja dormindo
  size_t culledCount = 0;
  std::vector<tool::Entity> destroyQueue;
  std::string snapshot;
  int snapshotSel = -1;
  bool hasSnapshot = false;
  bool built = false;

  void clear() { *this = PlayRuntime{}; }

  // Grava snapshot das poses atuais antes de avancar o tick de fisica.
  void capturePreviousTransforms() {
    if (prevTf.size() != tf.size()) prevTf.resize(tf.size());
    std::copy(tf.begin(), tf.end(), prevTf.begin());
  }

  // Avalia o culling por raio da camera em paralelo para milhares de corpos dinamicos.
  void updateSimulationRadiusParallel(tool::World& ecsWorld, float radius, float cx, float cy, float cz) {
    // Limita ao menor vetor: dead/culled/tf/rb sao indexados pelo mesmo i (evita leitura fora do limite).
    const size_t count = std::min({ents.size(), tf.size(), rb.size(), dead.size(), culled.size()});
    if (count == 0) return;

    if (radius > 0.f) {
      const float outR2 = radius * radius;
      const float inR2 = (radius * 0.9f) * (radius * 0.9f);
      std::atomic<size_t> totalCulled{0};

      tool::parallelFor(count, 512, [&](size_t b, size_t e) {
        size_t localCulled = 0;
        for (size_t i = b; i < e; ++i) {
          if (dead[i] || !rb[i].IsDynamic()) continue;
          const tool::Entity ent = ents[i];
          if (!ecsWorld.alive(ent)) continue;

          const float dx = tf[i].position.x - cx;
          const float dy = tf[i].position.y - cy;
          const float dz = tf[i].position.z - cz;
          const float d2 = dx * dx + dy * dy + dz * dz;

          if (!culled[i] && d2 > outR2) {
            culled[i] = 1;
            rb[i].isSleeping = true;
            rb[i].velocity = {};
            rb[i].angularVelocity = {};
            ecsWorld.setSimulating(ent, false);
          } else if (culled[i] && d2 < inR2) {
            culled[i] = 0;
            rb[i].WakeUp();
            if (i < wasSleeping.size()) wasSleeping[i] = 0;
            ecsWorld.setSimulating(ent, true);
          }
          if (culled[i]) ++localCulled;
        }
        if (localCulled > 0) totalCulled.fetch_add(localCulled, std::memory_order_relaxed);
      });
      culledCount = totalCulled.load(std::memory_order_relaxed);
    } else if (culledCount > 0) {
      tool::parallelFor(count, 512, [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) {
          if (culled[i]) {
            culled[i] = 0;
            rb[i].WakeUp();
            if (i < wasSleeping.size()) wasSleeping[i] = 0;
            const tool::Entity ent = ents[i];
            if (ecsWorld.alive(ent)) ecsWorld.setSimulating(ent, true);
          }
        }
      });
      culledCount = 0;
    }
  }

  // Sincroniza e interpola poses fisicas -> Transform ECS em paralelo (alpha em [0, 1]).
  // Ignora estaticos, culled e corpos que ja estavam adormecidos no quadro anterior.
  void syncPhysicsToEcsParallel(tool::World& ecsWorld, float alpha = 1.f) {
    const size_t count = tf.size();
    if (count == 0) return;
    if (wasSleeping.size() != count) wasSleeping.assign(count, 0);

    const bool hasPrev = prevTf.size() == count && alpha < 0.9999f;
    const float t = std::clamp(alpha, 0.f, 1.f);

    tool::parallelFor(count, 256, [&](size_t b, size_t e) {
      for (size_t i = b; i < e; ++i) {
        if ((i < dead.size() && dead[i]) || (i < culled.size() && culled[i])) continue;
        if (i >= rb.size() || !rb[i].IsDynamic()) continue;

        const bool sleepingNow = rb[i].isSleeping;
        if (sleepingNow && wasSleeping[i]) continue; // ja estava parado e o Transform ja esta atualizado
        wasSleeping[i] = sleepingNow ? 1 : 0;

        const tool::Entity ent = ents[i];
        if (!ecsWorld.alive(ent) || ecsWorld.isDormant(ent)) continue;
        auto* tr = ecsWorld.getByIndex<Transform>(ent.index);
        if (!tr) continue;

        const Newtoon::Transform& cur = tf[i];
        if (hasPrev && !sleepingNow) {
          const Newtoon::Transform& prv = prevTf[i];
          tr->pos[0] = prv.position.x + (cur.position.x - prv.position.x) * t;
          tr->pos[1] = prv.position.y + (cur.position.y - prv.position.y) * t;
          tr->pos[2] = prv.position.z + (cur.position.z - prv.position.z) * t;
          const Newtoon::Quat q = slerpQuat(prv.rotation, cur.rotation, t);
          quatToEuler(q, tr->rot);
        } else {
          tr->pos[0] = cur.position.x;
          tr->pos[1] = cur.position.y;
          tr->pos[2] = cur.position.z;
          quatToEuler(cur.rotation, tr->rot);
        }
      }
    });
  }
};

} // namespace editor