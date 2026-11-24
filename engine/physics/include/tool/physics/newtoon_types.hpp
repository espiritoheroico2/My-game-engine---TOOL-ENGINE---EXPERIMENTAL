// Newtoon Physics - tipos, materiais, eventos e COMPONENTES (os arrays de componentes pertencem a engine; a Newtoon
// Convencao: "Entity" = indice nos arrays Transform/Rigidbody/Collider que voce passa para PhysicsWorld::Simulate.
#pragma once
#include "newtoon_math.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace Newtoon {

using Entity = uint32_t;
// Valor que indica 'nenhuma entidade'.
constexpr Entity kNoEntity = 0xFFFFFFFFu;

// Forma do collider usada na deteccao de colisao.
enum class ShapeType : uint8_t { Box, Sphere, Cylinder, CustomConvex };

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

// Transformacao espacial de cada entidade passada para PhysicsWorld::Simulate.
struct Transform {
Vec3 position{0.f, 0.f, 0.f};
Quat rotation{0.f, 0.f, 0.f, 1.f};
Vec3 scale{1.f, 1.f, 1.f};
};

// ALOCADOR LINEAR POR FRAME (Baseado no TempAllocator do Jolt Physics)
// Motivo: Evita alocacoes dinamicas (heap) durante o passo de fisica e zera em O(1).
class FrameArenaAllocator {
public:
explicit FrameArenaAllocator(size_t capacityBytes = 16 * 1024 * 1024)
  : buffer_(capacityBytes), offset_(0) {}

template <typename T>
T* AllocateArray(size_t count) {
 static_assert(std::is_trivially_destructible_v<T>,
               "FrameArenaAllocator suporta apenas tipos triviais (POD).");
 if (count == 0) return nullptr;
 const size_t align = alignof(T);
 const size_t alignedOffset = (offset_ + (align - 1)) & ~(align - 1);
 const size_t bytes = sizeof(T) * count;
 assert(alignedOffset + bytes <= buffer_.size() && "Estouro da Arena de Fisica!");
 T* ptr = reinterpret_cast<T*>(buffer_.data() + alignedOffset);
 offset_ = alignedOffset + bytes;
 return ptr;
}

void Reset() noexcept { offset_ = 0; }

private:
std::vector<uint8_t> buffer_;
size_t offset_;
};

// Corpo rigido alinhado a 64 bytes (cache line) para maximizar localidade de cache no solver.
struct alignas(64) Rigidbody {
float mass = 1.f;
float inverseMass = 1.f;
Vec3 velocity{0.f, 0.f, 0.f};
Vec3 angularVelocity{0.f, 0.f, 0.f};
Vec3 forceAccumulator{0.f, 0.f, 0.f};
Vec3 torqueAccumulator{0.f, 0.f, 0.f};
Vec3 inverseInertiaLocal{1.f, 1.f, 1.f};

float linearDamping = 0.02f;
float angularDamping = 0.05f;
float gravityScale = 1.f;
Vec3 gravity{0.f, -9.81f, 0.f};

float sleepThreshold = 0.05f;
float sleepTimer = 0.f;
Vec3 groundNormal{0.f, 1.f, 0.f};

bool isKinematic = false;
bool useGravity = true;
bool overrideGravity = false;
bool freezeRotation = false;
bool useCCD = false;
bool isSleeping = false;
bool grounded = false;

bool IsStatic() const noexcept { return !isKinematic && inverseMass <= 0.f; }
bool IsDynamic() const noexcept { return !isKinematic && inverseMass > 0.f; }

void SetMass(float m) noexcept {
 mass = m;
 if (m <= 0.f || isKinematic) {
   inverseMass = 0.f;
 } else {
   inverseMass = 1.f / m;
 }
}

void WakeUp() noexcept {
 isSleeping = false;
 sleepTimer = 0.f;
}

void AddForce(const Vec3& f) noexcept {
 if (!IsDynamic()) return;
 forceAccumulator += f;
 WakeUp();
}

void AddTorque(const Vec3& t) noexcept {
if (!IsDynamic()) return;
torqueAccumulator += t;
WakeUp();
}

// Impulso instantaneo: muda a velocidade em j * inverseMass (corpo estatico/cinematico nao muda).
void AddImpulse(const Vec3& j) noexcept {
if (!IsDynamic()) return;
velocity += j * inverseMass;
WakeUp();
}
};

// Forma de colisao do objeto (caixa, esfera, etc.), com material, deslocamento e flag de gatilho.
struct Collider {
ShapeType type = ShapeType::Box;
uint16_t materialId = 0;
Vec3 extents{0.5f, 0.5f, 0.5f};      // meia-medida da caixa (ja com a escala do objeto)
float radius = 0.5f;                 // esfera
float height = 1.f;                  // reservado (cilindro)
Vec3 offset{0.f, 0.f, 0.f};          // deslocamento do centro no espaco do corpo
bool isTrigger = false;              // detecta sem empurrar
void* customMesh = nullptr;          // reservado
};

// Calcula massa/inercia a partir da forma (caixa ou esfera solidas). Retorna sem mudar nada se mass <= 0 (corpo est
inline void SetMassFromShape(Rigidbody& rb, const Collider& c, float mass) {
rb.SetMass(mass);
if (mass <= 0.f) { rb.inverseInertiaLocal = {0, 0, 0}; return; }
if (c.type == ShapeType::Sphere) {
const float r = std::max(c.radius, 1e-4f);
const float i = 0.4f * mass * r * r;
rb.inverseInertiaLocal = {1.f / i, 1.f / i, 1.f / i};
return;
}
const Vec3 e = {std::max(c.extents.x, 1e-4f), std::max(c.extents.y, 1e-4f), std::max(c.extents.z, 1e-4f)};
const float k = mass / 3.f;
  rb.inverseInertiaLocal = {1.f / (k * (e.y * e.y + e.z * e.z)), 1.f / (k * (e.x * e.x + e.z * e.z)), 1.f / (k * (e.x * e.x + e.y * e.y))};
}

// Juntas (estrutura de dados; solver de juntas esta no roteiro - docs/planos/10_FISICA.md).
enum class JointType : uint8_t { Fixed, Hinge, BallAndSocket };
struct Joint { JointType type = JointType::Fixed; Entity bodyA = kNoEntity, bodyB = kNoEntity; Vec3 anchorPointA, anchorPointB, hingeAxis; };

// Parametros de suspensao de uma roda de veiculo (mola e amortecedor).
struct WheelCollider {
float radius = 0.3f, suspensionRestLength = 0.5f, springStiffness = 30000.f, springDamper = 3000.f;
bool isGrounded = false; float currentSuspensionLength = 0.f;
};

// Caixa alinhada aos eixos (AABB), definida pelos cantos minimo e maximo.
struct AABB { Vec3 min, max; };

} // namespace Newtoon
