// ToolEngine Editor - hierarquia pai/filho dos objetos.
//
// REGRAS
//   - O Transform de um objeto COM `Parent` e local (relativo ao pai); sem `Parent` e o proprio mundo.
//   - Matriz de mundo = matriz do pai * matriz local (recursivo, no maximo 32 niveis: ciclos sao cortados).
//   - Mover/girar/escalar o pai move os filhos. Apagar o pai apaga a subarvore.
//   - Ao reparentar com keepWorld, o objeto nao "pula": o local e recalculado (inversa da matriz do pai).
//   - Fisica: ao dar Play, objetos com Parent e corpo fisico sao soltos do pai mantendo o mundo (o Play e restaurado ao parar).
// Custo: objetos sem pai nao pagam nada alem de um teste `has<Parent>`.
#pragma once
#include "editor_state.hpp"

#include <atomic>
#include <bit>
#include <cstring>
#include "tool/core/parallel.hpp"

namespace editor {

// Decompoe uma matriz afim T*Ry*Rx*Rz*S em posicao / rotacao (graus, ordem do editor) / escala.
inline void decomposeTRS(const tool::Mat4& m, float pos[3], float rot[3], float scale[3]) {
  pos[0] = m.m[3][0]; pos[1] = m.m[3][1]; pos[2] = m.m[3][2];
  for (int c = 0; c < 3; ++c) { const float l = std::sqrt(m.m[c][0] * m.m[c][0] + m.m[c][1] * m.m[c][1] + m.m[c][2] * m.m[c][2]); scale[c] = l > 1e-8f ? l : 1.f; }
  auto R = [&](int row, int col) { return m.m[col][row] / scale[col]; };
  constexpr float d = 180.f / 3.14159265f;
  const float sx = std::clamp(-R(1, 2), -1.f, 1.f);
  rot[0] = std::asin(sx) * d;
  if (std::fabs(sx) < 0.99999f) { rot[1] = std::atan2(R(0, 2), R(2, 2)) * d; rot[2] = std::atan2(R(1, 0), R(1, 1)) * d; }
  else { rot[1] = std::atan2(-R(2, 0), R(0, 0)) * d; rot[2] = 0.f; }
}

// Inversa de uma matriz afim (3x3 + translacao) por cofatores.
inline tool::Mat4 inverseAffine(const tool::Mat4& a) {
  const float (*m)[4] = a.m; // m[col][row]
  const float a00 = m[0][0], a01 = m[1][0], a02 = m[2][0], a10 = m[0][1], a11 = m[1][1], a12 = m[2][1], a20 = m[0][2], a21 = m[1][2], a22 = m[2][2];
  const float c00 = a11 * a22 - a12 * a21, c01 = a12 * a20 - a10 * a22, c02 = a10 * a21 - a11 * a20;
  const float det = a00 * c00 + a01 * c01 + a02 * c02;
  tool::Mat4 r; if (std::fabs(det) < 1e-12f) return r;
  const float id = 1.f / det;
  const float i00 = c00 * id, i01 = (a02 * a21 - a01 * a22) * id, i02 = (a01 * a12 - a02 * a11) * id;
  const float i10 = c01 * id, i11 = (a00 * a22 - a02 * a20) * id, i12 = (a02 * a10 - a00 * a12) * id;
  const float i20 = c02 * id, i21 = (a01 * a20 - a00 * a21) * id, i22 = (a00 * a11 - a01 * a10) * id;
  r.m[0][0] = i00; r.m[1][0] = i01; r.m[2][0] = i02; r.m[0][1] = i10; r.m[1][1] = i11; r.m[2][1] = i12; r.m[0][2] = i20; r.m[1][2] = i21; r.m[2][2] = i22;
  const float tx = m[3][0], ty = m[3][1], tz = m[3][2];
  r.m[3][0] = -(i00 * tx + i01 * ty + i02 * tz); r.m[3][1] = -(i10 * tx + i11 * ty + i12 * tz); r.m[3][2] = -(i20 * tx + i21 * ty + i22 * tz);
  return r;
}

inline tool::Entity Editor::parentOf(tool::Entity e) {
  const auto* p = std::as_const(scene.world()).get<Parent>(e);   // leitura: nao sobe a versao do componente
  return p && scene.world().alive(p->e) && p->e != e ? p->e : tool::Entity{};
}

// Matriz de mundo SEM cache: sobe ate a raiz multiplicando (world = P_n * ... * P_1 * local; ciclos cortados em 32 niveis).
inline tool::Mat4 Editor::worldMatWalk(tool::Entity e) {
  auto& w = scene.world();
  tool::Mat4 m; bool first = true; tool::Entity cur = e;
  for (int depth = 0; depth < 32 && w.alive(cur); ++depth) {
    const Transform* t = std::as_const(w).get<Transform>(cur);   // leitura: nao sobe a versao
    if (t) { m = first ? worldMatrix(*t) : worldMatrix(*t) * m; first = false; }
    cur = parentOf(cur);
    if (!w.alive(cur)) break;
  }
  return m;
}

// Com o cache ativo (durante renderScene) esta funcao e SOMENTE LEITURA: acerto devolve a matriz pre-calculada em
// beginWorldMatFrame (todos os niveis); erro (entidade criada depois, sem Transform, adormecida) calcula sem gravar.
// Nada de resize/escrita aqui: seguro para chamar de varias threads enquanto beginWorldMatFrame nao roda.
inline tool::Mat4 Editor::worldMat(tool::Entity e) {
  if (renderWorldMatCacheActive_) {
    const std::size_t slotIdx = static_cast<std::size_t>(e.index);
    if (slotIdx < renderWorldMatCache_.size()) {
      const WorldMatSlot& s = renderWorldMatCache_[slotIdx];
      if (s.stamp == renderWorldMatStamp_ && s.gen == static_cast<std::uint32_t>(e.generation)) return s.m;
    }
    worldMatMisses_.fetch_add(1, std::memory_order_relaxed);   // falha de cache: o bench mostra quantas por quadro (nao esconde custo)
    return worldMatWalk(e);
  }

  auto& w = scene.world();
  tool::Mat4 m; bool first = true; tool::Entity cur = e;
  // sobe ate a raiz multiplicando: world = P_n * ... * P_1 * local
  for (int depth = 0; depth < 32 && w.alive(cur); ++depth) {
    const Transform* t = std::as_const(w).get<Transform>(cur);
    if (t) { m = first ? worldMatrix(*t) : worldMatrix(*t) * m; first = false; }
    cur = parentOf(cur);
    if (!w.alive(cur)) break;
  }
  return m;
}
// --- Assinaturas por valor dos vetores densos -------------------------------------------------------------------
namespace worldsig {
inline std::uint64_t mix64(std::uint64_t x) {
  x += 0x9E3779B97F4A7C15ull; x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull; x = (x ^ (x >> 27)) * 0x94D049BB133111EBull; return x ^ (x >> 31);
}
// Soma dos hashes de cada entidade viva (indice + geracao + bytes do componente). A SOMA nao depende da ordem do vetor
// denso (o swap-remove reordena) e e calculada em paralelo; qualquer escrita nos bytes muda o resultado, venha de onde vier.
template <class T> std::uint64_t denseSum(const tool::World& w, const tool::World::DenseView<T>& v) {
  static_assert(sizeof(T) % 4 == 0, "componente com tamanho multiplo de 4 bytes");
  std::atomic<std::uint64_t> total{0};
  tool::parallelFor(v.n, 4096, [&](std::size_t b, std::size_t e) {
    std::uint64_t sum = 0;
    for (std::size_t p = b; p < e; ++p) {
      const tool::u32 i = v.idx[p];
      if (!w.aliveIndex(i) || w.dormantAt(i)) continue;
      std::uint64_t h = (static_cast<std::uint64_t>(i) << 32) | w.generationAt(i);
      std::uint32_t words[sizeof(T) / 4]; std::memcpy(words, v.data + p, sizeof(T));
      for (std::uint32_t wd : words) h = (h ^ wd) * 0x9E3779B97F4A7C15ull;
      sum += mix64(h);
    }
    total.fetch_add(sum, std::memory_order_relaxed);
  });
  return total.load(std::memory_order_relaxed);
}
} // namespace worldsig

// Valida o cache de matrizes mundiais no inicio de cada vista, por VERSAO (World::epochOf): se as versoes de Transform, Parent e
// a estrutural nao mudaram desde a ultima construcao, as matrizes do quadro/vista anterior continuam validas (custo O(1)). Senao:
// novo carimbo (invalida tudo em O(1)) e pre-calcula EM PARALELO as matrizes de raizes e de filhos diretos de raizes (o caso comum),
// e em serie o resto (netos...). O verificador por valor (verifyVersionsByValue) protege contra escrita fora da versao.
inline void Editor::beginWorldMatFrame() {
  const auto bumpStamp = [&]() { if (++renderWorldMatStamp_ == 0) { renderWorldMatCache_.assign(renderWorldMatCache_.size(), WorldMatSlot{}); renderWorldMatStamp_ = 1; } };
  const tool::World& cw = scene.world();
  const auto tv = cw.denseView<Transform>();
  // Versoes atuais; sem mudanca desde a ultima construcao, nada a fazer.
  const std::uint64_t te = cw.epochOf<Transform>(), pe = cw.epochOf<Parent>(), se = cw.structuralEpoch();
  if (worldMatBuilt_ && te == worldTxEpoch_ && pe == worldParEpoch_ && se == worldStructEpoch_) return;
  // Passo final (serial): toda entidade viva com Transform que ainda nao tem matriz neste carimbo (netos, cadeias de 3+ niveis)
  // recebe a sua pela cadeia de pais. Depois disto o vetor NAO muda mais de tamanho nem de conteudo ate o proximo carimbo.
  const auto fillRemaining = [&]() {
    if (tv.n == 0) return;
    tool::u32 maxIdx = 0; for (std::size_t p = 0; p < tv.n; ++p) maxIdx = std::max(maxIdx, tv.idx[p]);
    if (renderWorldMatCache_.size() <= maxIdx) renderWorldMatCache_.resize(static_cast<std::size_t>(maxIdx) + 1);
    const std::uint32_t stamp = renderWorldMatStamp_;
    for (std::size_t p = 0; p < tv.n; ++p) {
      const tool::u32 i = tv.idx[p];
      if (!cw.aliveIndex(i) || cw.dormantAt(i)) continue;
      WorldMatSlot& s = renderWorldMatCache_[i];
      if (s.stamp == stamp && s.gen == cw.generationAt(i)) continue;
      s.m = worldMatWalk(tool::Entity{i, cw.generationAt(i)}); s.gen = cw.generationAt(i); s.stamp = stamp;
    }
  };
  worldTxEpoch_ = te; worldParEpoch_ = pe; worldStructEpoch_ = se; worldMatBuilt_ = true;
  ++worldMatRebuilds_;
  bumpStamp();
  if (tv.n == 0) return;
  tool::u32 maxIdx = 0; for (std::size_t p = 0; p < tv.n; ++p) maxIdx = std::max(maxIdx, tv.idx[p]);
  if (renderWorldMatCache_.size() <= maxIdx) renderWorldMatCache_.resize(static_cast<std::size_t>(maxIdx) + 1);
  const std::uint32_t stamp = renderWorldMatStamp_;
  WorldMatSlot* slots = renderWorldMatCache_.data();
  // Raiz = sem Parent ou com Parent invalido (mesma regra de parentOf).
  const auto parentIdxOf = [&](tool::u32 i, tool::u32& out) -> bool {
    const Parent* par = cw.getByIndex<Parent>(i);
    if (!par || !cw.alive(par->e) || par->e.index == i) return false;
    out = par->e.index; return true;
  };
  // Passo 1: raizes (mundo = local).
  tool::parallelFor(tv.n, 2048, [&](std::size_t b, std::size_t e) {
    for (std::size_t p = b; p < e; ++p) {
      const tool::u32 i = tv.idx[p];
      if (!cw.aliveIndex(i) || cw.dormantAt(i)) continue;
      tool::u32 pi;
      if (parentIdxOf(i, pi)) continue;
      WorldMatSlot& s = slots[i]; s.m = worldMatrix(tv.data[p]); s.gen = cw.generationAt(i); s.stamp = stamp;
    }
  });
  // Passo 2: filhos diretos de uma raiz com Transform (mundo = mundo do pai * local; mesma ordem de produto do calculo sob demanda).
  tool::parallelFor(tv.n, 2048, [&](std::size_t b, std::size_t e) {
    for (std::size_t p = b; p < e; ++p) {
      const tool::u32 i = tv.idx[p];
      if (!cw.aliveIndex(i) || cw.dormantAt(i)) continue;
      tool::u32 pi;
      if (!parentIdxOf(i, pi)) continue;
      const WorldMatSlot& ps = slots[pi];
      if (ps.stamp != stamp || ps.gen != cw.generationAt(pi)) continue;   // pai nao e raiz pre-calculada: sai sob demanda
      WorldMatSlot& s = slots[i]; s.m = ps.m * worldMatrix(tv.data[p]); s.gen = cw.generationAt(i); s.stamp = stamp;
    }
  });
  fillRemaining();   // passo 3 (serial): netos e cadeias profundas; depois disto worldMat() e so-leitura durante o render
}

// Somas por VALOR dos vetores densos (O(N), paralelas). Nao e o mecanismo de invalidacao: e o VERIFICADOR que pega escrita feita
// sem passar pela versao do World (ponteiro mutavel guardado e escrito depois). Usado em Debug ao fim de renderScene e no selftest.
inline Editor::ValueSums Editor::valueSumsNow() {
  const tool::World& cw = scene.world();
  ValueSums out;
  out.tf = worldsig::denseSum(cw, cw.denseView<Transform>());
  out.par = worldsig::denseSum(cw, cw.denseView<Parent>());
  const auto mv = cw.denseView<MeshRenderer>();
  std::atomic<std::uint64_t> total{0};
  tool::parallelFor(mv.n, 4096, [&](std::size_t b, std::size_t e) {
    std::uint64_t sum = 0;
    for (std::size_t p = b; p < e; ++p) {
      const MeshRenderer& mr = mv.data[p];
      if (!mr.castShadow) continue;
      const tool::u32 i = mv.idx[p];
      if (!cw.aliveIndex(i) || cw.dormantAt(i) || !cw.hasByIndex<Transform>(i)) continue;
      std::uint64_t h = (static_cast<std::uint64_t>(i) << 32) | cw.generationAt(i);
      h = worldsig::mix64(h ^ (static_cast<std::uint64_t>(static_cast<std::uint32_t>(mr.shape)) << 8) ^ (static_cast<std::uint64_t>(static_cast<std::uint32_t>(mr.node)) << 24));
      if (mr.shape == 3) for (const char* c = mr.model; *c && c < mr.model + sizeof mr.model; ++c) h = (h ^ static_cast<unsigned char>(*c)) * 1099511628211ull;
      sum += h;
    }
    total.fetch_add(sum, std::memory_order_relaxed);
  });
  out.mr = total.load(std::memory_order_relaxed);
  const auto iv = cw.denseView<Inactive>();
  for (std::size_t p = 0; p < iv.n; ++p) out.inactive += worldsig::mix64((static_cast<std::uint64_t>(iv.idx[p]) << 32) | (cw.aliveIndex(iv.idx[p]) ? cw.generationAt(iv.idx[p]) : 0u));
  return out;
}

// true = coerente. Amostra as versoes e as somas por valor; se as versoes NAO mudaram desde a amostra anterior, as somas tambem
// nao podem ter mudado (senao alguem escreveu sem passar pela versao: o cache de matrizes/sombras ficaria velho em silencio).
inline bool Editor::verifyVersionsByValue() {
  const tool::World& cw = scene.world();
  const std::uint64_t tf = cw.epochOf<Transform>(), par = cw.epochOf<Parent>(), mr = cw.epochOf<MeshRenderer>(), in = cw.epochOf<Inactive>(), st = cw.structuralEpoch();
  const ValueSums now = valueSumsNow();
  const bool unchanged = versionSample_.valid && versionSample_.tf == tf && versionSample_.par == par && versionSample_.mr == mr && versionSample_.inact == in && versionSample_.st == st;
  const bool ok = !unchanged || versionSample_.v == now;
  versionSample_ = VersionSample{tf, par, mr, in, st, now, true};
  return ok;
}

// Assinatura dos casters de sombra POR VERSAO: epochs de Transform/Parent/MeshRenderer/Inactive + estrutural + isolamento + malhas
// internas + modelos carregados (+ voxel chunks, poucos). O(1) em relacao ao numero de objetos; sem laco com worldMat.
// Limite conhecido: o LOD da esfera por distancia da camera nao entra (para o sol a camera ja esta na assinatura).
inline std::uint64_t Editor::casterSignatureEpoch() {
  if (!worldMatBuilt_) beginWorldMatFrame();
  const tool::World& cw = scene.world();
  std::uint64_t hs = 1469598103934665603ull;
  auto mixu = [&](std::uint64_t v) { hs ^= v; hs *= 1099511628211ull; };
  mixu(cw.epochOf<Transform>()); mixu(cw.epochOf<Parent>()); mixu(cw.epochOf<MeshRenderer>()); mixu(cw.epochOf<Inactive>()); mixu(cw.structuralEpoch());
  mixu(renderIsolated_ ? 1u : 0u);
  if (renderIsolated_ && cw.alive(isolatedRoot)) { mixu(isolatedRoot.index); mixu(isolatedRoot.generation); }
  for (const tool::Mesh* m : {&cube, &sphere, &sphereFar, &plane, &cylinder, &cone, &capsule, &wheel, &triPrism, &hexPrism}) {
    mixu(m->revision); mixu(m->pos.size()); mixu(m->idx.size());
  }
  mixu(lib.meshCount());   // modelo carregado/descartado muda o conjunto de casters desenhaveis
  scene.world().eachRead<VoxelChunk, Transform>([&](tool::Entity ce, const VoxelChunk& vc, const Transform&) {
    if ((scene.world().has<Inactive>(ce) || hiddenInView(ce))) return;
    mixu(ce.index); mixu(ce.generation); mixu(vc.revision); mixu(std::bit_cast<std::uint32_t>(vc.cell));
    const tool::Mat4 wm = worldMat(ce);
    for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) mixu(std::bit_cast<std::uint32_t>(wm.m[c][r]));
  });
  return hs;
}

inline tool::Vec3 Editor::worldPos(tool::Entity e) {
  if (!scene.world().has<Parent>(e)) { const Transform* t = std::as_const(scene.world()).get<Transform>(e); return t ? tool::Vec3{t->pos[0], t->pos[1], t->pos[2]} : tool::Vec3{}; }
  const tool::Mat4 m = worldMat(e); return {m.m[3][0], m.m[3][1], m.m[3][2]};
}

inline bool Editor::findNamed(const std::string& name, tool::Entity& out) {
  bool found = false;
  scene.world().each<Name>([&](tool::Entity e, Name& n) { if (!found && name == n.text) { out = e; found = true; } });
  return found;
}
// Direcao da luz sincronizada: o eixo -Y local (direcao padrao da luz) girado pelo Transform do alvo (ou deste objeto).
inline tool::Vec3 Editor::lightDir(tool::Entity le, const LightComp& lc) {
  tool::Entity src = le;
  if (lc.target[0]) { tool::Entity f = le; if (findNamed(lc.target, f)) src = f; }
  const tool::Vec4 d = worldMat(src).mul(tool::Vec4{0.f, -1.f, 0.f, 0.f});
  const tool::Vec3 v{d.x, d.y, d.z};
  return (v.x * v.x + v.y * v.y + v.z * v.z) > 1e-12f ? tool::normalize(v) : tool::Vec3{0.f, -1.f, 0.f};
}

inline bool Editor::isDescendant(tool::Entity e, tool::Entity anc) {
  tool::Entity cur = e;
  for (int i = 0; i < 64 && scene.world().alive(cur); ++i) { if (cur == anc) return true; cur = parentOf(cur); }
  return false;
}

inline bool Editor::setParent(tool::Entity child, tool::Entity parent, bool keepWorld) {
  auto& w = scene.world();
  if (!w.alive(child)) return false;
  if (w.alive(parent) && isDescendant(parent, child)) return false; // ciclo (inclui pai == filho)
  Transform* t = w.get<Transform>(child); if (!t) return false;
  const tool::Mat4 world = worldMat(child);
  if (w.has<Parent>(child)) w.remove<Parent>(child);
  if (w.alive(parent)) w.add<Parent>(child, Parent{parent});
  if (keepWorld) {
    const tool::Mat4 local = w.alive(parent) ? inverseAffine(worldMat(parent)) * world : world;
    decomposeTRS(local, t->pos, t->rot, t->scale);
  }
  return true;
}

// Modelo com hierarquia: o objeto "pai" leva o nome do arquivo; cada no do arquivo vira um objeto com a
// transformacao local dele (TRS) e, se tiver partes, um MeshRenderer que desenha essas partes (node = indice do no).
// Modelo sem nos (ou que nao carregou) vira um objeto so, como antes.
inline tool::Entity Editor::makeModel(const std::string& rel) {
  const auto* lm = lib.model(rel);
  if (!lm || lm->data.nodes.empty()) {
    tool::Vec3 tint{0.8f, 0.8f, 0.8f};
    tool::Entity e = make(fs::path(rel).stem().string().c_str());
    MeshRenderer m; m.shape = 3; ser::copyStr(m.model, sizeof m.model, rel);
    if (lib.mesh(rel, &tint)) { m.color[0] = tint.x; m.color[1] = tint.y; m.color[2] = tint.z; }
    scene.world().add<MeshRenderer>(e, m);
    assignAsset(rel);
    return e;
  }
  tool::Entity root = make(fs::path(rel).stem().string().c_str());
  const auto& nodes = lm->data.nodes;
  std::vector<tool::Entity> ents;
  ents.reserve(nodes.size());
  for (size_t n = 0; n < nodes.size(); ++n) {
    tool::Entity e = make(nodes[n].name.empty() ? "node" : nodes[n].name.c_str());
    if (auto* t = scene.world().get<Transform>(e)) decomposeTRS(nodes[n].local, t->pos, t->rot, t->scale);
    if (!nodes[n].parts.empty()) {
      MeshRenderer m; m.shape = 3; m.node = static_cast<int>(n); ser::copyStr(m.model, sizeof m.model, rel);
      const tool::Vec3& a = lm->data.parts[static_cast<size_t>(nodes[n].parts[0])].material.albedo;
      m.color[0] = a.x; m.color[1] = a.y; m.color[2] = a.z;
      scene.world().add<MeshRenderer>(e, m);
    }
    ents.push_back(e);
  }
  for (size_t n = 0; n < nodes.size(); ++n) { // o pai do arquivo ja foi criado (indice menor)
    const int p = nodes[n].parent;
    setParent(ents[n], (p >= 0 && static_cast<size_t>(p) < n) ? ents[static_cast<size_t>(p)] : root, false);
  }
  assignAsset(rel);
  return root;
}

inline void Editor::bakeWorld(tool::Entity e) {
  auto& w = scene.world();
  if (!w.has<Parent>(e)) return;
  Transform* t = w.get<Transform>(e);
  if (t) decomposeTRS(worldMat(e), t->pos, t->rot, t->scale);
  w.remove<Parent>(e);
}

inline void Editor::destroy(tool::Entity e) {
  auto& w = scene.world(); if (!w.alive(e)) return;
  std::vector<tool::Entity> all; // e + descendentes (BFS)
  all.push_back(e);
  for (size_t i = 0; i < all.size() && all.size() < 100000; ++i)
    w.each<Parent>([&](tool::Entity c, Parent& p) { if (p.e == all[i] && std::find(all.begin(), all.end(), c) == all.end()) all.push_back(c); });
  for (auto d : all) { if (!w.alive(d)) continue; releaseScript(d); w.destroy(d); if (selected == d) selected = {}; }
}

inline tool::Camera Editor::cameraFor(tool::Entity e) {
  auto& w = scene.world(); const CameraComp* c = w.get<CameraComp>(e); const Transform* t = w.get<Transform>(e);
  tool::Camera cam; if (!c || !t) return cam;
  if (!w.has<Parent>(e)) return withDrawDistance(cameraOf(*t, *c));
  const tool::Mat4 m = worldMat(e);
  auto dir = [&](tool::Vec3 v) { const tool::Vec4 r = m.mul({v.x, v.y, v.z, 0.f}); return tool::normalize({r.x, r.y, r.z}); };
  cam.eye = {m.m[3][0], m.m[3][1], m.m[3][2]}; cam.target = cam.eye + dir({0, 0, -1}); cam.up = dir({0, 1, 0});
  cam.fovY = c->fovDeg * kDeg2Rad; cam.nearZ = c->nearZ; cam.farZ = c->farZ;
  cam.ortho = c->ortho2D; cam.orthoHeight = c->orthoSize;
  return withDrawDistance(cam);
}

} // namespace editor
