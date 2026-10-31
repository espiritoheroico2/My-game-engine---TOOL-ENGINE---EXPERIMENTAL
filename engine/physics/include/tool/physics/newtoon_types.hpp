// Newtoon Physics - tipos, materiais, eventos e COMPONENTES (os arrays de componentes pertencem a engine; a Newtoon so processa).
// Convencao: "Entity" = indice nos arrays Transform/Rigidbody/Collider que voce passa para PhysicsWorld::Simulate.
#pragma once
#include "newtoon_math.hpp"

#include <cstdint>

namespace Newtoon {

using Entity = uint32_t;
constexpr Entity kNoEntity = 0xFFFFFFFFu;

enum class ShapeType : uint8_t { Box, Sphere, Cylinder, CustomConvex };
// Cylinder/CustomConvex: ainda sem narrow-phase proprio; sao tratados como a caixa envolvente (extents). Ver docs/PHYSICS.md.
enum class CombineMode : uint8_t { Average, Minimum, Maximum, Multiply };

struct PhysicsMaterial {
  float friction = 0.5f;
  float bounciness = 0.0f;
  CombineMode frictionCombine = CombineMode::Average;
  CombineMode bounceCombine = CombineMode::Maximum;
};

inline float Combine(CombineMode m, float a, float b) {
  switch (m) {
    case CombineMode::Minimum: return a < b ? a : b;
    case CombineMode::Maximum: return a > b ? a : b;
    case CombineMode::Multiply: return a * b;
    default: return 0.5f * (a + b);
  }
}

enum class CollisionType : uint8_t { Enter, Stay, Exit };
struct CollisionEvent {
  Entity entityA = kNoEntity, entityB = kNoEntity;
  Vec3 contactPoint, contactNormal; // normal aponta de A para B
  float impactForce = 0.f;          // impulso normal total do contato (aprox.)
  CollisionType type = CollisionType::Enter;
  bool trigger = false;             // um dos colliders e gatilho (sem resposta fisica)
};

struct RaycastHit {
  bool hit = false;
  Vec3 point, normal;
  float distance = 0.f;
  Entity entity = kNoEntity;
};

struct Transform { Vec3 position; Quat rotation; };

struct Rigidbody {
  Vec3 velocity, angularVelocity;
  Vec3 forceAccumulator, torqueAccumulator;
  float inverseMass = 1.f;
  Vec3 inverseInertiaLocal{1, 1, 1};   // 1/I no espaco do corpo (diagonal); preenchido por SetMassFromShape
  // --- ajustes do Inspector ---
  float gravityScale = 1.f;            // 0 = flutua, 2 = cai o dobro
  bool useGravity = true;
  float linearDamping = 0.01f;         // perda de velocidade por segundo (arrasto simples)
  float angularDamping = 0.05f;
  bool isKinematic = false;            // movido so pela velocidade (nao reage a colisoes), empurra os outros
  bool freezeRotation = false;
  // --- VECTOR PHYSICS: aceleracao propria que SUBSTITUI a gravidade do mundo para este corpo (ex.: cair para o lado) ---
  bool overrideGravity = false;
  Vec3 gravity{0, -9.81f, 0};
  // --- sono / CCD ---
  bool isSleeping = false;
  float sleepTimer = 0.f, sleepThreshold = 0.05f;
  bool useCCD = false;
  // --- resultado do ultimo passo (somente leitura) ---
  bool grounded = false;               // tocando uma superficie "de chao" (normal.y > 0.7 em relacao ao mundo)
  Vec3 groundNormal{0, 1, 0};

  void SetMass(float mass) { inverseMass = (mass <= 0.f) ? 0.f : (1.f / mass); }
  bool IsStatic() const { return inverseMass == 0.f && !isKinematic; }
  bool IsDynamic() const { return inverseMass > 0.f && !isKinematic; }
  void WakeUp() { isSleeping = false; sleepTimer = 0.f; }
  void AddForce(const Vec3& f) { forceAccumulator += f; WakeUp(); }
  void AddImpulse(const Vec3& j) { velocity += j * inverseMass; WakeUp(); }          // ex.: pulo = AddImpulse({0, massa*v, 0})
  void AddTorque(const Vec3& t) { torqueAccumulator += t; WakeUp(); }
};

struct Collider {
  ShapeType type = ShapeType::Box;
  uint16_t materialId = 0;
  Vec3 extents{0.5f, 0.5f, 0.5f};      // meia-medida da caixa (ja com a escala do objeto)
  float radius = 0.5f;                 // esfera
  float height = 1.f;                  // reservado (cilindro)
  Vec3 offset;                         // deslocamento do centro no espaco do corpo
  bool isTrigger = false;              // detecta sem empurrar
  void* customMesh = nullptr;          // reservado
};

// Calcula massa/inercia a partir da forma (caixa ou esfera solidas). Retorna sem mudar nada se mass <= 0 (corpo estatico).
inline void SetMassFromShape(Rigidbody& rb, const Collider& c, float mass) {
  rb.SetMass(mass);
  if (mass <= 0.f) { rb.inverseInertiaLocal = {0, 0, 0}; return; }
  if (c.type == ShapeType::Sphere) { const float i = 0.4f * mass * c.radius * c.radius; rb.inverseInertiaLocal = {1.f / i, 1.f / i, 1.f / i}; return; }
  const Vec3 e = c.extents; const float k = mass / 3.f; // (1/12)*m*(w^2+h^2) com w = 2e  ->  m/3 * (e1^2+e2^2)
  rb.inverseInertiaLocal = {1.f / (k * (e.y * e.y + e.z * e.z)), 1.f / (k * (e.x * e.x + e.z * e.z)), 1.f / (k * (e.x * e.x + e.y * e.y))};
}

// Juntas (estrutura de dados; solver de juntas esta no roteiro - docs/PHYSICS.md).
enum class JointType : uint8_t { Fixed, Hinge, BallAndSocket };
struct Joint { JointType type = JointType::Fixed; Entity bodyA = kNoEntity, bodyB = kNoEntity; Vec3 anchorPointA, anchorPointB, hingeAxis; };

struct WheelCollider {
  float radius = 0.3f, suspensionRestLength = 0.5f, springStiffness = 30000.f, springDamper = 3000.f;
  bool isGrounded = false; float currentSuspensionLength = 0.f;
};

struct AABB { Vec3 min, max; };

} // namespace Newtoon
