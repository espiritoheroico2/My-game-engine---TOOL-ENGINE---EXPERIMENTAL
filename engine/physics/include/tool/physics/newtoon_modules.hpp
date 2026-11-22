// Newtoon Physics - modulos opcionais: Verlet (ossos/tecidos), suspensao de veiculo por raycast e controle de ragdoll.
// Portados do repositorio original com correcoes (gravidade configuravel, restricoes com massa zero, raycast real).
#pragma once
#include "newtoon_world.hpp"

#include <vector>

namespace Newtoon {

// Ponto de osso no solver Verlet: posicao atual, anterior, massa e influencia do vento.
struct DynamicBone { Vec3 position, previousPosition; float mass = 1.f, windInfluence = 0.f; };
// Restricao de distancia entre dois ossos (indices no vetor de ossos).
struct BoneConstraint { Entity boneA = 0, boneB = 0; float restLength = 1.f, stiffness = 1.f; };

// Solver Verlet para ossos e tecidos (cabelo, cauda). Integra a gravidade e o vento e corrige as distancias das restricoes.
class SkeletalPhysicsSolver {
 public:
  Vec3 gravity{0, -9.81f, 0};
  int iterations = 3;
  // Avanca um passo de tamanho dt: integra os ossos e depois corrige as restricoes em 'iterations' rodadas.
  void Step(std::vector<DynamicBone>& bones, const std::vector<BoneConstraint>& cs, const Vec3& wind, float dt) const {
    for (auto& b : bones) {
      if (b.mass <= 0.f) continue;                                    // massa 0 = osso fixo
      const Vec3 vel = b.position - b.previousPosition; b.previousPosition = b.position;
      const Vec3 acc = gravity + Vec3{wind.x * b.windInfluence, 0.f, wind.z * b.windInfluence} * (1.f / b.mass);
      b.position += vel + acc * (dt * dt);
    }
    for (int it = 0; it < iterations; ++it)
      for (const auto& c : cs) {
        if (c.boneA >= bones.size() || c.boneB >= bones.size()) continue;
        DynamicBone& a = bones[c.boneA]; DynamicBone& b = bones[c.boneB];
        const Vec3 d = b.position - a.position; const float len = d.Magnitude(); if (len < 1e-6f) continue;
        const Vec3 corr = d * ((len - c.restLength) / len * 0.5f * c.stiffness);
        const bool fa = a.mass > 0.f, fb = b.mass > 0.f; if (!fa && !fb) continue;
        const float wa = fa ? (fb ? 1.f : 2.f) : 0.f, wb = fb ? (fa ? 1.f : 2.f) : 0.f; // osso fixo: o outro leva a correcao inteira
        a.position += corr * wa; b.position -= corr * wb;
      }
  }
};

// Suspensao de veiculo: cada roda faz um raycast para o chao e aplica forca de mola e amortecedor no chassi.
class VehicleSolver {
 public:
  // Uma roda: raio para baixo a partir da roda; mola de Hooke + amortecedor empurram o chassi.
  static void UpdateWheel(WheelCollider& w, const Vec3& wheelPos, Rigidbody& body, const PhysicsWorld& world, Entity chassis, float /*dt*/) {
    const RaycastHit h = world.Raycast(wheelPos, {0, -1, 0}, w.suspensionRestLength + w.radius, chassis);
    w.isGrounded = h.hit;
    if (!h.hit) return;
    w.currentSuspensionLength = h.distance - w.radius;
    const float compression = w.suspensionRestLength - w.currentSuspensionLength;
    body.AddForce({0.f, w.springStiffness * compression - w.springDamper * body.velocity.y, 0.f});
  }
};

// Controla um ragdoll: liga/desliga as partes do corpo entre fisica total e corpo fixo.
class RagdollController {
 public:
  std::vector<Entity> parts; bool active = false; float partMass = 15.f;
  // Ativa o ragdoll: define a massa das partes e as acorda.
  void Enable(std::vector<Rigidbody>& bodies) { active = true; for (Entity e : parts) if (e < bodies.size()) { bodies[e].SetMass(partMass); bodies[e].WakeUp(); } }
  // Desativa o ragdoll: torna as partes estaticas (massa infinita) e as coloca para dormir.
  void Disable(std::vector<Rigidbody>& bodies) { active = false; for (Entity e : parts) if (e < bodies.size()) { bodies[e].inverseMass = 0.f; bodies[e].isSleeping = true; } }
};

} // namespace Newtoon
