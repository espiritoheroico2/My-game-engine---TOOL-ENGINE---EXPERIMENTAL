// Newtoon Physics - PhysicsWorld 3D Otimizado:
// - Broad-phase BVH paralelizada por lotes sem alocacoes globais por thread
// - Narrow-phase (esfera/caixa SAT) paralelizada em todos os workers
// - Solver de impulsos sequenciais paralelizado via Graph Coloring (lock-free)
// - Integracao paralela, sono, CCD por raio, eventos Enter/Stay/Exit e raycast.
#pragma once
#include "newtoon_bvh.hpp"
#include "newtoon_jobs.hpp"

#include <algorithm>
#include <chrono>
#include <vector>

namespace Newtoon {

class PhysicsWorld {
 public:
  Vec3 gravity{0, -9.81f, 0};
  float fixedDt = 1.f / 60.f;
  int maxStepsPerFrame = 5;        // protecao contra a "espiral da morte" quando o quadro demora
  int solverIterations = 10;
  float baumgarte = 0.2f, slop = 0.005f, restitutionThreshold = 1.f;
  bool enableWarmStarting = true;  // reaproveita impulsos do passo anterior (estabilidade em pilhas)
  float warmStartFactor = 0.85f;
  bool enableIslands = true;       // agrupa corpos em ilhas de simulacao para sono coletivo
  bool enableInterpolation = true; // desative para economizar banda de memoria com 1M+ corpos
  bool enableSoftConstraints = true; // Soft Step (Box2D v3 / TGS Soft): estavel com razoes de massa altas
  float contactHertz = 30.f;
  float contactDampingRatio = 1.f;
  std::vector<PhysicsMaterial> materials{PhysicsMaterial{}}; // materialId indexa aqui

  struct Stats {
    int bodies = 0, awake = 0, pairs = 0, contacts = 0, steps = 0, idleSteps = 0, broadphaseBuilds = 0, broadphaseRefits = 0;
    int islands = 0, warmStartedContacts = 0;
    bool hitStepLimit = false;   // o quadro pediu mais passos que maxStepsPerFrame (o acumulador foi descartado)
    // Cores do solver no ultimo passo com contatos: quantas cores tem contatos e o tamanho do maior bucket.
    int colorsUsed = 0, largestColorBucket = 0;
    // Tempo de parede por fase, somado em todos os passos do quadro (ms). Nao inclui os passos ociosos.
    float integrateMs = 0, prepareMs = 0, broadphaseMs = 0, narrowphaseMs = 0, solverMs = 0, ccdMs = 0, finishMs = 0;
  } stats;

  // Fracao [0, 1] do acumulador entre o ultimo passo fisico e o proximo (para interpolacao de renderizacao).
  float Alpha() const noexcept { return fixedDt > 0.f ? std::clamp(acc_ / fixedDt, 0.f, 1.f) : 0.f; }

  // Gera poses interpoladas entre o estado anterior ao ultimo Step e o estado atual (para monitores 144Hz/240Hz+).
  void GetInterpolatedTransforms(const std::vector<Transform>& current, std::vector<Transform>& out) const {
    const size_t n = current.size();
    out.resize(n);
    const float a = Alpha();
    if (prevTransforms_.size() != n || a >= 1.f - 1e-5f) {
      out = current;
      return;
    }
    for (size_t i = 0; i < n; ++i) {
      out[i].position = Lerp(prevTransforms_[i].position, current[i].position, a);
      out[i].rotation = Quat::Nlerp(prevTransforms_[i].rotation, current[i].rotation, a);
      out[i].scale = current[i].scale;
    }
  }

  const std::vector<CollisionEvent>& GetCollisionEvents() const { return events_; }
  void ClearEvents() { events_.clear(); }

  void Simulate(std::vector<Transform>& tf, std::vector<Rigidbody>& rb, const std::vector<Collider>& col, float dt) {
    const size_t n = std::min({tf.size(), rb.size(), col.size()});
    stats = Stats{}; stats.bodies = static_cast<int>(n);
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
    if (steps == 0 && cache_.size() != n) {
      if (Prepare(tf, rb, col, n)) ++stats.broadphaseBuilds; else ++stats.broadphaseRefits;
    }
  }

  void Rebuild(const std::vector<Transform>& tf, const std::vector<Rigidbody>& rb, const std::vector<Collider>& col) {
    Prepare(tf, rb, col, std::min({tf.size(), rb.size(), col.size()}), true);
  }

  RaycastHit Raycast(const Vec3& origin, const Vec3& dirIn, float maxDistance, Entity ignore = kNoEntity) const {
    RaycastHit best; const Vec3 d = Normalized(dirIn); if (d.SqrMagnitude() < 0.5f) return best;
    float maxT = maxDistance;
    bvh_.RayQuery(origin, d, maxT, [&](Entity e, float& mt) {
      if (e == ignore || e >= cache_.size()) return;
      float t; Vec3 nrm;
      if (RayShape(cache_[e], origin, d, mt, t, nrm)) { mt = t; best.hit = true; best.distance = t; best.entity = e; best.normal = nrm; best.point = origin + d * t; }
    });
    return best;
  }

  size_t BodyCount() const { return cache_.size(); }

 private:
  struct Shape { Vec3 c; Mat3 R; Vec3 e; float r = 0; bool sphere = false; AABB box; bool valid = false; };
  struct ShapeInput { Transform transform; ShapeType type; Vec3 extents, offset, inverseInertia; float radius; bool dynamic; };
  struct Contact {
    Entity a, b;
    Vec3 p, n;
    float depth;
    float e, mu;
    Vec3 ra, rb;
    float normalMass = 0, tangentMass = 0, tangentMass2 = 0, bias = 0, impulseSoftScale = 0, jn = 0, jt1 = 0, jt2 = 0;
    Vec3 t1, t2;
  };
  struct PairInfo { Entity a = kNoEntity, b = kNoEntity; Vec3 p, n; float depth = 0, impulse = 0; bool trigger = false; };
  struct Pt { Vec3 p, n; float depth; };
  // Limite de pontos de contato por par (manifold) no caminho caixa-caixa/casco. Mantem os de MAIOR profundidade.
  // Padrao 8 (= comportamento anterior). Testado com 4: os testes de pilha/sono de newtoon_advanced falham
  // (pilha instavel, ilha nao dorme), entao NAO baixe para 4 sem revisar o solver. Menos pontos = menos linhas
  // no solver. Maximo permitido: 8 (tamanho do WarmManifold e do buffer narrowResults_).
  static constexpr size_t kMaxManifoldPoints = 8;

  std::vector<ShapeInput> shapeInputs_;

  static Shape MakeShape(const Transform& t, const Collider& c, const Rigidbody& r, bool active) {
    Shape s; s.valid = active;
    if (c.type == ShapeType::Sphere) {
      s.sphere = true; s.r = c.radius; s.e = {c.radius, c.radius, c.radius}; s.c = t.position;
      const bool rotateOffset = c.offset.SqrMagnitude() != 0.f;
      const bool anisotropicInertia = r.IsDynamic() &&
          (r.inverseInertiaLocal.x != r.inverseInertiaLocal.y || r.inverseInertiaLocal.y != r.inverseInertiaLocal.z);
      if (rotateOffset || anisotropicInertia) s.R = t.rotation.ToMat3();
      if (rotateOffset) s.c += s.R * c.offset;
      s.box = {s.c - s.e, s.c + s.e};
    } else {
      s.R = t.rotation.ToMat3(); s.c = t.position + s.R * c.offset;
      s.e = c.extents;
      const Vec3 ex{std::fabs(s.R.r[0].x) * s.e.x + std::fabs(s.R.r[0].y) * s.e.y + std::fabs(s.R.r[0].z) * s.e.z,
                    std::fabs(s.R.r[1].x) * s.e.x + std::fabs(s.R.r[1].y) * s.e.y + std::fabs(s.R.r[1].z) * s.e.z,
                    std::fabs(s.R.r[2].x) * s.e.x + std::fabs(s.R.r[2].y) * s.e.y + std::fabs(s.R.r[2].z) * s.e.z};
      s.box = {s.c - ex, s.c + ex};
    }
    return s;
  }

  static bool SameVec(const Vec3& a, const Vec3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
  static bool SameShapeInput(const ShapeInput& a, const Transform& t, const Rigidbody& r, const Collider& c) {
    return SameVec(a.transform.position, t.position) &&
        a.transform.rotation.x == t.rotation.x && a.transform.rotation.y == t.rotation.y &&
        a.transform.rotation.z == t.rotation.z && a.transform.rotation.w == t.rotation.w &&
        a.type == c.type && SameVec(a.extents, c.extents) && SameVec(a.offset, c.offset) &&
        a.radius == c.radius && SameVec(a.inverseInertia, r.inverseInertiaLocal) && a.dynamic == r.IsDynamic();
  }

  bool ShapeInputsUnchanged(const std::vector<Transform>& tf, const std::vector<Rigidbody>& rb,
                            const std::vector<Collider>& col, size_t n) const {
    if (shapeInputs_.size() != n) return false;
    for (size_t i = 0; i < n; ++i) if (!SameShapeInput(shapeInputs_[i], tf[i], rb[i], col[i])) return false;
    return true;
  }

  bool Prepare(const std::vector<Transform>& tf, const std::vector<Rigidbody>& rb, const std::vector<Collider>& col, size_t n, bool forceFull = false) {
    cache_.resize(n); boxes_.resize(n); valid_.assign(n, 1); shapeInputs_.resize(n);
    JobSystem::Get().ParallelFor(n, 512, [&](size_t b, size_t e) {
      for (size_t i = b; i < e; ++i) {
        cache_[i] = MakeShape(tf[i], col[i], rb[i], true);
        boxes_[i] = cache_[i].box;
        shapeInputs_[i] = {tf[i], col[i].type, col[i].extents, col[i].offset, rb[i].inverseInertiaLocal, col[i].radius, rb[i].IsDynamic()};
      }
    });
    constexpr std::size_t kRefitRebuildInterval = 60;
    const bool full = forceFull || bvh_.Empty() || bvhBodyCount_ != n || refitSteps_ >= kRefitRebuildInterval;
    if (full || !bvh_.Refit(boxes_, valid_)) {
      bvh_.Build(boxes_, valid_); bvhBodyCount_ = n; refitSteps_ = 0; return true;
    }
    ++refitSteps_;
    return false;
  }

  // ------------------------------------------------------------------ passo fixo otimizado
  void Step(std::vector<Transform>& tf, std::vector<Rigidbody>& rb, const std::vector<Collider>& col, size_t n, float dt) {
    auto active = [&](size_t i) {
      return (rb[i].IsDynamic() && !rb[i].isSleeping) || (rb[i].isKinematic && rb[i].velocity.SqrMagnitude() > 1e-8f);
    };

    activeIndices_.clear();
    for (size_t i = 0; i < n; ++i) {
      if (active(i)) activeIndices_.push_back(static_cast<Entity>(i));
    }
    const bool hasActiveBody = !activeIndices_.empty();
    if (!hasActiveBody && !ShapeInputsUnchanged(tf, rb, col, n)) {
      if (Prepare(tf, rb, col, n)) ++stats.broadphaseBuilds; else ++stats.broadphaseRefits;
    }
    if (!hasActiveBody) {
      curPairs_.clear();
      for (const auto& kv : prevPairs_)
        events_.push_back({kv.info.a, kv.info.b, {}, {}, 0.f, CollisionType::Exit, kv.info.trigger});
      prevPairs_.clear();
      ++stats.idleSteps;
      TrimEvents();
      return;
    }

    // Cronometro das fases: lap(campo) soma o tempo desde a marca anterior e reinicia a marca.
    auto mark = std::chrono::steady_clock::now();
    auto lap = [&mark](float& acc) {
      const auto now = std::chrono::steady_clock::now();
      acc += std::chrono::duration<float, std::milli>(now - mark).count();
      mark = now;
    };

    // 1) Velocidades (paralelo, apenas corpos ativos): gravidade, forcas, arrasto
    const size_t activeCount = activeIndices_.size();
    JobSystem::Get().ParallelFor(activeCount, 512, [&](size_t b, size_t e) {
      for (size_t idx = b; idx < e; ++idx) {
        const Entity i = activeIndices_[idx];
        Rigidbody& r = rb[i];
        r.grounded = false;
        if (!r.IsDynamic()) { r.forceAccumulator = r.torqueAccumulator = {}; continue; }
        const Vec3 g = r.overrideGravity ? r.gravity : gravity;
        if (r.useGravity) r.velocity += g * (r.gravityScale * dt);
        r.velocity += r.forceAccumulator * (r.inverseMass * dt);
        if (!r.freezeRotation && r.torqueAccumulator.SqrMagnitude() != 0.f)
          r.angularVelocity += InvInertiaWorld(r, tf[i].rotation.ToMat3()) * r.torqueAccumulator * dt;
        r.forceAccumulator = r.torqueAccumulator = {};
        r.velocity *= 1.f / (1.f + r.linearDamping * dt);
        r.angularVelocity *= 1.f / (1.f + r.angularDamping * dt);
        if (r.freezeRotation) r.angularVelocity = {};
      }
    });

    lap(stats.integrateMs);

    // 2) Formas + BVH
    if (Prepare(tf, rb, col, n)) ++stats.broadphaseBuilds; else ++stats.broadphaseRefits;
    lap(stats.prepareMs);

    // 3) Broad-phase PARALELA (apenas corpos ativos consultam a BVH)
    constexpr size_t kBroadGrain = 128;
    const size_t numBroadChunks = activeCount > 0 ? (activeCount + kBroadGrain - 1) / kBroadGrain : 0;
    chunkPairs_.resize(numBroadChunks);
    for (size_t c = 0; c < numBroadChunks; ++c) chunkPairs_[c].clear();

    JobSystem::Get().ParallelFor(numBroadChunks, 1, [&](size_t cb, size_t ce) {
      for (size_t c = cb; c < ce; ++c) {
        auto& localPairs = chunkPairs_[c];
        const size_t b = c * kBroadGrain;
        const size_t e = std::min(activeCount, b + kBroadGrain);
        for (size_t idx = b; idx < e; ++idx) {
          const Entity i = activeIndices_[idx];
          bvh_.Query(boxes_[i], [&](Entity j) {
            if (j == i) return;
            if (j < i && active(j)) return; // par ja visto do outro lado
            if (rb[i].IsStatic() && rb[j].IsStatic()) return;
            localPairs.push_back({i, j});
          });
        }
      }
    });

    pairs_.clear();
    size_t totalPairs = 0;
    for (size_t c = 0; c < numBroadChunks; ++c) totalPairs += chunkPairs_[c].size();
    pairs_.reserve(totalPairs);
    for (size_t c = 0; c < numBroadChunks; ++c) {
      pairs_.insert(pairs_.end(), chunkPairs_[c].begin(), chunkPairs_[c].end());
    }
    stats.pairs += static_cast<int>(pairs_.size());
    lap(stats.broadphaseMs);

    // 4) Narrow-phase PARALELA (zero alocacoes por par: buffer inline de 8 pontos)
    const size_t pairCount = pairs_.size();
    narrowResults_.resize(pairCount);
    JobSystem::Get().ParallelFor(pairCount, 128, [&](size_t b, size_t e) {
      std::vector<Pt> localPts;
      localPts.reserve(8);
      for (size_t idx = b; idx < e; ++idx) {
        auto& res = narrowResults_[idx];
        res.count = 0;
        const Entity a = pairs_[idx].first, bEnt = pairs_[idx].second;
        if (!Collide(cache_[a], cache_[bEnt], localPts)) continue;
        const uint8_t cnt = static_cast<uint8_t>(std::min<size_t>(localPts.size(), 8));
        res.count = cnt;
        for (uint8_t k = 0; k < cnt; ++k) res.pts[k] = localPts[k];
        localPts.clear();
      }
    });

    // Consolidacao sequencial dos contatos e acordar corpos dormindo
    contacts_.clear();
    curPairs_.clear();
    curPairs_.reserve(pairCount);

    for (size_t idx = 0; idx < pairCount; ++idx) {
      const auto& res = narrowResults_[idx];
      if (res.count == 0) continue;

      const Entity a = pairs_[idx].first, b = pairs_[idx].second;
      const bool trig = col[a].isTrigger || col[b].isTrigger;
      PairInfo info;
      info.a = a; info.b = b; info.trigger = trig;
      info.p = res.pts[0].p; info.n = res.pts[0].n; info.depth = res.pts[0].depth;

      if (!trig) {
        const PhysicsMaterial& ma = Mat(col[a].materialId);
        const PhysicsMaterial& mb = Mat(col[b].materialId);
        const float mu = Combine(ma.frictionCombine, ma.friction, mb.friction);
        const float bounce = Combine(ma.bounceCombine, ma.bounciness, mb.bounciness);
        for (uint8_t kIdx = 0; kIdx < res.count; ++kIdx) {
          const Pt& c = res.pts[kIdx];
          Contact k{};
          k.a = a; k.b = b; k.p = c.p; k.n = c.n; k.depth = c.depth; k.e = bounce; k.mu = mu;
          contacts_.push_back(k);
        }
        if (rb[a].isSleeping && active(b)) rb[a].WakeUp();
        if (rb[b].isSleeping && active(a)) rb[b].WakeUp();
        if (c0Up(res.pts[0].n) > 0.7f) { rb[b].grounded = true; rb[b].groundNormal = res.pts[0].n; }
        if (c0Up(res.pts[0].n) < -0.7f) { rb[a].grounded = true; rb[a].groundNormal = -res.pts[0].n; }
      }
      curPairs_.push_back({Key(a, b), info});
    }
    std::sort(curPairs_.begin(), curPairs_.end(), [](const PairEntry& x, const PairEntry& y) { return x.key < y.key; });
    stats.contacts += static_cast<int>(contacts_.size());
    lap(stats.narrowphaseMs);

    // 5) Solver PARALELO via Graph Coloring (sem mutexes nem condicoes de corrida)
    ++frameStamp_;
    if (frameStamp_ == 0) {
      invIwStamp_.assign(n, 0);
      islandStamp_.assign(n, 0);
      frameStamp_ = 1;
    }
    if (!contacts_.empty()) {
      if (invIw_.size() < n) { invIw_.resize(n); invIwStamp_.resize(n, 0); }
      auto prepareInertia = [&](Entity e) {
        if (invIwStamp_[e] == frameStamp_) return;
        invIw_[e] = InvInertiaWorld(rb[e], cache_[e].R);
        invIwStamp_[e] = frameStamp_;
      };
      for (const Contact& c : contacts_) { prepareInertia(c.a); prepareInertia(c.b); }

      PreSolve(tf, rb, dt);
      if (enableWarmStarting) ApplyWarmStart(rb);

      // Particiona os contatos em cores independentes
      colorGraph_.Build(contacts_, rb, n);
      stats.colorsUsed = 0; stats.largestColorBucket = 0;
      for (const auto& bucket : colorGraph_.buckets) {
        if (bucket.empty()) continue;
        ++stats.colorsUsed;
        stats.largestColorBucket = std::max(stats.largestColorBucket, static_cast<int>(bucket.size()));
      }

      for (int it = 0; it < solverIterations; ++it) {
        SolveContactsColored(rb);
      }

      nextWarmCache_.clear();
      nextWarmCache_.reserve(curPairs_.size());
      size_t pCursor = 0;
      for (const auto& c : contacts_) {
        const uint64_t k = Key(c.a, c.b);
        while (pCursor < curPairs_.size() && curPairs_[pCursor].key < k) ++pCursor;
        if (pCursor < curPairs_.size() && curPairs_[pCursor].key == k) curPairs_[pCursor].info.impulse += c.jn;
        else {
          auto it = std::lower_bound(curPairs_.begin(), curPairs_.end(), k, [](const PairEntry& e, uint64_t key) { return e.key < key; });
          if (it != curPairs_.end() && it->key == k) it->info.impulse += c.jn;
        }
        if (enableWarmStarting && c.jn > 1e-6f) {
          if (nextWarmCache_.empty() || nextWarmCache_.back().key != k) {
            nextWarmCache_.push_back({k, {}});
          }
          auto& wm = nextWarmCache_.back().manifold;
          if (wm.count < 8) wm.pts[wm.count++] = {c.p, c.n, c.jn, c.jt1, c.jt2};
        }
      }
      std::sort(nextWarmCache_.begin(), nextWarmCache_.end(), [](const WarmEntry& x, const WarmEntry& y) { return x.key < y.key; });
      warmCache_.swap(nextWarmCache_);
    } else {
      warmCache_.clear();
    }

    lap(stats.solverMs);

    // 6) CCD (apenas corpos ativos que pediram CCD)
    for (size_t idx = 0; idx < activeCount; ++idx) {
      const Entity i = activeIndices_[idx];
      Rigidbody& r = rb[i];
      if (!r.useCCD || !r.IsDynamic() || r.isSleeping) continue;
      const float speed = r.velocity.Magnitude();
      const float reach = speed * dt;
      const float size = cache_[i].sphere ? cache_[i].r : std::min({cache_[i].e.x, cache_[i].e.y, cache_[i].e.z});
      if (reach <= size) continue;
      const RaycastHit h = Raycast(cache_[i].c, r.velocity, reach + size, i);
      if (h.hit && !col[h.entity].isTrigger) {
        const float allow = std::max(0.f, h.distance - size);
        tf[i].position += Normalized(r.velocity) * std::min(allow, reach) - r.velocity * dt;
      }
    }

    lap(stats.ccdMs);

    // 7) Posicoes e rotacoes (paralelo, apenas corpos ativos)
    JobSystem::Get().ParallelFor(activeCount, 512, [&](size_t b, size_t e) {
      for (size_t idx = b; idx < e; ++idx) {
        const Entity i = activeIndices_[idx];
        Rigidbody& r = rb[i];
        if (r.IsStatic() || (r.isSleeping && !r.isKinematic)) continue;
        tf[i].position += r.velocity * dt;
        const Vec3 w = r.angularVelocity;
        if (w.SqrMagnitude() > 1e-12f) {
          const Quat dq{w.x * dt * 0.5f, w.y * dt * 0.5f, w.z * dt * 0.5f, 0.f};
          Quat q = dq * tf[i].rotation;
          tf[i].rotation = Quat{tf[i].rotation.x + q.x, tf[i].rotation.y + q.y, tf[i].rotation.z + q.z, tf[i].rotation.w + q.w}.Normalized();
        }
      }
    });

    // 8) Sono + Ilhas de Simulacao (O(ativos + pares) via stamp)
    if (!enableIslands || n == 0) {
      for (size_t idx = 0; idx < activeCount; ++idx) {
        const Entity i = activeIndices_[idx];
        Rigidbody& r = rb[i];
        if (!r.IsDynamic() || r.isSleeping) { if (!r.isSleeping) ++stats.awake; continue; }
        if (r.velocity.Magnitude() < r.sleepThreshold && r.angularVelocity.Magnitude() < r.sleepThreshold) {
          r.sleepTimer += dt;
          if (r.sleepTimer > 0.5f) { r.isSleeping = true; r.velocity = r.angularVelocity = {}; }
        } else {
          r.sleepTimer = 0.f;
        }
        if (!r.isSleeping) ++stats.awake;
      }
    } else {
      UpdateIslandsAndSleep(rb, n, dt);
    }

    // 9) Eventos Enter/Stay/Exit por two-pointer merge O(P)
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
    lap(stats.finishMs);
  }

  static float c0Up(const Vec3& n) { return n.y; }
  static uint64_t Key(Entity a, Entity b) { if (a > b) std::swap(a, b); return (static_cast<uint64_t>(a) << 32) | b; }
  const PhysicsMaterial& Mat(uint16_t id) const { return materials[id < materials.size() ? id : 0]; }
  void TrimEvents() {
    if (events_.size() > 20000) events_.erase(events_.begin(), events_.begin() + 10000);
  }

  static Mat3 InvInertiaWorld(const Rigidbody& r, const Mat3& R) {
    Mat3 out; if (!r.IsDynamic()) { out.r[0] = out.r[1] = out.r[2] = {}; return out; }
    const float d[3] = {r.inverseInertiaLocal.x, r.inverseInertiaLocal.y, r.inverseInertiaLocal.z};
    float m[3][3];
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) { float s = 0; for (int k = 0; k < 3; ++k) s += At(R.r[i], k) * d[k] * At(R.r[j], k); m[i][j] = s; }
    for (int i = 0; i < 3; ++i) out.r[i] = {m[i][0], m[i][1], m[i][2]};
    return out;
  }

  // ------------------------------------------------------------------ narrow-phase
  bool Collide(const Shape& A, const Shape& B, std::vector<Pt>& out) const {
    if (A.sphere && B.sphere) return SphereSphere(A, B, out);
    if (A.sphere) return SphereBox(A, B, out);
    if (B.sphere) {
      const size_t first = out.size();
      if (!SphereBox(B, A, out)) return false;
      for (size_t i = first; i < out.size(); ++i) out[i].n = -out[i].n;
      return true;
    }
    return BoxBox(A, B, out);
  }

  static bool SphereSphere(const Shape& A, const Shape& B, std::vector<Pt>& out) {
    const Vec3 d = B.c - A.c; const float dist = d.Magnitude(), rr = A.r + B.r;
    if (dist >= rr) return false;
    const Vec3 n = dist > 1e-6f ? d * (1.f / dist) : Vec3{0, 1, 0};
    out.push_back({A.c + n * (A.r - (rr - dist) * 0.5f), n, rr - dist}); return true;
  }

  static bool SphereBox(const Shape& S, const Shape& X, std::vector<Pt>& out) {
    const Vec3 rel = S.c - X.c; const Vec3 p = X.R.Transposed() * rel;
    Vec3 q{std::clamp(p.x, -X.e.x, X.e.x), std::clamp(p.y, -X.e.y, X.e.y), std::clamp(p.z, -X.e.z, X.e.z)};
    Vec3 nLocal; float depth;
    const Vec3 d = p - q; const float dist = d.Magnitude();
    if (dist > 1e-6f) { if (dist >= S.r) return false; nLocal = d * (1.f / dist); depth = S.r - dist; }
    else {
      const float dx = X.e.x - std::fabs(p.x), dy = X.e.y - std::fabs(p.y), dz = X.e.z - std::fabs(p.z);
      if (dx <= dy && dx <= dz) { nLocal = {p.x >= 0 ? 1.f : -1.f, 0, 0}; q.x = nLocal.x * X.e.x; depth = S.r + dx; }
      else if (dy <= dz) { nLocal = {0, p.y >= 0 ? 1.f : -1.f, 0}; q.y = nLocal.y * X.e.y; depth = S.r + dy; }
      else { nLocal = {0, 0, p.z >= 0 ? 1.f : -1.f}; q.z = nLocal.z * X.e.z; depth = S.r + dz; }
    }
    const Vec3 nBoxToSphere = X.R * nLocal;
    out.push_back({X.c + X.R * q, -nBoxToSphere, depth});
    return true;
  }

  static bool BoxBox(const Shape& A, const Shape& B, std::vector<Pt>& out) {
    const Vec3 Aax[3] = {A.R.Col(0), A.R.Col(1), A.R.Col(2)}, Bax[3] = {B.R.Col(0), B.R.Col(1), B.R.Col(2)};
    const float ae[3] = {A.e.x, A.e.y, A.e.z}, be[3] = {B.e.x, B.e.y, B.e.z};
    const Vec3 T = B.c - A.c;
    float best = 1e30f; Vec3 bn; int kind = 0, ia = 0, ib = 0;
    auto test = [&](Vec3 L, bool edge, int i, int j) {
      const float len = L.Magnitude(); if (len < 1e-6f) return true; L = L * (1.f / len);
      float ra = 0, rb = 0; for (int k = 0; k < 3; ++k) { ra += ae[k] * std::fabs(Dot(Aax[k], L)); rb += be[k] * std::fabs(Dot(Bax[k], L)); }
      const float dist = std::fabs(Dot(T, L)), ov = ra + rb - dist;
      if (ov < 0.f) return false;
      const float score = edge ? ov * 1.05f + 1e-4f : ov;
      if (score < best) { best = score; bn = Dot(T, L) >= 0.f ? L : -L; kind = edge ? 1 : 0; ia = i; ib = j; }
      return true;
    };
    for (int i = 0; i < 3; ++i) if (!test(Aax[i], false, i, 0)) return false;
    for (int i = 0; i < 3; ++i) if (!test(Bax[i], false, 0, i)) return false;
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) if (!test(Cross(Aax[i], Bax[j]), true, i, j)) return false;
    float raN = 0, rbN = 0; for (int k = 0; k < 3; ++k) { raN += ae[k] * std::fabs(Dot(Aax[k], bn)); rbN += be[k] * std::fabs(Dot(Bax[k], bn)); }
    const size_t first = out.size();
    auto inside = [&](const Shape& S, const Vec3& pt) { const Vec3 l = S.R.Transposed() * (pt - S.c); return std::fabs(l.x) <= S.e.x + 1e-3f && std::fabs(l.y) <= S.e.y + 1e-3f && std::fabs(l.z) <= S.e.z + 1e-3f; };
    for (int s = 0; s < 8; ++s) {
      const Vec3 v = B.c + Bax[0] * ((s & 1) ? be[0] : -be[0]) + Bax[1] * ((s & 2) ? be[1] : -be[1]) + Bax[2] * ((s & 4) ? be[2] : -be[2]);
      if (!inside(A, v)) continue;
      const float d = raN - Dot(v - A.c, bn); if (d > 0.f) out.push_back({v, bn, d});
    }
    for (int s = 0; s < 8; ++s) {
      const Vec3 v = A.c + Aax[0] * ((s & 1) ? ae[0] : -ae[0]) + Aax[1] * ((s & 2) ? ae[1] : -ae[1]) + Aax[2] * ((s & 4) ? ae[2] : -ae[2]);
      if (!inside(B, v)) continue;
      const float d = Dot(v - B.c, bn) + rbN; if (d > 0.f) out.push_back({v, bn, d});
    }
    if (out.size() == first) {
      Vec3 pa = A.c, pb = B.c;
      for (int k = 0; k < 3; ++k) { if (kind == 1 && k == ia) continue; pa += Aax[k] * (Dot(Aax[k], bn) > 0 ? ae[k] : -ae[k]); }
      for (int k = 0; k < 3; ++k) { if (kind == 1 && k == ib) continue; pb += Bax[k] * (Dot(Bax[k], bn) > 0 ? -be[k] : be[k]); }
      Vec3 mid = (pa + pb) * 0.5f;
      if (kind == 1) {
        const Vec3 d1 = Aax[ia], d2 = Bax[ib], r = pa - pb;
        const float a = Dot(d1, d1), e2 = Dot(d2, d2), b2 = Dot(d1, d2), c = Dot(d1, r), f = Dot(d2, r), den = a * e2 - b2 * b2;
        if (std::fabs(den) > 1e-8f) { const float s = (b2 * f - c * e2) / den, t = (a * f - b2 * c) / den; mid = ((pa + d1 * s) + (pb + d2 * t)) * 0.5f; }
      }
      out.push_back({mid, bn, std::max(best, 1e-4f)});
    }
    if (out.size() - first > kMaxManifoldPoints) {
      // Fica so com os kMaxManifoldPoints mais profundos (stable_sort: empates mantem a ordem -> deterministico).
      std::stable_sort(out.begin() + static_cast<long>(first), out.end(), [](const Pt& x, const Pt& y) { return x.depth > y.depth; });
      out.resize(first + kMaxManifoldPoints);
    }
    return true;
  }

  static bool RayShape(const Shape& S, const Vec3& o, const Vec3& d, float maxT, float& tOut, Vec3& nOut) {
    if (S.sphere) {
      const Vec3 oc = o - S.c; const float b = Dot(oc, d), c = Dot(oc, oc) - S.r * S.r, disc = b * b - c; if (disc < 0.f) return false;
      float t = -b - std::sqrt(disc); if (t < 0.f) { t = -b + std::sqrt(disc); if (t < 0.f) return false; }
      if (t > maxT) return false;
      tOut = t; nOut = Normalized(o + d * t - S.c); return true;
    }
    const Mat3 Rt = S.R.Transposed(); const Vec3 lo = Rt * (o - S.c), ld = Rt * d;
    float t0 = 0.f, t1 = maxT; int axis = -1; float sgn = 1.f;
    for (int i = 0; i < 3; ++i) {
      const float oi = At(lo, i), di = At(ld, i), ei = At(S.e, i);
      if (std::fabs(di) < 1e-9f) { if (std::fabs(oi) > ei) return false; continue; }
      float a = (-ei - oi) / di, b = (ei - oi) / di, s = -1.f; if (a > b) { std::swap(a, b); s = 1.f; }
      if (a > t0) { t0 = a; axis = i; sgn = s; } t1 = std::min(t1, b); if (t0 > t1) return false;
    }
    tOut = t0; Vec3 nl{}; if (axis == 0) nl.x = sgn; else if (axis == 1) nl.y = sgn; else if (axis == 2) nl.z = sgn; else nl = {0, 1, 0};
    nOut = S.R * nl; return true;
  }

  // ------------------------------------------------------------------ solver paralelo
  void PreSolve(const std::vector<Transform>& tf, std::vector<Rigidbody>& rb, float dt) {
    JobSystem::Get().ParallelFor(contacts_.size(), 256, [&](size_t b, size_t e) {
      for (size_t i = b; i < e; ++i) {
        auto& c = contacts_[i];
        const Rigidbody& A = rb[c.a];
        const Rigidbody& B = rb[c.b];
        c.ra = c.p - tf[c.a].position;
        c.rb = c.p - tf[c.b].position;
        const Mat3& IA = invIw_[c.a];
        const Mat3& IB = invIw_[c.b];
        auto eff = [&](const Vec3& dir) {
          const Vec3 rna = Cross(c.ra, dir), rnb = Cross(c.rb, dir);
          return A.inverseMass + B.inverseMass + Dot(dir, Cross(IA * rna, c.ra) + Cross(IB * rnb, c.rb));
        };
        const float k = eff(c.n);
        c.normalMass = k > 1e-9f ? 1.f / k : 0.f;
        const Vec3 vrel = (B.velocity + Cross(B.angularVelocity, c.rb)) - (A.velocity + Cross(A.angularVelocity, c.ra));
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
        Vec3 t = vrel - c.n * vn;
        t = t.SqrMagnitude() > 1e-8f ? Normalized(t) : (std::fabs(c.n.x) > 0.9f ? Normalized(Cross(c.n, Vec3{0, 1, 0})) : Normalized(Cross(c.n, Vec3{1, 0, 0})));
        c.t1 = t;
        c.t2 = Normalized(Cross(c.n, t));
        const float k1 = eff(c.t1), k2 = eff(c.t2);
        c.tangentMass = k1 > 1e-9f ? 1.f / k1 : 0.f;
        c.tangentMass2 = k2 > 1e-9f ? 1.f / k2 : 0.f;
        c.jn = c.jt1 = c.jt2 = 0.f;
        if (enableWarmStarting && !warmCache_.empty()) {
          const uint64_t pairKey = Key(c.a, c.b);
          const auto it = std::lower_bound(warmCache_.begin(), warmCache_.end(), pairKey, [](const WarmEntry& e, uint64_t key) { return e.key < key; });
          if (it != warmCache_.end() && it->key == pairKey) {
            const WarmManifold& wm = it->manifold;
            for (uint8_t m = 0; m < wm.count; ++m) {
              if ((c.p - wm.pts[m].p).SqrMagnitude() < 0.04f && Dot(c.n, wm.pts[m].n) > 0.9f) {
                c.jn = wm.pts[m].jn * warmStartFactor;
                c.jt1 = wm.pts[m].jt1 * warmStartFactor;
                c.jt2 = wm.pts[m].jt2 * warmStartFactor;
                break;
              }
            }
          }
        }
      }
    });
  }

  void ApplyWarmStart(std::vector<Rigidbody>& rb) {
    int warmCount = 0;
    for (const auto& c : contacts_) {
      if (c.jn <= 0.f && std::fabs(c.jt1) <= 0.f && std::fabs(c.jt2) <= 0.f) continue;
      ++warmCount;
      const Vec3 P = c.n * c.jn + c.t1 * c.jt1 + c.t2 * c.jt2;
      Rigidbody& A = rb[c.a];
      Rigidbody& B = rb[c.b];
      if (A.IsDynamic()) { A.velocity -= P * A.inverseMass; A.angularVelocity -= invIw_[c.a] * Cross(c.ra, P); }
      if (B.IsDynamic()) { B.velocity += P * B.inverseMass; B.angularVelocity += invIw_[c.b] * Cross(c.rb, P); }
    }
    stats.warmStartedContacts += warmCount;
  }

  void UpdateIslandsAndSleep(std::vector<Rigidbody>& rb, size_t n, float dt) {
    if (islandParent_.size() < n) {
      islandParent_.resize(n);
      islandRank_.resize(n, 0);
      islandAwake_.resize(n, 0);
      islandSeen_.resize(n, 0);
      islandStamp_.resize(n, 0);
    }
    auto ensureNode = [&](Entity x) {
      if (islandStamp_[x] != frameStamp_) {
        islandStamp_[x] = frameStamp_;
        islandParent_[x] = x;
        islandRank_[x] = 0;
        islandAwake_[x] = 0;
        islandSeen_[x] = 0;
      }
    };
    auto findRoot = [&](Entity x) {
      ensureNode(x);
      while (islandParent_[x] != x) {
        islandParent_[x] = islandParent_[islandParent_[x]];
        x = islandParent_[x];
      }
      return x;
    };
    auto unite = [&](Entity a, Entity b) {
      Entity ra = findRoot(a), rbRoot = findRoot(b);
      if (ra == rbRoot) return;
      if (islandRank_[ra] < islandRank_[rbRoot]) std::swap(ra, rbRoot);
      islandParent_[rbRoot] = ra;
      if (islandRank_[ra] == islandRank_[rbRoot]) ++islandRank_[ra];
    };

    for (const auto& kv : curPairs_) {
      if (kv.info.trigger) continue;
      const Entity a = kv.info.a, b = kv.info.b;
      if (rb[a].IsDynamic() && rb[b].IsDynamic()) unite(a, b);
    }

    for (Entity i : activeIndices_) {
      Rigidbody& r = rb[i];
      if (!r.IsDynamic()) continue;
      const Entity root = findRoot(i);
      if (!r.isSleeping) {
        if (r.velocity.Magnitude() < r.sleepThreshold && r.angularVelocity.Magnitude() < r.sleepThreshold) {
          r.sleepTimer += dt;
          if (r.sleepTimer <= 0.5f) islandAwake_[root] = 1;
        } else {
          r.sleepTimer = 0.f;
          islandAwake_[root] = 1;
        }
      }
    }

    int islandCount = 0;
    for (Entity i : activeIndices_) {
      Rigidbody& r = rb[i];
      if (!r.IsDynamic()) continue;
      const Entity root = findRoot(i);
      if (!islandSeen_[root]) { islandSeen_[root] = 1; ++islandCount; }
      if (islandAwake_[root]) {
        r.isSleeping = false;
        ++stats.awake;
      } else {
        r.isSleeping = true;
        r.velocity = r.angularVelocity = {};
      }
    }
    stats.islands = islandCount;
  }

  void SolveSingleContact(Contact& c, std::vector<Rigidbody>& rb) {
    Rigidbody& A = rb[c.a];
    Rigidbody& B = rb[c.b];
    auto vrel = [&] { return (B.velocity + Cross(B.angularVelocity, c.rb)) - (A.velocity + Cross(A.angularVelocity, c.ra)); };
    // So escreve em corpos dinamicos: estaticos/cinematicos nao sao escritos, evitando corrida de dados entre threads.
    const bool dynA = A.IsDynamic(), dynB = B.IsDynamic();
    auto apply = [&](const Vec3& P) {
      if (dynA) { A.velocity -= P * A.inverseMass; A.angularVelocity -= invIw_[c.a] * Cross(c.ra, P); }
      if (dynB) { B.velocity += P * B.inverseMass; B.angularVelocity += invIw_[c.b] * Cross(c.rb, P); }
    };
    {
      const float vn = Dot(vrel(), c.n);
      float dj = c.normalMass * (-vn + c.bias) - c.impulseSoftScale * c.jn;
      const float old = c.jn;
      c.jn = std::max(0.f, old + dj);
      dj = c.jn - old;
      apply(c.n * dj);
    }
    const float maxF = c.mu * c.jn;
    {
      const float vt = Dot(vrel(), c.t1);
      float dj = -c.tangentMass * vt;
      const float old = c.jt1;
      c.jt1 = std::clamp(old + dj, -maxF, maxF);
      dj = c.jt1 - old;
      apply(c.t1 * dj);
    }
    {
      const float vt = Dot(vrel(), c.t2);
      float dj = -c.tangentMass2 * vt;
      const float old = c.jt2;
      c.jt2 = std::clamp(old + dj, -maxF, maxF);
      dj = c.jt2 - old;
      apply(c.t2 * dj);
    }
  }

#ifdef NEWTOON_SIMD_SSE2
  // Wide-SIMD: resolve 4 contatos independentes da mesma cor simultaneamente nos registradores SSE2.
  void Solve4ContactsWide(uint32_t i0, uint32_t i1, uint32_t i2, uint32_t i3, std::vector<Rigidbody>& rb) {
    Contact* cs[4] = {&contacts_[i0], &contacts_[i1], &contacts_[i2], &contacts_[i3]};
    auto computeVn = [&](const Vec3& dir0, const Vec3& dir1, const Vec3& dir2, const Vec3& dir3) {
      const Vec3* dirs[4] = {&dir0, &dir1, &dir2, &dir3};
      float v[4];
      for (int lane = 0; lane < 4; ++lane) {
        const Contact& c = *cs[lane];
        const Rigidbody& A = rb[c.a];
        const Rigidbody& B = rb[c.b];
        const Vec3 vrel = (B.velocity + Cross(B.angularVelocity, c.rb)) - (A.velocity + Cross(A.angularVelocity, c.ra));
        v[lane] = Dot(vrel, *dirs[lane]);
      }
      return _mm_loadu_ps(v);
    };
    auto apply4 = [&](const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3) {
      const Vec3* ps[4] = {&p0, &p1, &p2, &p3};
      for (int lane = 0; lane < 4; ++lane) {
        const Contact& c = *cs[lane];
        const Vec3& P = *ps[lane];
        Rigidbody& A = rb[c.a];
        Rigidbody& B = rb[c.b];
        if (A.IsDynamic()) { A.velocity -= P * A.inverseMass; A.angularVelocity -= invIw_[c.a] * Cross(c.ra, P); }
        if (B.IsDynamic()) { B.velocity += P * B.inverseMass; B.angularVelocity += invIw_[c.b] * Cross(c.rb, P); }
      }
    };

    const __m128 zero = _mm_setzero_ps();
    // 1) Normal impulse (4 contatos em paralelo em SSE2)
    {
      const __m128 vn = computeVn(cs[0]->n, cs[1]->n, cs[2]->n, cs[3]->n);
      const __m128 nm = _mm_set_ps(cs[3]->normalMass, cs[2]->normalMass, cs[1]->normalMass, cs[0]->normalMass);
      const __m128 bias = _mm_set_ps(cs[3]->bias, cs[2]->bias, cs[1]->bias, cs[0]->bias);
      const __m128 soft = _mm_set_ps(cs[3]->impulseSoftScale, cs[2]->impulseSoftScale, cs[1]->impulseSoftScale, cs[0]->impulseSoftScale);
      const __m128 oldJn = _mm_set_ps(cs[3]->jn, cs[2]->jn, cs[1]->jn, cs[0]->jn);
      const __m128 dj = _mm_sub_ps(_mm_mul_ps(nm, _mm_sub_ps(bias, vn)), _mm_mul_ps(soft, oldJn));
      const __m128 newJn = _mm_max_ps(zero, _mm_add_ps(oldJn, dj));
      const __m128 delta = _mm_sub_ps(newJn, oldJn);
      alignas(16) float jnArr[4], dArr[4];
      _mm_store_ps(jnArr, newJn);
      _mm_store_ps(dArr, delta);
      for (int lane = 0; lane < 4; ++lane) cs[lane]->jn = jnArr[lane];
      apply4(cs[0]->n * dArr[0], cs[1]->n * dArr[1], cs[2]->n * dArr[2], cs[3]->n * dArr[3]);
    }

    const __m128 maxF = _mm_set_ps(cs[3]->mu * cs[3]->jn, cs[2]->mu * cs[2]->jn, cs[1]->mu * cs[1]->jn, cs[0]->mu * cs[0]->jn);
    const __m128 minF = _mm_sub_ps(zero, maxF);

    // 2) Tangente 1 (4 contatos em paralelo)
    {
      const __m128 vt = computeVn(cs[0]->t1, cs[1]->t1, cs[2]->t1, cs[3]->t1);
      const __m128 tm = _mm_set_ps(cs[3]->tangentMass, cs[2]->tangentMass, cs[1]->tangentMass, cs[0]->tangentMass);
      const __m128 oldJt = _mm_set_ps(cs[3]->jt1, cs[2]->jt1, cs[1]->jt1, cs[0]->jt1);
      const __m128 dj = _mm_sub_ps(zero, _mm_mul_ps(tm, vt));
      const __m128 newJt = _mm_min_ps(maxF, _mm_max_ps(minF, _mm_add_ps(oldJt, dj)));
      const __m128 delta = _mm_sub_ps(newJt, oldJt);
      alignas(16) float jtArr[4], dArr[4];
      _mm_store_ps(jtArr, newJt);
      _mm_store_ps(dArr, delta);
      for (int lane = 0; lane < 4; ++lane) cs[lane]->jt1 = jtArr[lane];
      apply4(cs[0]->t1 * dArr[0], cs[1]->t1 * dArr[1], cs[2]->t1 * dArr[2], cs[3]->t1 * dArr[3]);
    }

    // 3) Tangente 2 (4 contatos em paralelo)
    {
      const __m128 vt = computeVn(cs[0]->t2, cs[1]->t2, cs[2]->t2, cs[3]->t2);
      const __m128 tm = _mm_set_ps(cs[3]->tangentMass2, cs[2]->tangentMass2, cs[1]->tangentMass2, cs[0]->tangentMass2);
      const __m128 oldJt = _mm_set_ps(cs[3]->jt2, cs[2]->jt2, cs[1]->jt2, cs[0]->jt2);
      const __m128 dj = _mm_sub_ps(zero, _mm_mul_ps(tm, vt));
      const __m128 newJt = _mm_min_ps(maxF, _mm_max_ps(minF, _mm_add_ps(oldJt, dj)));
      const __m128 delta = _mm_sub_ps(newJt, oldJt);
      alignas(16) float jtArr[4], dArr[4];
      _mm_store_ps(jtArr, newJt);
      _mm_store_ps(dArr, delta);
      for (int lane = 0; lane < 4; ++lane) cs[lane]->jt2 = jtArr[lane];
      apply4(cs[0]->t2 * dArr[0], cs[1]->t2 * dArr[1], cs[2]->t2 * dArr[2], cs[3]->t2 * dArr[3]);
    }
  }
#endif

  // Resolve todas as cores independentes em paralelo (com Wide-SIMD 4x); apenas o ultimo balde (fallback) roda sequencialmente.
  void SolveContactsColored(std::vector<Rigidbody>& rb) {
    for (uint32_t colIdx = 0; colIdx < ConstraintColorGraph::kMaxColors - 1; ++colIdx) {
      const auto& bucket = colorGraph_.buckets[colIdx];
      if (bucket.empty()) continue;
      JobSystem::Get().ParallelFor(bucket.size(), 64, [&](size_t b, size_t e) {
        size_t i = b;
#ifdef NEWTOON_SIMD_SSE2
        for (; i + 4 <= e; i += 4) {
          Solve4ContactsWide(bucket[i], bucket[i + 1], bucket[i + 2], bucket[i + 3], rb);
        }
#endif
        for (; i < e; ++i) {
          SolveSingleContact(contacts_[bucket[i]], rb);
        }
      });
    }
    // Fallback sequencial caso algum contato tenha excedido as 15 cores paralelas
    for (uint32_t contactIdx : colorGraph_.buckets[ConstraintColorGraph::kMaxColors - 1]) {
      SolveSingleContact(contacts_[contactIdx], rb);
    }
  }

  struct NarrowResult {
    Pt pts[8];
    uint8_t count = 0;
  };

  struct PairEntry {
    uint64_t key = 0;
    PairInfo info;
  };

  struct WarmPoint {
    Vec3 p, n;
    float jn = 0.f, jt1 = 0.f, jt2 = 0.f;
  };
  struct WarmManifold {
    WarmPoint pts[8];
    uint8_t count = 0;
  };
  struct WarmEntry {
    uint64_t key = 0;
    WarmManifold manifold;
  };

  ConstraintColorGraph colorGraph_;
  std::vector<Entity> activeIndices_;
  std::vector<Mat3> invIw_;
  std::vector<uint32_t> invIwStamp_, islandStamp_;
  uint32_t frameStamp_ = 0;
  std::vector<Shape> cache_;
  std::vector<AABB> boxes_;
  std::vector<char> valid_;
  BVH4 bvh_;
  size_t bvhBodyCount_ = 0, refitSteps_ = 0;
  std::vector<std::vector<std::pair<Entity, Entity>>> chunkPairs_;
  std::vector<std::pair<Entity, Entity>> pairs_;
  std::vector<NarrowResult> narrowResults_;
  std::vector<Contact> contacts_;
  std::vector<PairEntry> curPairs_, prevPairs_;
  std::vector<WarmEntry> warmCache_, nextWarmCache_;
  std::vector<Entity> islandParent_;
  std::vector<uint8_t> islandRank_, islandAwake_, islandSeen_;
  std::vector<Transform> prevTransforms_;
  std::vector<CollisionEvent> events_;
  float acc_ = 0.f;
};

} // namespace Newtoon