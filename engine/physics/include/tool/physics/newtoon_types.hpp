// Newtoon Physics - tipos, materiais, eventos e COMPONENTES (os arrays de componentes pertencem a engine; a Newtoon so processa).
// Convencao: "Entity" = indice nos arrays Transform/Rigidbody/Collider que voce passa para PhysicsWorld::Simulate.
#pragma once
#include "newtoon_math.hpp"

#include <cstdint>

namespace Newtoon {

using Entity = uint32_t;
// Valor que indica 'nenhuma entidade'.
constexpr Entity kNoEntity = 0xFFFFFFFFu;

// Forma do collider usada na deteccao de colisao.
enum class ShapeType : uint8_t { Box, Sphere, Cylinder, CustomConvex };
// Cylinder/CustomConvex: ainda sem narrow-phase proprio; sao tratados como a caixa envolvente (extents). Ver docs/planos/10_FISICA.md.
// Como combinar dois coeficientes de material (atrito ou quique) de corpos que colidem.
enum class CombineMode : uint8_t { Average, Minimum, Maximum, Multiply };

// Material fisico: atrito e quique (bounciness) e como combina-los com o outro material.
struct PhysicsMaterial {
  float friction = 0.5f;
  float bounciness = 0.0f;
  CombineMode frictionCombine = CombineMode::Average;
  CombineMode bounceCombine = CombineMode::Maximum;
};

// Combina dois valores a e b conforme o modo m (media, minimo, maximo ou produto).
inline float Combine(CombineMode m, float a, float b) {
  switch (m) {
    case CombineMode::Minimum: return a < b ? a : b;
    case CombineMode::Maximum: return a > b ? a : b;
    case CombineMode::Multiply: return a * b;
    default: return 0.5f * (a + b);
  }
}

// Fase do evento de colisao: inicio (Enter), continuacao (Stay) ou fim (Exit).
enum class CollisionType : uint8_t { Enter, Stay, Exit };
// Evento de colisao entre duas entidades, com ponto, normal e impacto do contato.
struct CollisionEvent {
  Entity entityA = kNoEntity, entityB = kNoEntity;
  Vec3 contactPoint, contactNormal; // normal aponta de A para B
  float impactForce = 0.f;          // impulso normal total do contato (aprox.)
  CollisionType type = CollisionType::Enter;
  bool trigger = false;             // um dos colliders e gatilho (sem resposta fisica)
};

// Resultado de um raycast: se acertou, onde, a normal, a distancia e a entidade atingida.
struct RaycastHit {
  bool hit = false;
  Vec3 point, normal;
  float distance = 0.f;
  Entity entity = kNoEntity;
};

// Posicao e rotacao de um objeto, lidas pela fisica.
struct Transform { Vec3 position; Quat rotation; };

// Componente de corpo rigido: massa, velocidades, forcas acumuladas e configuracoes de movimento.
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

  // Define a massa (0 ou menos = corpo estatico com massa infinita).
  void SetMass(float mass) { inverseMass = (mass <= 0.f) ? 0.f : (1.f / mass); }
  // True se o corpo nunca se move (massa infinita e nao cinematico).
  bool IsStatic() const { return inverseMass == 0.f && !isKinematic; }
  // True se o corpo e afetado pela fisica (tem massa e nao e cinematico).
  bool IsDynamic() const { return inverseMass > 0.f && !isKinematic; }
  // Acorda o corpo da hibernacao e zera o temporizador de sono.
  void WakeUp() { isSleeping = false; sleepTimer = 0.f; }
  // Acumula a forca f para o proximo passo (a forca e em newtons; acorda o corpo).
  void AddForce(const Vec3& f) { forceAccumulator += f; WakeUp(); }
  // Aplica um impulso instantaneo j: muda a velocidade de acordo com a massa.
  void AddImpulse(const Vec3& j) { velocity += j * inverseMass; WakeUp(); }          // ex.: pulo = AddImpulse({0, massa*v, 0})
  // Acumula um torque t para o proximo passo (acorda o corpo).
  void AddTorque(const Vec3& t) { torqueAccumulator += t; WakeUp(); }
};

// Forma de colisao do objeto (caixa, esfera, etc.), com material, deslocamento e flag de gatilho.
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
// Calcula massa/inercia a partir da forma (caixa ou esfera solidas). Retorna sem mudar nada se mass <= 0 (corpo estatico).
inline void SetMassFromShape(Rigidbody& rb, const Collider& c, float mass) {
  rb.SetMass(mass);
  if (mass <= 0.f) { rb.inverseInertiaLocal = {0, 0, 0}; return; }
  // Limita dimensoes minimas para evitar divisao por zero e inercia infinita.
  if (c.type == ShapeType::Sphere) {
    const float r = std::max(c.radius, 1e-4f);
    const float i = 0.4f * mass * r * r;
    rb.inverseInertiaLocal = {1.f / i, 1.f / i, 1.f / i};
    return;
  }
  const Vec3 e = {std::max(c.extents.x, 1e-4f), std::max(c.extents.y, 1e-4f), std::max(c.extents.z, 1e-4f)};
  const float k = mass / 3.f; // (1/12)*m*(w^2+h^2) com w = 2e  ->  m/3 * (e1^2+e2^2)
  rb.inverseInertiaLocal = {1.f / (k * (e.y * e.y + e.z * e.z)), 1.f / (k * (e.x * e.x + e.z * e.z)), 1.f / (k * (e.x * e.x + e.y * e.y))};
}

// Juntas (estrutura de dados; solver de juntas esta no roteiro - docs/planos/10_FISICA.md).
enum class JointType : uint8_t { Fixed, Hinge, BallAndSocket };
// Junta entre dois corpos: tipo, corpos conectados, pontos de ancoragem e eixo da dobradica. So dados; o solver ainda nao existe.
struct Joint { JointType type = JointType::Fixed; Entity bodyA = kNoEntity, bodyB = kNoEntity; Vec3 anchorPointA, anchorPointB, hingeAxis; };

// Parametros de suspensao de uma roda de veiculo (mola e amortecedor).
struct WheelCollider {
  float radius = 0.3f, suspensionRestLength = 0.5f, springStiffness = 30000.f, springDamper = 3000.f;
  bool isGrounded = false; float currentSuspensionLength = 0.f;
};

// Caixa alinhada aos eixos (AABB), definida pelos cantos minimo e maximo.
struct AABB { Vec3 min, max; };

} // namespace Newtoon
