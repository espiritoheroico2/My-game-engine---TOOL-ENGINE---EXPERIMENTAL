// Newtoon Physics - PhysicsWorld 3D: integracao, broad-phase (BVH), narrow-phase (esfera/caixa), solver de impulsos
// sequenciais com atrito e restituicao, sono, CCD por raio, eventos Enter/Stay/Exit, gatilhos e raycast.
//
// USO (a engine e dona dos arrays; indice = entidade):
//   Newtoon::PhysicsWorld world; world.gravity = {0,-9.81f,0};
//   world.Simulate(transforms, bodies, colliders, dt);          // dt variavel: o mundo acumula e roda passos FIXOS (fixedDt)
//   for (auto& ev : world.GetCollisionEvents()) { ... } world.ClearEvents();
//   auto hit = world.Raycast(origin, dir, 100.f);
// GRAVIDADE: world.gravity vale para todos; Rigidbody::overrideGravity + gravity = "vector physics" (substitui a do mundo).
// DETERMINISMO: mesma entrada -> mesma saida (sem aleatoriedade; o paralelismo so toca dados independentes por corpo).
#pragma once
#include "newtoon_bvh.hpp"
#include "newtoon_jobs.hpp"

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace Newtoon {

// Mundo de fisica 3D: guarda configuracoes, estatisticas e o estado interno (formas, BVH, contatos). A engine chama Simulate a cada quadro.
class PhysicsWorld {
 public:
  Vec3 gravity{0, -9.81f, 0};
  float fixedDt = 1.f / 60.f;
  int maxStepsPerFrame = 5;        // protecao contra a "espiral da morte" quando o quadro demora
  int solverIterations = 10;
  float baumgarte = 0.2f, slop = 0.005f, restitutionThreshold = 1.f;
  std::vector<PhysicsMaterial> materials{PhysicsMaterial{}}; // materialId indexa aqui

  // estatisticas do ultimo Simulate (para o Profiler)
  struct Stats { int bodies = 0, awake = 0, pairs = 0, contacts = 0, steps = 0, idleSteps = 0, broadphaseBuilds = 0, broadphaseRefits = 0; } stats;

  // Eventos de colisao (Enter/Stay/Exit) geradas desde o ultimo ClearEvents().
  const std::vector<CollisionEvent>& GetCollisionEvents() const { return events_; }
  // Descarta os eventos acumulados. Chame depois de processar os eventos.
  void ClearEvents() { events_.clear(); }

  // Avanca a simulacao com tempo dt (em segundos). Usa passos fixos de fixedDt e no maximo maxStepsPerFrame por chamada. Os arrays tf e rb sao atualizados in-place (col e so leitura).
  void Simulate(std::vector<Transform>& tf, std::vector<Rigidbody>& rb, const std::vector<Collider>& col, float dt) {
    const size_t n = std::min({tf.size(), rb.size(), col.size()});
    stats = Stats{}; stats.bodies = static_cast<int>(n);
    acc_ += std::clamp(dt, 0.f, 0.25f);
    int steps = 0;
    while (acc_ >= fixedDt && steps < maxStepsPerFrame) { Step(tf, rb, col, n, fixedDt); acc_ -= fixedDt; ++steps; }
    if (steps == maxStepsPerFrame) acc_ = 0.f;
    stats.steps = steps;
    if (steps == 0 && cache_.size() != n) {
      if (Prepare(tf, rb, col, n)) ++stats.broadphaseBuilds; else ++stats.broadphaseRefits;
    } // primeira chamada: deixa o raycast pronto
  }

  // Recalcula formas no mundo + BVH (ja chamado por Simulate). Chame antes de Raycast se ainda nao simulou.
  void Rebuild(const std::vector<Transform>& tf, const std::vector<Rigidbody>& rb, const std::vector<Collider>& col) {
    Prepare(tf, rb, col, std::min({tf.size(), rb.size(), col.size()}), true);
  }

  // Raycast no mundo: devolve o acerto mais proximo ate maxDistance a partir de origin na direcao dirIn (normalizada). `ignore` e a entidade a desconsiderar.
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
  // Numero de corpos (formas) na ultima preparacao.
  size_t BodyCount() const { return cache_.size(); }

 private:
  // ------------------------------------------------------------------ formas no mundo
  // Forma no mundo: centro, orientacao, meia-medidas, caixa envolvente e flag de esfera.
  struct Shape { Vec3 c; Mat3 R; Vec3 e; float r = 0; bool sphere = false; AABB box; bool valid = false; };
  // Copia dos dados de entrada usados para saber se a forma mudou desde a ultima preparacao.
  struct ShapeInput { Transform transform; ShapeType type; Vec3 extents, offset, inverseInertia; float radius; bool dynamic; };
  // Ponto de contato entre dois corpos, com normal, penetracao, coeficientes e impulsos acumulados do solver.
  struct Contact { Entity a, b; Vec3 p, n; float depth; float e, mu; Vec3 ra, rb; float normalMass = 0, tangentMass = 0, tangentMass2 = 0, bias = 0, jn = 0, jt1 = 0, jt2 = 0; Vec3 t1, t2; };
  std::vector<ShapeInput> shapeInputs_;

  // Monta a forma de um corpo a partir do transform e do collider. active = false marca a forma como invalida.
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
    }
    else {
      s.R = t.rotation.ToMat3(); s.c = t.position + s.R * c.offset;
      s.e = c.extents; const Vec3 ex{std::fabs(s.R.r[0].x) * s.e.x + std::fabs(s.R.r[0].y) * s.e.y + std::fabs(s.R.r[0].z) * s.e.z,
                                      std::fabs(s.R.r[1].x) * s.e.x + std::fabs(s.R.r[1].y) * s.e.y + std::fabs(s.R.r[1].z) * s.e.z,
                                      std::fabs(s.R.r[2].x) * s.e.x + std::fabs(s.R.r[2].y) * s.e.y + std::fabs(s.R.r[2].z) * s.e.z};
      s.box = {s.c - ex, s.c + ex};
    }
    return s;
  }

  // True se os dois vetores tem as mesmas componentes (comparacao exata).
  static bool SameVec(const Vec3& a, const Vec3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
  // True se a entrada guardada ainda corresponde ao transform, collider e corpo atuais.
  static bool SameShapeInput(const ShapeInput& a, const Transform& t, const Rigidbody& r, const Collider& c) {
    return SameVec(a.transform.position, t.position) &&
        a.transform.rotation.x == t.rotation.x && a.transform.rotation.y == t.rotation.y &&
        a.transform.rotation.z == t.rotation.z && a.transform.rotation.w == t.rotation.w &&
        a.type == c.type && SameVec(a.extents, c.extents) && SameVec(a.offset, c.offset) &&
        a.radius == c.radius && SameVec(a.inverseInertia, r.inverseInertiaLocal) && a.dynamic == r.IsDynamic();
  }
  // True se nenhuma das n entradas mudou desde a ultima preparacao (permite pular a reconstrucao da BVH).
  bool ShapeInputsUnchanged(const std::vector<Transform>& tf, const std::vector<Rigidbody>& rb,
                            const std::vector<Collider>& col, size_t n) const {
    if (shapeInputs_.size() != n) return false;
    for (size_t i = 0; i < n; ++i) if (!SameShapeInput(shapeInputs_[i], tf[i], rb[i], col[i])) return false;
    return true;
  }

  // Recalcula formas no mundo e atualiza a BVH. O refit preserva a topologia por varios passos;
  // uma reconstrução periódica recupera a qualidade espacial da árvore.
  bool Prepare(const std::vector<Transform>& tf, const std::vector<Rigidbody>& rb, const std::vector<Collider>& col, size_t n, bool forceFull = false) {
    cache_.resize(n); boxes_.resize(n); valid_.assign(n, 1); shapeInputs_.resize(n);
    JobSystem::Get().ParallelFor(n, 512, [&](size_t b, size_t e) { for (size_t i = b; i < e; ++i) {
      cache_[i] = MakeShape(tf[i], col[i], rb[i], true); boxes_[i] = cache_[i].box;
      shapeInputs_[i] = {tf[i], col[i].type, col[i].extents, col[i].offset, rb[i].inverseInertiaLocal, col[i].radius, rb[i].IsDynamic()};
    } });
    constexpr std::size_t kRefitRebuildInterval = 60;
    const bool full = forceFull || bvh_.Empty() || bvhBodyCount_ != n || refitSteps_ >= kRefitRebuildInterval;
    if (full || !bvh_.Refit(boxes_, valid_)) {
      bvh_.Build(boxes_, valid_); bvhBodyCount_ = n; refitSteps_ = 0; return true;
    }
    ++refitSteps_;
    return false;
  }

  // ------------------------------------------------------------------ passo fixo
  // Executa um passo fixo de dt: velocidades, broad-phase, narrow-phase, solver, CCD, posicoes, sono e eventos.
  void Step(std::vector<Transform>& tf, std::vector<Rigidbody>& rb, const std::vector<Collider>& col, size_t n, float dt) {
    auto active = [&](size_t i) { return (rb[i].IsDynamic() && !rb[i].isSleeping) || (rb[i].isKinematic && rb[i].velocity.SqrMagnitude() > 1e-8f); };
    bool hasActiveBody = false;
    for (size_t i = 0; i < n && !hasActiveBody; ++i) hasActiveBody = active(i);
    if (!hasActiveBody && !ShapeInputsUnchanged(tf, rb, col, n)) {
      if (Prepare(tf, rb, col, n)) ++stats.broadphaseBuilds; else ++stats.broadphaseRefits;
    }
    if (!hasActiveBody) {
      for (size_t i = 0; i < n; ++i) {
        Rigidbody& r = rb[i];
        if (!r.isSleeping) r.grounded = false;
        r.forceAccumulator = r.torqueAccumulator = {};
      }
      curPairs_.clear();
      for (auto& kv : prevPairs_)
        events_.push_back({kv.second.a, kv.second.b, {}, {}, 0.f, CollisionType::Exit, kv.second.trigger});
      prevPairs_.clear();
      ++stats.idleSteps;
      TrimEvents();
      return;
    }

    // 1) velocidades (paralelo): gravidade/vector physics, forcas, arrasto
    JobSystem::Get().ParallelFor(n, 1024, [&](size_t b, size_t e) {
      for (size_t i = b; i < e; ++i) {
        Rigidbody& r = rb[i]; if (!r.isSleeping) r.grounded = false;
        if (!r.IsDynamic() || r.isSleeping) { r.forceAccumulator = r.torqueAccumulator = {}; continue; }
        const Vec3 g = r.overrideGravity ? r.gravity : gravity;
        if (r.useGravity) r.velocity += g * (r.gravityScale * dt);
        r.velocity += r.forceAccumulator * (r.inverseMass * dt);
        if (!r.freezeRotation && r.torqueAccumulator.SqrMagnitude() != 0.f)
          r.angularVelocity += InvInertiaWorld(r, tf[i].rotation.ToMat3()) * r.torqueAccumulator * dt;
        r.forceAccumulator = r.torqueAccumulator = {};
        r.velocity *= 1.f / (1.f + r.linearDamping * dt); r.angularVelocity *= 1.f / (1.f + r.angularDamping * dt);
        if (r.freezeRotation) r.angularVelocity = {};
      }
    });
    // 2) formas + BVH
    if (Prepare(tf, rb, col, n)) ++stats.broadphaseBuilds; else ++stats.broadphaseRefits;
    // 3) pares (broad-phase)
    pairs_.clear();
    for (size_t i = 0; i < n; ++i) {
      if (!active(i)) continue;
      bvh_.Query(boxes_[i], [&](Entity j) {
        if (j == i) return;
        if (j < i && active(j)) return; // par ja visto do outro lado
        if (rb[i].IsStatic() && rb[j].IsStatic()) return;
        pairs_.push_back({static_cast<Entity>(i), j});
      });
    }
    stats.pairs += static_cast<int>(pairs_.size());
    // 4) narrow-phase
    contacts_.clear(); curPairs_.clear();
    for (auto& pr : pairs_) {
      const Entity a = pr.first, b = pr.second;
      tmp_.clear(); if (!Collide(cache_[a], cache_[b], tmp_)) continue;
      const bool trig = col[a].isTrigger || col[b].isTrigger;
      PairInfo info; info.a = a; info.b = b; info.trigger = trig; info.p = tmp_[0].p; info.n = tmp_[0].n; info.depth = tmp_[0].depth;
      if (!trig) {
        const PhysicsMaterial& ma = Mat(col[a].materialId); const PhysicsMaterial& mb = Mat(col[b].materialId);
        const float mu = Combine(ma.frictionCombine, ma.friction, mb.friction), bounce = Combine(ma.bounceCombine, ma.bounciness, mb.bounciness);
        for (auto& c : tmp_) { Contact k{}; k.a = a; k.b = b; k.p = c.p; k.n = c.n; k.depth = c.depth; k.e = bounce; k.mu = mu; contacts_.push_back(k); }
        // acorda corpos dormindo tocados por um corpo ativo
        if (rb[a].isSleeping && active(b)) rb[a].WakeUp();
        if (rb[b].isSleeping && active(a)) rb[b].WakeUp();
        // chao: normal (A->B) com componente vertical contra o corpo que esta em cima
        if (c0Up(tmp_[0].n) > 0.7f) { rb[b].grounded = true; rb[b].groundNormal = tmp_[0].n; }
        if (c0Up(tmp_[0].n) < -0.7f) { rb[a].grounded = true; rb[a].groundNormal = -tmp_[0].n; }
      }
      curPairs_[Key(a, b)] = info;
    }
    stats.contacts += static_cast<int>(contacts_.size());
    // 5) solver
    if (!contacts_.empty()) {
      invIw_.resize(n); invIwReady_.assign(n, 0);
      auto prepareInertia = [&](Entity e) {
        if (invIwReady_[e]) return;
        invIw_[e] = InvInertiaWorld(rb[e], cache_[e].R);
        invIwReady_[e] = 1;
      };
      for (const Contact& c : contacts_) { prepareInertia(c.a); prepareInertia(c.b); }
      PreSolve(tf, rb, dt);
      for (int it = 0; it < solverIterations; ++it) SolveContacts(rb);
      for (auto& c : contacts_) { auto f = curPairs_.find(Key(c.a, c.b)); if (f != curPairs_.end()) f->second.impulse += c.jn; }
    }
    // 6) CCD (so quem pediu e anda rapido): limita o deslocamento ao primeiro acerto
    for (size_t i = 0; i < n; ++i) {
      Rigidbody& r = rb[i]; if (!r.useCCD || !r.IsDynamic() || r.isSleeping) continue;
      const float speed = r.velocity.Magnitude(), reach = speed * dt, size = cache_[i].sphere ? cache_[i].r : std::min({cache_[i].e.x, cache_[i].e.y, cache_[i].e.z});
      if (reach <= size) continue;
      const RaycastHit h = Raycast(cache_[i].c, r.velocity, reach + size, static_cast<Entity>(i));
      if (h.hit && !col[h.entity].isTrigger) { const float allow = std::max(0.f, h.distance - size); tf[i].position += Normalized(r.velocity) * std::min(allow, reach) - r.velocity * dt; }
    }
    // 7) posicoes e rotacoes
    JobSystem::Get().ParallelFor(n, 1024, [&](size_t b, size_t e) {
      for (size_t i = b; i < e; ++i) {
        Rigidbody& r = rb[i]; if (r.IsStatic() || (r.isSleeping && !r.isKinematic)) continue;
        tf[i].position += r.velocity * dt;
        const Vec3 w = r.angularVelocity;
        if (w.SqrMagnitude() > 1e-12f) { const Quat dq{w.x * dt * 0.5f, w.y * dt * 0.5f, w.z * dt * 0.5f, 0.f}; Quat q = dq * tf[i].rotation; tf[i].rotation = Quat{tf[i].rotation.x + q.x, tf[i].rotation.y + q.y, tf[i].rotation.z + q.z, tf[i].rotation.w + q.w}.Normalized(); }
      }
    });
    // 8) sono
    for (size_t i = 0; i < n; ++i) {
      Rigidbody& r = rb[i]; if (!r.IsDynamic() || r.isSleeping) { if (!r.isSleeping) ++stats.awake; continue; }
      if (r.velocity.Magnitude() < r.sleepThreshold && r.angularVelocity.Magnitude() < r.sleepThreshold) { r.sleepTimer += dt; if (r.sleepTimer > 0.5f) { r.isSleeping = true; r.velocity = r.angularVelocity = {}; } }
      else r.sleepTimer = 0.f;
      if (!r.isSleeping) ++stats.awake;
    }
    // 9) eventos Enter/Stay/Exit comparando com o passo anterior
    for (auto& kv : curPairs_) {
      const bool had = prevPairs_.count(kv.first) != 0;
      events_.push_back({kv.second.a, kv.second.b, kv.second.p, kv.second.n, kv.second.impulse, had ? CollisionType::Stay : CollisionType::Enter, kv.second.trigger});
    }
    for (auto& kv : prevPairs_) if (!curPairs_.count(kv.first)) events_.push_back({kv.second.a, kv.second.b, {}, {}, 0.f, CollisionType::Exit, kv.second.trigger});
    prevPairs_ = curPairs_;
    TrimEvents();
  }

  // Componente vertical de uma normal (usada para detectar chao).
  static float c0Up(const Vec3& n) { return n.y; }
  // Chave unica e simetrica para o par de entidades (a, b).
  static uint64_t Key(Entity a, Entity b) { if (a > b) std::swap(a, b); return (static_cast<uint64_t>(a) << 32) | b; }
  // Material de id, ou o material padrao se o id nao existir.
  const PhysicsMaterial& Mat(uint16_t id) const { return materials[id < materials.size() ? id : 0]; }
  // Limita a fila de eventos: passando de 20000 apaga os 10000 mais antigos. Atenção: a perda e silenciosa; chame ClearEvents() a cada quadro.
  void TrimEvents() {
    if (events_.size() > 20000) events_.erase(events_.begin(), events_.begin() + 10000);
  }
  // Tensor de inercia inverso no MUNDO: R * diag(I^-1) * R^T (zero para corpos estaticos/cinematicos: nao giram por impacto).
  static Mat3 InvInertiaWorld(const Rigidbody& r, const Mat3& R) {
    Mat3 out; if (!r.IsDynamic()) { out.r[0] = out.r[1] = out.r[2] = {}; return out; }
    const float d[3] = {r.inverseInertiaLocal.x, r.inverseInertiaLocal.y, r.inverseInertiaLocal.z};
    float m[3][3];
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) { float s = 0; for (int k = 0; k < 3; ++k) s += At(R.r[i], k) * d[k] * At(R.r[j], k); m[i][j] = s; }
    for (int i = 0; i < 3; ++i) out.r[i] = {m[i][0], m[i][1], m[i][2]};
    return out;
  }

  // ------------------------------------------------------------------ narrow-phase
  // Ponto de contato temporario: posicao, normal e penetracao.
  struct Pt { Vec3 p, n; float depth; };
  // Testa colisao entre duas formas e acrescenta os pontos de contato em out. Normal de A para B.
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
  // Colisao esfera x esfera.
  static bool SphereSphere(const Shape& A, const Shape& B, std::vector<Pt>& out) {
    const Vec3 d = B.c - A.c; const float dist = d.Magnitude(), rr = A.r + B.r;
    if (dist >= rr) return false;
    const Vec3 n = dist > 1e-6f ? d * (1.f / dist) : Vec3{0, 1, 0};
    out.push_back({A.c + n * (A.r - (rr - dist) * 0.5f), n, rr - dist}); return true;
  }
  // normal sai de S (esfera) para X (caixa)
  static bool SphereBox(const Shape& S, const Shape& X, std::vector<Pt>& out) {
    const Vec3 rel = S.c - X.c; const Vec3 p = X.R.Transposed() * rel; // centro da esfera no espaco local da caixa
    Vec3 q{std::clamp(p.x, -X.e.x, X.e.x), std::clamp(p.y, -X.e.y, X.e.y), std::clamp(p.z, -X.e.z, X.e.z)};
    Vec3 nLocal; float depth;
    const Vec3 d = p - q; const float dist = d.Magnitude();
    if (dist > 1e-6f) { if (dist >= S.r) return false; nLocal = d * (1.f / dist); depth = S.r - dist; }
    else { // centro dentro da caixa: sai pela face mais proxima
      const float dx = X.e.x - std::fabs(p.x), dy = X.e.y - std::fabs(p.y), dz = X.e.z - std::fabs(p.z);
      if (dx <= dy && dx <= dz) { nLocal = {p.x >= 0 ? 1.f : -1.f, 0, 0}; q.x = nLocal.x * X.e.x; depth = S.r + dx; }
      else if (dy <= dz) { nLocal = {0, p.y >= 0 ? 1.f : -1.f, 0}; q.y = nLocal.y * X.e.y; depth = S.r + dy; }
      else { nLocal = {0, 0, p.z >= 0 ? 1.f : -1.f}; q.z = nLocal.z * X.e.z; depth = S.r + dz; }
    }
    const Vec3 nBoxToSphere = X.R * nLocal;       // da caixa para a esfera
    out.push_back({X.c + X.R * q, -nBoxToSphere, depth}); // normal de S para X
    return true;
  }
  // Colisao caixa x caixa (SAT): testa os eixos e gera ate 8 pontos de contato.
  static bool BoxBox(const Shape& A, const Shape& B, std::vector<Pt>& out) {
    const Vec3 Aax[3] = {A.R.Col(0), A.R.Col(1), A.R.Col(2)}, Bax[3] = {B.R.Col(0), B.R.Col(1), B.R.Col(2)};
    const float ae[3] = {A.e.x, A.e.y, A.e.z}, be[3] = {B.e.x, B.e.y, B.e.z};
    const Vec3 T = B.c - A.c;
    float best = 1e30f; Vec3 bn; int kind = 0, ia = 0, ib = 0; // kind 0 = face, 1 = aresta
    auto test = [&](Vec3 L, bool edge, int i, int j) {
      const float len = L.Magnitude(); if (len < 1e-6f) return true; L = L * (1.f / len);
      float ra = 0, rb = 0; for (int k = 0; k < 3; ++k) { ra += ae[k] * std::fabs(Dot(Aax[k], L)); rb += be[k] * std::fabs(Dot(Bax[k], L)); }
      const float dist = std::fabs(Dot(T, L)), ov = ra + rb - dist;
      if (ov < 0.f) return false;
      const float score = edge ? ov * 1.05f + 1e-4f : ov; // prefere faces em empate (contatos mais estaveis)
      if (score < best) { best = score; bn = Dot(T, L) >= 0.f ? L : -L; kind = edge ? 1 : 0; ia = i; ib = j; }
      return true;
    };
    for (int i = 0; i < 3; ++i) if (!test(Aax[i], false, i, 0)) return false;
    for (int i = 0; i < 3; ++i) if (!test(Bax[i], false, 0, i)) return false;
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) if (!test(Cross(Aax[i], Bax[j]), true, i, j)) return false;
    float raN = 0, rbN = 0; for (int k = 0; k < 3; ++k) { raN += ae[k] * std::fabs(Dot(Aax[k], bn)); rbN += be[k] * std::fabs(Dot(Bax[k], bn)); }
    const size_t first = out.size();
    auto inside = [&](const Shape& S, const Vec3& pt) { const Vec3 l = S.R.Transposed() * (pt - S.c); return std::fabs(l.x) <= S.e.x + 1e-3f && std::fabs(l.y) <= S.e.y + 1e-3f && std::fabs(l.z) <= S.e.z + 1e-3f; };
    for (int s = 0; s < 8; ++s) { // vertices de B dentro de A
      const Vec3 v = B.c + Bax[0] * ((s & 1) ? be[0] : -be[0]) + Bax[1] * ((s & 2) ? be[1] : -be[1]) + Bax[2] * ((s & 4) ? be[2] : -be[2]);
      if (!inside(A, v)) continue;
      const float d = raN - Dot(v - A.c, bn); if (d > 0.f) out.push_back({v, bn, d});
    }
    for (int s = 0; s < 8; ++s) { // vertices de A dentro de B
      const Vec3 v = A.c + Aax[0] * ((s & 1) ? ae[0] : -ae[0]) + Aax[1] * ((s & 2) ? ae[1] : -ae[1]) + Aax[2] * ((s & 4) ? ae[2] : -ae[2]);
      if (!inside(B, v)) continue;
      const float d = Dot(v - B.c, bn) + rbN; if (d > 0.f) out.push_back({v, bn, d});
    }
    if (out.size() == first) { // aresta x aresta: ponto medio entre as duas arestas mais proximas
      Vec3 pa = A.c, pb = B.c;
      for (int k = 0; k < 3; ++k) { if (kind == 1 && k == ia) continue; pa += Aax[k] * (Dot(Aax[k], bn) > 0 ? ae[k] : -ae[k]); }
      for (int k = 0; k < 3; ++k) { if (kind == 1 && k == ib) continue; pb += Bax[k] * (Dot(Bax[k], bn) > 0 ? -be[k] : be[k]); }
      Vec3 mid = (pa + pb) * 0.5f;
      if (kind == 1) { const Vec3 d1 = Aax[ia], d2 = Bax[ib], r = pa - pb; const float a = Dot(d1, d1), e2 = Dot(d2, d2), b2 = Dot(d1, d2), c = Dot(d1, r), f = Dot(d2, r), den = a * e2 - b2 * b2;
        if (std::fabs(den) > 1e-8f) { const float s = (b2 * f - c * e2) / den, t = (a * f - b2 * c) / den; mid = ((pa + d1 * s) + (pb + d2 * t)) * 0.5f; } }
      out.push_back({mid, bn, std::max(best, 1e-4f)});
    }
    if (out.size() - first > 8) { std::sort(out.begin() + static_cast<long>(first), out.end(), [](const Pt& x, const Pt& y) { return x.depth > y.depth; }); out.resize(first + 8); } // no maximo 8 pontos mais fundos
    return true;
  }

  // ------------------------------------------------------------------ raio x forma
  // Raio x forma: True se o raio (o, d) acerta a forma antes de maxT. Devolve o tempo em tOut e a normal em nOut.
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

  // ------------------------------------------------------------------ solver
  // Prepara os contatos para o solver: massas efetivas, bias de penetracao, restituicao e tangentes.
  void PreSolve(const std::vector<Transform>& tf, std::vector<Rigidbody>& rb, float dt) {
    for (auto& c : contacts_) {
      const Rigidbody& A = rb[c.a]; const Rigidbody& B = rb[c.b];
      c.ra = c.p - tf[c.a].position; c.rb = c.p - tf[c.b].position;
      const Mat3& IA = invIw_[c.a]; const Mat3& IB = invIw_[c.b];
      auto eff = [&](const Vec3& dir) { const Vec3 rna = Cross(c.ra, dir), rnb = Cross(c.rb, dir); return A.inverseMass + B.inverseMass + Dot(dir, Cross(IA * rna, c.ra) + Cross(IB * rnb, c.rb)); };
      const float k = eff(c.n); c.normalMass = k > 1e-9f ? 1.f / k : 0.f;
      const Vec3 vrel = (B.velocity + Cross(B.angularVelocity, c.rb)) - (A.velocity + Cross(A.angularVelocity, c.ra));
      const float vn = Dot(vrel, c.n);
      c.bias = std::min(4.f, baumgarte / dt * std::max(c.depth - slop, 0.f));
      if (vn < -restitutionThreshold) c.bias += -c.e * vn;                     // restituicao (quicar)
      Vec3 t = vrel - c.n * vn; t = t.SqrMagnitude() > 1e-8f ? Normalized(t) : (std::fabs(c.n.x) > 0.9f ? Normalized(Cross(c.n, Vec3{0, 1, 0})) : Normalized(Cross(c.n, Vec3{1, 0, 0})));
      c.t1 = t; c.t2 = Normalized(Cross(c.n, t));
      const float k1 = eff(c.t1), k2 = eff(c.t2); c.tangentMass = k1 > 1e-9f ? 1.f / k1 : 0.f; c.tangentMass2 = k2 > 1e-9f ? 1.f / k2 : 0.f;
      c.jn = c.jt1 = c.jt2 = 0.f;
    }
  }
  // Uma iteracao do solver de impulsos: impulso normal e de atrito (cone de Coulomb) em cada contato.
  void SolveContacts(std::vector<Rigidbody>& rb) {
    for (auto& c : contacts_) {
      Rigidbody& A = rb[c.a]; Rigidbody& B = rb[c.b];
      auto vrel = [&] { return (B.velocity + Cross(B.angularVelocity, c.rb)) - (A.velocity + Cross(A.angularVelocity, c.ra)); };
      auto apply = [&](const Vec3& P) { A.velocity -= P * A.inverseMass; B.velocity += P * B.inverseMass; A.angularVelocity -= invIw_[c.a] * Cross(c.ra, P); B.angularVelocity += invIw_[c.b] * Cross(c.rb, P); };
      // normal
      { const float vn = Dot(vrel(), c.n); float dj = c.normalMass * (-vn + c.bias); const float old = c.jn; c.jn = std::max(0.f, old + dj); dj = c.jn - old; apply(c.n * dj); }
      // atrito (cone de Coulomb em 2 tangentes)
      const float maxF = c.mu * c.jn;
      { const float vt = Dot(vrel(), c.t1); float dj = -c.tangentMass * vt; const float old = c.jt1; c.jt1 = std::clamp(old + dj, -maxF, maxF); dj = c.jt1 - old; apply(c.t1 * dj); }
      { const float vt = Dot(vrel(), c.t2); float dj = -c.tangentMass2 * vt; const float old = c.jt2; c.jt2 = std::clamp(old + dj, -maxF, maxF); dj = c.jt2 - old; apply(c.t2 * dj); }
    }
  }
  // Estado de um par de corpos no passo atual (usado para gerar eventos Enter/Stay/Exit).
  struct PairInfo { Entity a = kNoEntity, b = kNoEntity; Vec3 p, n; float depth = 0, impulse = 0; bool trigger = false; };
  std::vector<Mat3> invIw_;
  std::vector<char> invIwReady_;
  std::vector<Shape> cache_; std::vector<AABB> boxes_; std::vector<char> valid_;
  BVH bvh_;
  size_t bvhBodyCount_ = 0, refitSteps_ = 0;
  std::vector<std::pair<Entity, Entity>> pairs_; std::vector<Pt> tmp_; std::vector<Contact> contacts_;
  std::unordered_map<uint64_t, PairInfo> curPairs_, prevPairs_;
  std::vector<CollisionEvent> events_;
  float acc_ = 0.f;
};

} // namespace Newtoon
