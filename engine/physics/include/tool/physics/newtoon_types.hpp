// Newtoon Physics - tipos, materiais, eventos e COMPONENTES (os arrays de componentes pertencem a engine; a Newtoon so processa).
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
constexpr Entity kNoEntity = 0xFFFFFFFFu;

enum class ShapeType : uint8_t { Box, Sphere, Cylinder, CustomConvex };
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
  Vec3 contactPoint, contactNormal;
  float impactForce = 0.f;
  CollisionType type = CollisionType::Enter;
  bool trigger = false;
};

enum class MotionType : uint8_t {
  Static = 0,
  Kinematic = 1,
  Dynamic = 2
};

// ALOCADOR LINEAR POR FRAME (Baseado no TempAllocator do Jolt Physics)
// Evita alocações dinâmicas (heap) ao gerar pares de colisão e manifolds a cada tick.
class FrameArenaAllocator {
public:
  explicit FrameArenaAllocator(size_t capacityBytes = 16 * 1024 * 1024)
      : m_buffer(capacityBytes), m_offset(0) {}

  template <typename T>
  T* allocateArray(size_t count) {
    static_assert(std::is_trivially_destructible_v<T>,
                  "FrameArenaAllocator suporta apenas tipos triviais (POD).");
    if (count == 0) return nullptr;
    size_t align = alignof(T);
    size_t alignedOffset = (m_offset + (align - 1)) & ~(align - 1);
    size_t bytes = sizeof(T) * count;
    assert(alignedOffset + bytes <= m_buffer.size() && "Estouro da Arena de Fisica!");

    T* ptr = reinterpret_cast<T*>(m_buffer.data() + alignedOffset);
    m_offset = alignedOffset + bytes;
    return ptr;
  }

  void reset() noexcept { m_offset = 0; }

private:
  std::vector<uint8_t> m_buffer;
  size_t m_offset;
};

struct RaycastHit {
  bool hit = false;
  Vec3 point, normal;
  float distance = 0.f;
  Entity entity = kNoEntity;
};

// DADOS QUENTES (Hot Data): Lidos pela Broad Phase e Narrow Phase (64 bytes = 1 cache line).
struct alignas(64) BodyCore {
  Vec3 position{0.0f, 0.0f, 0.0f};
  uint32_t shapeId{0};

  Quat rotation{0.0f, 0.0f, 0.0f, 1.0f};

  uint32_t motionIndex{UINT32_MAX}; // UINT32_MAX se Static
  Entity entityId{kNoEntity};
  MotionType motionType{MotionType::Static};
  uint8_t isSleeping{0};
  uint16_t sleepCounter{0};
};

// DADOS DE MOVIMENTO: Alocados APENAS para corpos Dynamic ou Kinematic (128 bytes = 2 cache lines).
struct alignas(64) MotionProperties {
  Vec3 linearVelocity{0.0f, 0.0f, 0.0f};
  float invMass{1.0f};

  Vec3 angularVelocity{0.0f, 0.0f, 0.0f};
  float linearDamping{0.02f};

  Vec3 forceAccumulator{0.0f, 0.0f, 0.0f};
  float angularDamping{0.05f};

  Vec3 torqueAccumulator{0.0f, 0.0f, 0.0f};
  float gravityScale{1.0f};

  Vec3 inverseInertiaLocal{1.0f, 1.0f, 1.0f};
  float sleepTimer{0.0f};
};

// COMPONENTE PÚBLICO DO ECS (Mantém compatibilidade com a engine e SetMassFromShape)
struct Rigidbody {
  MotionType motionType = MotionType::Dynamic;
  float mass = 1.0f;
  float inverseMass = 1.0f;
  Vec3 linearVelocity{0.0f, 0.0f, 0.0f};
  Vec3 angularVelocity{0.0f, 0.0f, 0.0f};
  Vec3 force{0.0f, 0.0f, 0.0f};
  Vec3 torque{0.0f, 0.0f, 0.0f};
  Vec3 inverseInertiaLocal{1.0f, 1.0f, 1.0f};
  float linearDamping = 0.02f;
  float angularDamping = 0.05f;
  float gravityScale = 1.0f;
  bool isKinematic = false;
  bool useGravity = true;
  bool isSleeping = false;

  void SetMass(float m) {
    mass = m;
    if (m <= 0.0f || isKinematic || motionType == MotionType::Static) {
      inverseMass = 0.0f;
    } else {
      inverseMass = 1.0f / m;
    }
  }
};

struct Collider {
  ShapeType type = ShapeType::Box;
  uint16_t materialId = 0;
  Vec3 extents{0.5f, 0.5f, 0.5f};
  float radius = 0.5f;
  float height = 1.f;
  Vec3 offset{0.0f, 0.0f, 0.0f};
  bool isTrigger = false;
  void* customMesh = nullptr;
};

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

enum class JointType : uint8_t { Fixed, Hinge, BallAndSocket };
struct Joint { JointType type = JointType::Fixed; Entity bodyA = kNoEntity, bodyB = kNoEntity; Vec3 anchorPointA, anchorPointB, hingeAxis; };

struct WheelCollider {
  float radius = 0.3f, suspensionRestLength = 0.5f, springStiffness = 30000.f, springDamper = 3000.f;
  bool isGrounded = false; float currentSuspensionLength = 0.f;
};

struct AABB { Vec3 min, max; };

} // namespace Newtoon