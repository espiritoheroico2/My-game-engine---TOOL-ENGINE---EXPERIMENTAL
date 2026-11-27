// Newtoon Physics - Modulo 2D dedicado (sem custo de eixo Z, quaternion ou tensor 3x3).
// - Broad-phase 2D por Sweep-and-Prune no eixo X (O(N log N), sem alocacoes por quadro)
// - Narrow-phase 2D (Circulo-Circulo, Circulo-OBB, OBB-OBB SAT com apenas 4 eixos)
// - Solver de impulsos sequenciais 2D com Warm Starting, sono, eventos e Render Interpolation.
#pragma once
#include "newtoon_jobs.hpp"
#include "newtoon_types.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Newtoon {

enum class ShapeType2D : uint8_t { Box, Circle };

struct Transform2D {
  Vec2 position{0.f, 0.f};
  float rotation = 0.f; // radianos
  Vec2 scale{1.f, 1.f};
};

struct AABB2D {
  Vec2 min{0.f, 0.f};
  Vec2 max{0.f, 0.f};
};

struct RaycastHit2D {
  bool hit = false;
  Vec2 point{0.f, 0.f};
  Vec2 normal{0.f, 0.f};
  float distance = 0.f;
  Entity entity = kNoEntity;
};

struct CollisionEvent2D {
  Entity entityA = kNoEntity, entityB = kNoEntity;
  Vec2 contactPoint{0.f, 0.f};
  Vec2 contactNormal{0.f, 0.f};
  float impactForce = 0.f;
  CollisionType type = CollisionType::Enter;
  bool trigger = false;
};

struct alignas(64) Rigidbody2D {
  float mass = 1.f;
  float inverseMass = 1.f;
  float inverseInertia = 1.f; // escalar em 2D (substitui o tensor 3x3)
  Vec2 velocity{0.f, 0.f};
  float angularVelocity = 0.f;
  Vec2 forceAccumulator{0.f, 0.f};
  float torqueAccumulator = 0.f;

  float linearDamping = 0.02f;
  float angularDamping = 0.05f;
  float gravityScale = 1.f;
  Vec2 gravity{0.f, -9.81f};

  float sleepThreshold = 0.05f;
  float sleepTimer = 0.f;
  Vec2 groundNormal{0.f, 1.f};

  bool isKinematic = false;
  bool useGravity = true;
  bool overrideGravity = false;
  bool freezeRotation = false;
  bool isSleeping = false;
  bool grounded = false;

  bool IsStatic() const noexcept { return !isKinematic && inverseMass <= 0.f; }
  bool IsDynamic() const noexcept { return !isKinematic && inverseMass > 0.f; }

  void SetMass(float m) noexcept {
    mass = m;
    inverseMass = (m <= 0.f || isKinematic) ? 0.f : 1.f / m;
  }

  void WakeUp() noexcept { isSleeping = false; sleepTimer = 0.f; }

  void AddForce(const Vec2& f) noexcept {
    if (!IsDynamic()) return;
    forceAccumulator += f;
    WakeUp();
  }

  void AddTorque(float t) noexcept {
    if (!IsDynamic()) return;
    torqueAccumulator += t;
    WakeUp();
  }

  void AddImpulse(const Vec2& j) noexcept {
    if (!IsDynamic()) return;
    velocity += j * inverseMass;
    WakeUp();
  }
};

struct Collider2D {
  ShapeType2D type = ShapeType2D::Box;
  uint16_t materialId = 0;
  Vec2 halfExtents{0.5f, 0.5f};
  float radius = 0.5f;
  Vec2 offset{0.f, 0.f};
  bool isTrigger = false;
};

inline void SetMassFromShape2D(Rigidbody2D& rb, const Collider2D& c, float mass) {
  rb.SetMass(mass);
  if (mass <= 0.f) { rb.inverseInertia = 0.f; return; }
  if (c.type == ShapeType2D::Circle) {
    const float r = std::max(c.radius, 1e-4f);
    const float inertia = 0.5f * mass * r * r;
    rb.inverseInertia = 1.f / inertia;
    return;
  }
  const float w = std::max(c.halfExtents.x * 2.f, 1e-4f);
  const float h = std::max(c.halfExtents.y * 2.f, 1e-4f);
  const float inertia = (mass / 12.f) * (w * w + h * h);
  rb.inverseInertia = 1.f / inertia;
}

class PhysicsWorld2D {
 public:
  Vec2 gravity{0.f, -9.81f};
  float fixedDt = 1.f / 60.f;
  int maxStepsPerFrame = 5;
  int solverIterations = 8;
  float baumgarte = 0.2f, slop = 0.005f, restitutionThreshold = 1.f;
  bool enableWarmStarting = true;
  float warmStartFactor = 0.85f;
  bool enableInterpolation = true; // desative para economizar banda de memoria com 1M+ corpos
  bool enableSoftConstraints = true; // Soft Step (Box2D v3 / TGS Soft)
  float contactHertz = 30.f;
  float contactDampingRatio = 1.f;
  std::vector<PhysicsMaterial> materials{PhysicsMaterial{}};

  struct Stats {
    int bodies = 0, awake = 0, pairs = 0, contacts = 0, steps = 0, idleSteps = 0;
    int warmStartedContacts = 0;
    bool hitStepLimit = false;
    float stepMs = 0.f;
  } stats;

  const std::vector<CollisionEvent2D>& GetCollisionEvents() const { return events_; }
  void ClearEvents() { events_.clear(); }

  float Alpha() const noexcept { return fixedDt > 0.f ? std::clamp(acc_ / fixedDt, 0.f, 1.f) : 0.f; }

  void GetInterpolatedTransforms(const std::vector<Transform2D>& current, std::vector<Transform2D>& out) const {
    const size_t n = current.size();
    out.resize(n);
    const float a = Alpha();
    if (prevTransforms_.size() != n || a >= 1.f - 1e-5f) {
      out = current;
      return;
    }
    for (size_t i = 0; i < n; ++i) {
      out[i].position = Lerp(prevTransforms_[i].position, current[i].position, a);
      out[i].rotation = Rot2::LerpAngle(prevTransforms_[i].rotation, current[i].rotation, a);
      out[i].scale = current[i].scale;
    }
  }

  void Simulate(std::vector<Transform2D>& tf, std::vector<Rigidbody2D>& rb, const std::vector<Collider2D>& col, float dt) {
    const size_t n = std::min({tf.size(), rb.size(), col.size()});
    stats = Stats{};
    stats.bodies = static_cast<int>(n);
    if (enableInterpolation && prevTransforms_.size() != tf.size()) prevTransforms_ = tf;
    acc_ += std::clamp(dt, 0.f, 0.25f);
    int steps = 0;
    while (acc_ >= fixedDt && steps < maxStepsPerFrame) {
      if (enableInterpolation) prevTransforms_.assign(tf.begin(), tf.end());
      Step(tf, rb, col, n, fixedDt);
      acc_ -= fixedDt;
      ++steps;
    }
    if (steps == maxStepsPerFrame) { stats.hitStepLimit = acc_ >= fixedDt; acc_ = 0.f; }
    stats.steps = steps;
    if (steps == 0 && cache_.size() != n) Prepare(tf, col, n);
  }

  RaycastHit2D Raycast(const Vec2& origin, const Vec2& dirIn, float maxDistance, Entity ignore = kNoEntity) const {
    RaycastHit2D best;
    const Vec2 d = Normalized(dirIn);
    if (d.SqrMagnitude() < 0.5f) return best;
    float maxT = maxDistance;
    for (size_t i = 0; i < cache_.size(); ++i) {
      if (static_cast<Entity>(i) == ignore) continue;
      const AABB2D& box = boxes_[i];
      // Teste rapido contra AABB 2D
      float tmin = 0.f, tmax = maxT;
      if (std::fabs(d.x) < 1e-9f) {
        if (origin.x < box.min.x || origin.x > box.max.x) continue;
      } else {
        float t1 = (box.min.x - origin.x) / d.x, t2 = (box.max.x - origin.x) / d.x;
        if (t1 > t2) std::swap(t1, t2);
        tmin = std::max(tmin, t1); tmax = std::min(tmax, t2);
        if (tmin > tmax) continue;
      }
      if (std::fabs(d.y) < 1e-9f) {
        if (origin.y < box.min.y || origin.y > box.max.y) continue;
      } else {
        float t1 = (box.min.y - origin.y) / d.y, t2 = (box.max.y - origin.y) / d.y;
        if (t1 > t2) std::swap(t1, t2);
        tmin = std::max(tmin, t1); tmax = std::min(tmax, t2);
        if (tmin > tmax) continue;
      }

      float t = 0.f;
      Vec2 nrm{};
      if (RayShape2D(cache_[i], origin, d, maxT, t, nrm)) {
        maxT = t;
        best.hit = true;
        best.distance = t;
        best.entity = static_cast<Entity>(i);
        best.normal = nrm;
        best.point = origin + d * t;
      }
    }
    return best;
  }

 private:
  struct Shape2D {
    Vec2 c{0.f, 0.f};
    Rot2 rot{};
    Vec2 e{0.5f, 0.5f};
    float r = 0.5f;
    bool circle = false;
  };

  struct Pt2D { Vec2 p, n; float depth = 0.f; };

  struct Contact2D {
    Entity a = 0, b = 0;
    Vec2 p, n, t;
    Vec2 ra, rb;
    float depth = 0.f, e = 0.f, mu = 0.f;
    float normalMass = 0.f, tangentMass = 0.f, bias = 0.f, impulseSoftScale = 0.f;
    float jn = 0.f, jt = 0.f;
  };

  struct PairInfo2D {
    Entity a = 0, b = 0;
    Vec2 p, n;
    float impulse = 0.f;
    bool trigger = false;
  };

  struct WarmPoint2D { Vec2 p, n; float jn = 0.f, jt = 0.f; };
  struct WarmManifold2D { WarmPoint2D pts[2]; uint8_t count = 0; };
  struct PairEntry2D { uint64_t key = 0; PairInfo2D info; };
  struct WarmEntry2D { uint64_t key = 0; WarmManifold2D manifold; };

  void Prepare(const std::vector<Transform2D>& tf, const std::vector<Collider2D>& col, size_t n) {
    cache_.resize(n);
    boxes_.resize(n);
    for (size_t i = 0; i < n; ++i) {
      Shape2D& s = cache_[i];
      s.rot = Rot2::FromAngle(tf[i].rotation);
      const Vec2 off{col[i].offset.x * tf[i].scale.x, col[i].offset.y * tf[i].scale.y};
      s.c = tf[i].position + s.rot.Rotate(off);
      s.circle = (col[i].type == ShapeType2D::Circle);
      if (s.circle) {
        s.r = col[i].radius * std::max(std::fabs(tf[i].scale.x), std::fabs(tf[i].scale.y));
        boxes_[i] = {{s.c.x - s.r, s.c.y - s.r}, {s.c.x + s.r, s.c.y + s.r}};
      } else {
        s.e = {std::fabs(col[i].halfExtents.x * tf[i].scale.x), std::fabs(col[i].halfExtents.y * tf[i].scale.y)};
        const float ex = std::fabs(s.rot.c) * s.e.x + std::fabs(s.rot.s) * s.e.y;
        const float ey = std::fabs(s.rot.s) * s.e.x + std::fabs(s.rot.c) * s.e.y;
        boxes_[i] = {{s.c.x - ex, s.c.y - ey}, {s.c.x + ex, s.c.y + ey}};
      }
    }
  }

  void Step(std::vector<Transform2D>& tf, std::vector<Rigidbody2D>& rb, const std::vector<Collider2D>& col, size_t n, float dt) {
    auto t0 = std::chrono::steady_clock::now();
    auto active = [&](size_t i) { return rb[i].isKinematic || (rb[i].IsDynamic() && !rb[i].isSleeping); };
    activeIndices_.clear();
    for (size_t i = 0; i < n; ++i) {
      if (active(i)) activeIndices_.push_back(static_cast<Entity>(i));
    }
    if (activeIndices_.empty()) {
      curPairs_.clear();
      for (const auto& kv : prevPairs_) {
        events_.push_back({kv.info.a, kv.info.b, {}, {}, 0.f, CollisionType::Exit, kv.info.trigger});
      }
      prevPairs_.clear();
      ++stats.idleSteps;
      TrimEvents();
      return;
    }

    // 1) Integracao de velocidades (paralelo, apenas corpos ativos)
    const size_t activeCount = activeIndices_.size();
    JobSystem::Get().ParallelFor(activeCount, 512, [&](size_t b, size_t e) {
      for (size_t idx = b; idx < e; ++idx) {
        const Entity i = activeIndices_[idx];
        Rigidbody2D& r = rb[i];
        r.grounded = false;
        if (!r.IsDynamic()) { r.forceAccumulator = {}; r.torqueAccumulator = 0.f; continue; }
        const Vec2 g = r.overrideGravity ? r.gravity : gravity;
        if (r.useGravity) r.velocity += g * (r.gravityScale * dt);
        r.velocity += r.forceAccumulator * (r.inverseMass * dt);
        if (!r.freezeRotation) r.angularVelocity += r.torqueAccumulator * (r.inverseInertia * dt);
        r.forceAccumulator = {}; r.torqueAccumulator = 0.f;
        r.velocity *= 1.f / (1.f + r.linearDamping * dt);
        r.angularVelocity *= 1.f / (1.f + r.angularDamping * dt);
        if (r.freezeRotation) r.angularVelocity = 0.f;
      }
    });

    // 2) Formas + Broad-phase 2D Sweep-and-Prune no eixo X
    Prepare(tf, col, n);
    sorted_.resize(n);
    for (size_t i = 0; i < n; ++i) sorted_[i] = static_cast<Entity>(i);
    std::sort(sorted_.begin(), sorted_.end(), [&](Entity a, Entity b) { return boxes_[a].min.x < boxes_[b].min.x; });

    // Maior largura em X entre todos os corpos: limita a varredura para tras (corpos a esquerda).
    float maxWidthX = 0.f;
    for (size_t i = 0; i < n; ++i) maxWidthX = std::max(maxWidthX, boxes_[i].max.x - boxes_[i].min.x);

    // So corpos ativos sao origem da varredura. Corpos parados entram apenas como alvo. Cada par e achado
    // uma unica vez: ativo x ativo sai da varredura do que esta mais a esquerda; ativo x parado sai da
    // varredura do ativo (para frente ou para tras). Isso evita comparar parado com parado (O(N x coluna)).
    pairs_.clear();
    for (size_t i = 0; i < n; ++i) {
      const Entity a = sorted_[i];
      if (!active(a)) continue;
      const AABB2D& ba = boxes_[a];
      // Para frente: corpos a direita que se sobrepoem em X (ativos e parados).
      for (size_t j = i + 1; j < n; ++j) {
        const Entity b = sorted_[j];
        const AABB2D& bb = boxes_[b];
        if (bb.min.x > ba.max.x) break; // Sweep-and-Prune early-out!
        if (ba.max.y < bb.min.y || bb.max.y < ba.min.y) continue;
        pairs_.push_back(a < b ? std::make_pair(a, b) : std::make_pair(b, a));
      }
      // Para tras: so corpos parados (os ativos a esquerda ja foram achados pela varredura deles).
      for (size_t j = i; j-- > 0;) {
        const Entity b = sorted_[j];
        const AABB2D& bb = boxes_[b];
        if (bb.min.x < ba.min.x - maxWidthX) break; // nenhum corpo mais a esquerda alcanca ba
        if (active(b) || bb.max.x < ba.min.x) continue;
        if (ba.max.y < bb.min.y || bb.max.y < ba.min.y) continue;
        pairs_.push_back(a < b ? std::make_pair(a, b) : std::make_pair(b, a));
      }
    }
    stats.pairs += static_cast<int>(pairs_.size());

    // 3) Narrow-phase 2D
    contacts_.clear();
    curPairs_.clear();
    curPairs_.reserve(pairs_.size());
    std::vector<Pt2D> pts;
    pts.reserve(4);
    for (const auto& pr : pairs_) {
      pts.clear();
      if (!Collide2D(cache_[pr.first], cache_[pr.second], pts) || pts.empty()) continue;
      const Entity a = pr.first, b = pr.second;
      const bool trig = col[a].isTrigger || col[b].isTrigger;
      PairInfo2D info{a, b, pts[0].p, pts[0].n, 0.f, trig};
      if (!trig) {
        const PhysicsMaterial& ma = Mat(col[a].materialId);
        const PhysicsMaterial& mb = Mat(col[b].materialId);
        const float mu = Combine(ma.frictionCombine, ma.friction, mb.friction);
        const float bounce = Combine(ma.bounceCombine, ma.bounciness, mb.bounciness);
        for (const auto& pt : pts) {
          Contact2D c{};
          c.a = a; c.b = b; c.p = pt.p; c.n = pt.n; c.depth = pt.depth; c.e = bounce; c.mu = mu;
          contacts_.push_back(c);
        }
        if (rb[a].isSleeping && active(b)) rb[a].WakeUp();
        if (rb[b].isSleeping && active(a)) rb[b].WakeUp();
        if (pts[0].n.y > 0.7f) { rb[b].grounded = true; rb[b].groundNormal = pts[0].n; }
        if (pts[0].n.y < -0.7f) { rb[a].grounded = true; rb[a].groundNormal = -pts[0].n; }
      }
      curPairs_.push_back({Key(a, b), info});
    }
    std::sort(curPairs_.begin(), curPairs_.end(), [](const PairEntry2D& x, const PairEntry2D& y) { return x.key < y.key; });
    stats.contacts += static_cast<int>(contacts_.size());

    // 4) Solver 2D com Warm Starting (lookup binario em vetor ordenado)
    if (!contacts_.empty()) {
      for (auto& c : contacts_) {
        const Rigidbody2D& A = rb[c.a];
        const Rigidbody2D& B = rb[c.b];
        c.ra = c.p - tf[c.a].position;
        c.rb = c.p - tf[c.b].position;
        const float rna = Cross(c.ra, c.n);
        const float rnb = Cross(c.rb, c.n);
        const float kn = A.inverseMass + B.inverseMass + rna * rna * A.inverseInertia + rnb * rnb * B.inverseInertia;
        c.normalMass = kn > 1e-9f ? 1.f / kn : 0.f;
        c.t = Perp(c.n);
        const float rta = Cross(c.ra, c.t);
        const float rtb = Cross(c.rb, c.t);
        const float kt = A.inverseMass + B.inverseMass + rta * rta * A.inverseInertia + rtb * rtb * B.inverseInertia;
        c.tangentMass = kt > 1e-9f ? 1.f / kt : 0.f;

        const Vec2 vrel = (B.velocity + Cross(B.angularVelocity, c.rb)) - (A.velocity + Cross(A.angularVelocity, c.ra));
        const float vn = Dot(vrel, c.n);
        const float pen = std::max(c.depth - slop, 0.f);
        if (enableSoftConstraints) {
          const float omega = 2.f * 3.14159265f * contactHertz;
          const float a1 = 2.f * contactDampingRatio + omega * dt;
          const float a2 = dt * omega * a1;
          c.normalMass *= a2 / (1.f + a2);
          c.impulseSoftScale = 1.f / (1.f + a2);
          c.bias = std::min(4.f, (omega / a1) * pen);
        } else {
          c.impulseSoftScale = 0.f;
          c.bias = std::min(4.f, baumgarte / dt * pen);
        }
        if (vn < -restitutionThreshold) c.bias += -c.e * vn;

        if (enableWarmStarting && !warmCache_.empty()) {
          const uint64_t k = Key(c.a, c.b);
          const auto it = std::lower_bound(warmCache_.begin(), warmCache_.end(), k, [](const WarmEntry2D& e, uint64_t key) { return e.key < key; });
          if (it != warmCache_.end() && it->key == k) {
            const WarmManifold2D& wm = it->manifold;
            for (uint8_t m = 0; m < wm.count; ++m) {
              if ((c.p - wm.pts[m].p).SqrMagnitude() < 0.04f && Dot(c.n, wm.pts[m].n) > 0.9f) {
                c.jn = wm.pts[m].jn * warmStartFactor;
                c.jt = wm.pts[m].jt * warmStartFactor;
                const Vec2 P = c.n * c.jn + c.t * c.jt;
                if (rb[c.a].IsDynamic()) { rb[c.a].velocity -= P * rb[c.a].inverseMass; rb[c.a].angularVelocity -= Cross(c.ra, P) * rb[c.a].inverseInertia; }
                if (rb[c.b].IsDynamic()) { rb[c.b].velocity += P * rb[c.b].inverseMass; rb[c.b].angularVelocity += Cross(c.rb, P) * rb[c.b].inverseInertia; }
                ++stats.warmStartedContacts;
                break;
              }
            }
          }
        }
      }

      for (int it = 0; it < solverIterations; ++it) {
        for (auto& c : contacts_) {
          Rigidbody2D& A = rb[c.a];
          Rigidbody2D& B = rb[c.b];
          const bool dynA = A.IsDynamic(), dynB = B.IsDynamic();
          auto apply = [&](const Vec2& P) {
            if (dynA) { A.velocity -= P * A.inverseMass; A.angularVelocity -= Cross(c.ra, P) * A.inverseInertia; }
            if (dynB) { B.velocity += P * B.inverseMass; B.angularVelocity += Cross(c.rb, P) * B.inverseInertia; }
          };
          {
            const Vec2 vrel = (B.velocity + Cross(B.angularVelocity, c.rb)) - (A.velocity + Cross(A.angularVelocity, c.ra));
            float dj = c.normalMass * (-Dot(vrel, c.n) + c.bias) - c.impulseSoftScale * c.jn;
            const float old = c.jn;
            c.jn = std::max(0.f, old + dj);
            apply(c.n * (c.jn - old));
          }
          {
            const Vec2 vrel = (B.velocity + Cross(B.angularVelocity, c.rb)) - (A.velocity + Cross(A.angularVelocity, c.ra));
            float dj = -c.tangentMass * Dot(vrel, c.t);
            const float maxF = c.mu * c.jn;
            const float old = c.jt;
            c.jt = std::clamp(old + dj, -maxF, maxF);
            apply(c.t * (c.jt - old));
          }
        }
      }

      nextWarmCache_.clear();
      nextWarmCache_.reserve(curPairs_.size());
      size_t pCursor = 0;
      for (const auto& c : contacts_) {
        const uint64_t k = Key(c.a, c.b);
        while (pCursor < curPairs_.size() && curPairs_[pCursor].key < k) ++pCursor;
        if (pCursor < curPairs_.size() && curPairs_[pCursor].key == k) curPairs_[pCursor].info.impulse += c.jn;
        else {
          auto it = std::lower_bound(curPairs_.begin(), curPairs_.end(), k, [](const PairEntry2D& e, uint64_t key) { return e.key < key; });
          if (it != curPairs_.end() && it->key == k) it->info.impulse += c.jn;
        }
        if (enableWarmStarting && c.jn > 1e-6f) {
          if (nextWarmCache_.empty() || nextWarmCache_.back().key != k) {
            nextWarmCache_.push_back({k, {}});
          }
          auto& wm = nextWarmCache_.back().manifold;
          if (wm.count < 2) wm.pts[wm.count++] = {c.p, c.n, c.jn, c.jt};
        }
      }
      std::sort(nextWarmCache_.begin(), nextWarmCache_.end(), [](const WarmEntry2D& x, const WarmEntry2D& y) { return x.key < y.key; });
      warmCache_.swap(nextWarmCache_);
    } else {
      warmCache_.clear();
    }

    // 5) Posicoes e rotacoes 2D (paralelo, apenas corpos ativos)
    JobSystem::Get().ParallelFor(activeCount, 512, [&](size_t b, size_t e) {
      for (size_t idx = b; idx < e; ++idx) {
        const Entity i = activeIndices_[idx];
        Rigidbody2D& r = rb[i];
        if (r.IsStatic() || (r.isSleeping && !r.isKinematic)) continue;
        tf[i].position += r.velocity * dt;
        tf[i].rotation += r.angularVelocity * dt;
      }
    });

    // 6) Sono (apenas ativos) e Eventos (two-pointer merge)
    for (size_t idx = 0; idx < activeCount; ++idx) {
      const Entity i = activeIndices_[idx];
      Rigidbody2D& r = rb[i];
      if (!r.IsDynamic() || r.isSleeping) { if (!r.isSleeping) ++stats.awake; continue; }
      if (r.velocity.Magnitude() < r.sleepThreshold && std::fabs(r.angularVelocity) < r.sleepThreshold) {
        r.sleepTimer += dt;
        if (r.sleepTimer > 0.5f) { r.isSleeping = true; r.velocity = {}; r.angularVelocity = 0.f; }
      } else {
        r.sleepTimer = 0.f;
      }
      if (!r.isSleeping) ++stats.awake;
    }

    {
      size_t i = 0, j = 0;
      while (i < curPairs_.size() && j < prevPairs_.size()) {
        if (curPairs_[i].key < prevPairs_[j].key) {
          const auto& p = curPairs_[i++].info;
          events_.push_back({p.a, p.b, p.p, p.n, p.impulse, CollisionType::Enter, p.trigger});
        } else if (prevPairs_[j].key < curPairs_[i].key) {
          const auto& p = prevPairs_[j++].info;
          events_.push_back({p.a, p.b, {}, {}, 0.f, CollisionType::Exit, p.trigger});
        } else {
          const auto& p = curPairs_[i++].info;
          ++j;
          events_.push_back({p.a, p.b, p.p, p.n, p.impulse, CollisionType::Stay, p.trigger});
        }
      }
      while (i < curPairs_.size()) {
        const auto& p = curPairs_[i++].info;
        events_.push_back({p.a, p.b, p.p, p.n, p.impulse, CollisionType::Enter, p.trigger});
      }
      while (j < prevPairs_.size()) {
        const auto& p = prevPairs_[j++].info;
        events_.push_back({p.a, p.b, {}, {}, 0.f, CollisionType::Exit, p.trigger});
      }
    }
    prevPairs_.swap(curPairs_);
    TrimEvents();
    stats.stepMs += std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
  }

  static uint64_t Key(Entity a, Entity b) { if (a > b) std::swap(a, b); return (static_cast<uint64_t>(a) << 32) | b; }
  const PhysicsMaterial& Mat(uint16_t id) const { return materials[id < materials.size() ? id : 0]; }
  void TrimEvents() { if (events_.size() > 20000) events_.erase(events_.begin(), events_.begin() + 10000); }

  static bool Collide2D(const Shape2D& A, const Shape2D& B, std::vector<Pt2D>& out) {
    if (A.circle && B.circle) return CircleCircle(A, B, out);
    if (A.circle) return CircleBox(A, B, out);
    if (B.circle) {
      const size_t first = out.size();
      if (!CircleBox(B, A, out)) return false;
      for (size_t i = first; i < out.size(); ++i) out[i].n = -out[i].n;
      return true;
    }
    return BoxBox2D(A, B, out);
  }

  static bool CircleCircle(const Shape2D& A, const Shape2D& B, std::vector<Pt2D>& out) {
    const Vec2 d = B.c - A.c;
    const float rr = A.r + B.r;
    const float d2 = d.SqrMagnitude();
    if (d2 >= rr * rr) return false;
    const float dist = std::sqrt(d2);
    const Vec2 n = dist > 1e-6f ? d * (1.f / dist) : Vec2{0.f, 1.f};
    out.push_back({A.c + n * (A.r - (rr - dist) * 0.5f), n, rr - dist});
    return true;
  }

  static bool CircleBox(const Shape2D& C, const Shape2D& X, std::vector<Pt2D>& out) {
    const Vec2 p = X.rot.InvRotate(C.c - X.c);
    Vec2 q{std::clamp(p.x, -X.e.x, X.e.x), std::clamp(p.y, -X.e.y, X.e.y)};
    Vec2 nLocal{};
    float depth = 0.f;
    const Vec2 d = p - q;
    const float d2 = d.SqrMagnitude();
    if (d2 > 1e-12f) {
      const float dist = std::sqrt(d2);
      if (dist >= C.r) return false;
      nLocal = d * (1.f / dist);
      depth = C.r - dist;
    } else {
      const float dx = X.e.x - std::fabs(p.x), dy = X.e.y - std::fabs(p.y);
      if (dx <= dy) { nLocal = {p.x >= 0.f ? 1.f : -1.f, 0.f}; q.x = nLocal.x * X.e.x; depth = C.r + dx; }
      else { nLocal = {0.f, p.y >= 0.f ? 1.f : -1.f}; q.y = nLocal.y * X.e.y; depth = C.r + dy; }
    }
    out.push_back({X.c + X.rot.Rotate(q), -X.rot.Rotate(nLocal), depth});
    return true;
  }

  // SAT 2D: apenas 4 eixos (2 de A + 2 de B) ao inves de 15 eixos no 3D.
  static bool BoxBox2D(const Shape2D& A, const Shape2D& B, std::vector<Pt2D>& out) {
    const Vec2 Aax[2] = {{A.rot.c, A.rot.s}, {-A.rot.s, A.rot.c}};
    const Vec2 Bax[2] = {{B.rot.c, B.rot.s}, {-B.rot.s, B.rot.c}};
    const Vec2 T = B.c - A.c;
    float best = 1e30f;
    Vec2 bn{0.f, 1.f};
    auto test = [&](const Vec2& L) {
      const float ra = A.e.x * std::fabs(Dot(Aax[0], L)) + A.e.y * std::fabs(Dot(Aax[1], L));
      const float rb = B.e.x * std::fabs(Dot(Bax[0], L)) + B.e.y * std::fabs(Dot(Bax[1], L));
      const float ov = ra + rb - std::fabs(Dot(T, L));
      if (ov < 0.f) return false;
      if (ov < best) { best = ov; bn = Dot(T, L) >= 0.f ? L : -L; }
      return true;
    };
    if (!test(Aax[0]) || !test(Aax[1]) || !test(Bax[0]) || !test(Bax[1])) return false;

    const float raN = A.e.x * std::fabs(Dot(Aax[0], bn)) + A.e.y * std::fabs(Dot(Aax[1], bn));
    const float rbN = B.e.x * std::fabs(Dot(Bax[0], bn)) + B.e.y * std::fabs(Dot(Bax[1], bn));
    const size_t first = out.size();
    auto inside = [](const Shape2D& S, const Vec2& pt) {
      const Vec2 l = S.rot.InvRotate(pt - S.c);
      return std::fabs(l.x) <= S.e.x + 1e-3f && std::fabs(l.y) <= S.e.y + 1e-3f;
    };
    for (int s = 0; s < 4; ++s) {
      const Vec2 v = B.c + Bax[0] * ((s & 1) ? B.e.x : -B.e.x) + Bax[1] * ((s & 2) ? B.e.y : -B.e.y);
      if (inside(A, v)) {
        const float d = raN - Dot(v - A.c, bn);
        if (d > 0.f) out.push_back({v, bn, d});
      }
    }
    for (int s = 0; s < 4; ++s) {
      const Vec2 v = A.c + Aax[0] * ((s & 1) ? A.e.x : -A.e.x) + Aax[1] * ((s & 2) ? A.e.y : -A.e.y);
      if (inside(B, v)) {
        const float d = Dot(v - B.c, bn) + rbN;
        if (d > 0.f) out.push_back({v, bn, d});
      }
    }
    if (out.size() == first) out.push_back({(A.c + B.c) * 0.5f, bn, std::max(best, 1e-4f)});
    if (out.size() - first > 2) {
      std::sort(out.begin() + static_cast<long>(first), out.end(), [](const Pt2D& x, const Pt2D& y) { return x.depth > y.depth; });
      out.resize(first + 2);
    }
    return true;
  }

  static bool RayShape2D(const Shape2D& S, const Vec2& o, const Vec2& d, float maxT, float& tOut, Vec2& nOut) {
    if (S.circle) {
      const Vec2 oc = o - S.c;
      const float b = Dot(oc, d), c = Dot(oc, oc) - S.r * S.r, disc = b * b - c;
      if (disc < 0.f) return false;
      float t = -b - std::sqrt(disc);
      if (t < 0.f) { t = -b + std::sqrt(disc); if (t < 0.f) return false; }
      if (t > maxT) return false;
      tOut = t; nOut = Normalized(o + d * t - S.c);
      return true;
    }
    const Vec2 lo = S.rot.InvRotate(o - S.c);
    const Vec2 ld = S.rot.InvRotate(d);
    float t0 = 0.f, t1 = maxT;
    int axis = -1; float sgn = 1.f;
    auto slab = [&](float oi, float di, float ei, int ax) {
      if (std::fabs(di) < 1e-9f) return std::fabs(oi) <= ei;
      float a = (-ei - oi) / di, b = (ei - oi) / di, s = -1.f;
      if (a > b) { std::swap(a, b); s = 1.f; }
      if (a > t0) { t0 = a; axis = ax; sgn = s; }
      t1 = std::min(t1, b);
      return t0 <= t1;
    };
    if (!slab(lo.x, ld.x, S.e.x, 0) || !slab(lo.y, ld.y, S.e.y, 1)) return false;
    tOut = t0;
    const Vec2 nl = axis == 0 ? Vec2{sgn, 0.f} : (axis == 1 ? Vec2{0.f, sgn} : Vec2{0.f, 1.f});
    nOut = S.rot.Rotate(nl);
    return true;
  }

  std::vector<Entity> activeIndices_;
  std::vector<Shape2D> cache_;
  std::vector<AABB2D> boxes_;
  std::vector<Entity> sorted_;
  std::vector<std::pair<Entity, Entity>> pairs_;
  std::vector<Contact2D> contacts_;
  std::vector<PairEntry2D> curPairs_, prevPairs_;
  std::vector<WarmEntry2D> warmCache_, nextWarmCache_;
  std::vector<Transform2D> prevTransforms_;
  std::vector<CollisionEvent2D> events_;
  float acc_ = 0.f;
};

} // namespace Newtoon
