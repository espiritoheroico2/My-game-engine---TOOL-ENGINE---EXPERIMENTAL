// Newtoon Physics - modulos opcionais: Verlet (ossos/tecidos), suspensao de veiculo por raycast e controle de ragdoll.
// Portados do repositorio original com correcoes (gravidade configuravel, restricoes com massa zero, raycast real).
#pragma once
#include "newtoon_world.hpp"

#include <algorithm>
#include <cmath>
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

// ------------------------------------------------------------------ XPBD (Cabelos e Tecidos)
// Extended Position-Based Dynamics com substeps (Macklin et al.): rigidez independente de dt e iteracoes.
struct alignas(16) XPBDParticle {
  Vec3 position{0.f, 0.f, 0.f};
  Vec3 predicted{0.f, 0.f, 0.f};
  Vec3 velocity{0.f, 0.f, 0.f};
  float inverseMass = 1.f; // 0 = particula fixa (pino / raiz do cabelo)
  float windInfluence = 1.f;
  float damping = 0.02f;
};

struct XPBDDistanceConstraint {
  Entity a = 0, b = 0;
  float restLength = 1.f;
  float compliance = 0.f; // 0 = infinitamente rigido; > 0 = elastico (m/N)
  float lambda = 0.f;
};

// Restricao de flexao (bending) entre 3 particulas (ex: a-b-c em um fio de cabelo ou dobra de tecido).
struct XPBDBendingConstraint {
  Entity a = 0, b = 0, c = 0;
  float restDistance = 0.f; // distancia de repouso entre a e c (ou curvatura)
  float compliance = 0.01f;
  float lambda = 0.f;
};

struct XPBDSphereCollider {
  Vec3 center{0.f, 0.f, 0.f};
  float radius = 0.5f;
};

class XPBDClothAndHairSolver {
 public:
  Vec3 gravity{0.f, -9.81f, 0.f};
  int substeps = 4;     // "Small Steps": mais substeps com 1 iteracao converge muito mais rapido
  int iterations = 1;
  float groundY = -1e30f; // plano de chao opcional (desativado por padrao)

  void Step(std::vector<XPBDParticle>& particles,
            std::vector<XPBDDistanceConstraint>& distConstraints,
            std::vector<XPBDBendingConstraint>& bendConstraints,
            const std::vector<XPBDSphereCollider>& colliders,
            const Vec3& wind,
            float dt) const {
    if (dt <= 0.f || particles.empty()) return;
    const int steps = std::max(1, substeps);
    const int iters = std::max(1, iterations);
    const float h = dt / static_cast<float>(steps);
    const float invH2 = 1.f / (h * h);

    for (int s = 0; s < steps; ++s) {
      // 1) Predicao de posicao com gravidade, vento e arrasto
      for (auto& p : particles) {
        if (p.inverseMass <= 0.f) { p.predicted = p.position; p.velocity = {}; continue; }
        const Vec3 acc = gravity + wind * (p.windInfluence * p.inverseMass);
        p.velocity = (p.velocity + acc * h) * std::max(0.f, 1.f - p.damping * h);
        p.predicted = p.position + p.velocity * h;
      }

      for (auto& c : distConstraints) c.lambda = 0.f;
      for (auto& c : bendConstraints) c.lambda = 0.f;

      // 2) Resolucao das restricoes XPBD
      for (int it = 0; it < iters; ++it) {
        for (auto& c : distConstraints) {
          if (c.a >= particles.size() || c.b >= particles.size()) continue;
          auto& pa = particles[c.a]; auto& pb = particles[c.b];
          const float wSum = pa.inverseMass + pb.inverseMass;
          if (wSum <= 0.f) continue;
          const Vec3 diff = pb.predicted - pa.predicted;
          const float len = diff.Magnitude();
          if (len < 1e-7f) continue;
          const float C = len - c.restLength;
          const float alphaTilde = c.compliance * invH2;
          const float dLambda = (-C - alphaTilde * c.lambda) / (wSum + alphaTilde);
          c.lambda += dLambda;
          const Vec3 corr = diff * (dLambda / len);
          if (pa.inverseMass > 0.f) pa.predicted -= corr * pa.inverseMass;
          if (pb.inverseMass > 0.f) pb.predicted += corr * pb.inverseMass;
        }

        for (auto& c : bendConstraints) {
          if (c.a >= particles.size() || c.c >= particles.size()) continue;
          auto& pa = particles[c.a]; auto& pc = particles[c.c];
          const float wSum = pa.inverseMass + pc.inverseMass;
          if (wSum <= 0.f) continue;
          const Vec3 diff = pc.predicted - pa.predicted;
          const float len = diff.Magnitude();
          if (len < 1e-7f) continue;
          const float C = len - c.restDistance;
          const float alphaTilde = c.compliance * invH2;
          const float dLambda = (-C - alphaTilde * c.lambda) / (wSum + alphaTilde);
          c.lambda += dLambda;
          const Vec3 corr = diff * (dLambda / len);
          if (pa.inverseMass > 0.f) pa.predicted -= corr * pa.inverseMass;
          if (pc.inverseMass > 0.f) pc.predicted += corr * pc.inverseMass;
        }

        // 3) Colisao em nivel de posicao com esferas e plano do chao
        for (auto& p : particles) {
          if (p.inverseMass <= 0.f) continue;
          if (p.predicted.y < groundY) p.predicted.y = groundY;
          for (const auto& sc : colliders) {
            const Vec3 d = p.predicted - sc.center;
            const float d2 = d.SqrMagnitude();
            if (d2 < sc.radius * sc.radius && d2 > 1e-12f) {
              const float dist = std::sqrt(d2);
              p.predicted = sc.center + d * (sc.radius / dist);
            }
          }
        }
      }

      // 4) Atualiza velocidades e posicoes
      const float invH = 1.f / h;
      for (auto& p : particles) {
        if (p.inverseMass <= 0.f) continue;
        p.velocity = (p.predicted - p.position) * invH;
        p.position = p.predicted;
      }
    }
  }
};

} // namespace Newtoon
