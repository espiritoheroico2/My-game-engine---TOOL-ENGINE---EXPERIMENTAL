// UI-CONTRATO: este arquivo usa Dear ImGui. Catalogo, padroes e regras em ui_contract.hpp (secao 2.13).
// Elementos: entrada de mouse do viewport (IsMouseClicked, IsMouseDown) e GetIO para posicao.
// Dados: raio de selecao calculado a partir da camera (pickByRay); nao desenha widgets.
// Troca SpriteUI: a entrada de mouse vira um evento da SpriteUI ou continua lendo a entrada da plataforma.
// ToolEngine Editor - ferramentas da janela Scene: projecao 3D->tela e gizmo de transformacao (mover/girar/escalar).
//
// GIZMO: setas coloridas (X vermelho, Y verde, Z azul) no objeto selecionado, com tamanho constante na tela.
//   Mover (W):    arraste uma seta  -> o objeto anda so naquele eixo.
//   Girar (E):    arraste um anel   -> gira em torno daquele eixo (Ctrl = pulos de 15 graus).
//   Escalar (R):  arraste a ponta de um eixo -> escala so nele; arraste o quadrado central -> escala uniforme (Ctrl = passos de 0.1).
//   Mover com Ctrl = passos de 0.25 m.
// Tudo e feito em espaco de tela (sem raycast 3D): barato, estavel e independente da API grafica.
// Os valores sao gravados direto no Transform do objeto (os mesmos campos do Inspector).
#pragma once
#include "editor_state.hpp"

#include "imgui.h"

#include <algorithm>
#include <cmath>

namespace editor {

// Matriz de uma camera + retangulo da imagem na tela: transforma pontos 3D em pixels do ImGui.
struct ViewXf {
  tool::Mat4 vp; ImVec2 org, size; tool::Vec3 eye; float focal = 1.f; // focal = 1/tan(fovY/2)
  static ViewXf make(const tool::Camera& c, ImVec2 org, ImVec2 size) {
    ViewXf v; v.org = org; v.size = size; v.eye = c.eye; v.focal = 1.f / std::tan(std::max(0.01f, c.fovY) * 0.5f);
    v.vp = tool::Mat4::perspective(c.fovY, size.x / size.y, c.nearZ, c.farZ) * tool::Mat4::lookAt(c.eye, c.target, c.up);
    return v;
  }
  ImVec2 toScreen(const tool::Vec4& c) const { return {org.x + (c.x / c.w * 0.5f + 0.5f) * size.x, org.y + (1.f - (c.y / c.w * 0.5f + 0.5f)) * size.y}; }
  bool point(tool::Vec3 p, ImVec2& s) const { const tool::Vec4 c = vp.mul({p.x, p.y, p.z, 1}); if (c.w < 0.01f) return false; s = toScreen(c); return true; }
  // Linha 3D recortada no plano da camera (evita linhas "esticadas" quando um ponta esta atras).
  bool line(tool::Vec3 a, tool::Vec3 b, ImVec2& pa, ImVec2& pb) const {
    tool::Vec4 ca = vp.mul({a.x, a.y, a.z, 1}), cb = vp.mul({b.x, b.y, b.z, 1});
    constexpr float eps = 0.01f;
    if (ca.w < eps && cb.w < eps) return false;
    auto lerp = [](const tool::Vec4& p, const tool::Vec4& q, float t) { return tool::Vec4{p.x + (q.x - p.x) * t, p.y + (q.y - p.y) * t, p.z + (q.z - p.z) * t, p.w + (q.w - p.w) * t}; };
    if (ca.w < eps) ca = lerp(ca, cb, (eps - ca.w) / (cb.w - ca.w)); else if (cb.w < eps) cb = lerp(cb, ca, (eps - cb.w) / (ca.w - cb.w));
    pa = toScreen(ca); pb = toScreen(cb);
    return true;
  }
};

inline void line3(ImDrawList* dl, const ViewXf& v, tool::Vec3 a, tool::Vec3 b, ImU32 col, float th = 1.f) {
  ImVec2 pa, pb; if (v.line(a, b, pa, pb)) dl->AddLine(pa, pb, col, th);
}

// Distancia (px) de um ponto a um segmento.
inline float distToSegment(ImVec2 p, ImVec2 a, ImVec2 b) {
  const float dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
  const float t = l2 > 1e-6f ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / l2, 0.f, 1.f) : 0.f;
  return std::hypot(p.x - (a.x + dx * t), p.y - (a.y + dy * t));
}

// Caixa alinhada aos eixos do objeto no mundo (cantos da malha ja transformados). false se nao tem malha.
inline bool worldBox(Editor& ed, tool::Entity e, tool::Vec3& lo, tool::Vec3& hi) {
  auto& w = ed.scene.world();
  const MeshRenderer* mr = w.get<MeshRenderer>(e); if (!mr) return false;
  const tool::Mesh* m = ed.meshOf(*mr); if (!m || m->pos.empty()) return false;
  const tool::Mat4 wm = ed.worldMat(e);
  lo = hi = wm.transformPoint(m->pos[0]);
  for (const tool::Vec3& p : m->pos) {
    const tool::Vec3 q = wm.transformPoint(p);
    lo = {std::min(lo.x, q.x), std::min(lo.y, q.y), std::min(lo.z, q.z)};
    hi = {std::max(hi.x, q.x), std::max(hi.y, q.y), std::max(hi.z, q.z)};
  }
  return true;
}

// COLAR (ima de contato): encosta o objeto na superficie mais proxima de outro objeto, sem sobrepor e sem deixar vao.
// Para cada outro objeto com malha, testa encostar uma face de `e` na face oposta dele, no eixo X, Y ou Z. So vale
// se os outros dois eixos ja se sobrepoem (ou seja, as superficies realmente se encaram). Descarta encostos que
// criariam sobreposicao com um terceiro objeto e escolhe o de menor deslocamento. Retorna false se nao achou nada.
inline bool snapToContact(Editor& ed, tool::Entity e) {
  auto& w = ed.scene.world();
  tool::Vec3 alo{}, ahi{};
  if (!w.alive(e) || w.has<Locked>(e) || !w.get<Transform>(e) || !worldBox(ed, e, alo, ahi)) return false;
  struct Box { tool::Vec3 lo, hi; };
  std::vector<Box> others;
  w.eachEntity([&](tool::Entity o) {
    if (o == e || !w.alive(o) || w.has<Inactive>(o) || ed.isDescendant(o, e)) return;
    Box b; if (worldBox(ed, o, b.lo, b.hi)) others.push_back(b);
  });
  if (others.empty()) return false;
  const float eps = 1e-4f;
  auto get = [](const tool::Vec3& v, int k) { return k == 0 ? v.x : k == 1 ? v.y : v.z; };
  auto overlapsAt = [&](int k, float shift, const Box& b) { // A deslocado em `shift` no eixo k sobrepoe b (com folga)?
    for (int j = 0; j < 3; ++j) {
      float lo = get(alo, j), hi = get(ahi, j);
      if (j == k) { lo += shift; hi += shift; }
      if (!(lo < get(b.hi, j) - eps && hi > get(b.lo, j) + eps)) return false;
    }
    return true;
  };
  bool found = false; float bestAbs = 0.f; int bestK = 0; float bestDelta = 0.f;
  for (const Box& b : others) {
    for (int k = 0; k < 3; ++k) {
      bool sideBySide = true; // os outros dois eixos precisam se sobrepor
      for (int j = 0; j < 3; ++j) if (j != k && !(get(alo, j) < get(b.hi, j) && get(ahi, j) > get(b.lo, j))) sideBySide = false;
      if (!sideBySide) continue;
      const float cands[2] = {get(b.hi, k) - get(alo, k),    // face de A encostando na face de cima de b
                              get(b.lo, k) - get(ahi, k)};   // face de A encostando na face de baixo de b
      for (float d : cands) {
        bool clash = false;
        for (const Box& c : others) if (overlapsAt(k, d, c)) { clash = true; break; }
        if (clash) continue;
        if (!found || std::fabs(d) < bestAbs) { found = true; bestAbs = std::fabs(d); bestK = k; bestDelta = d; }
      }
    }
  }
  if (!found) return false;
  tool::Vec3 dv{bestK == 0 ? bestDelta : 0.f, bestK == 1 ? bestDelta : 0.f, bestK == 2 ? bestDelta : 0.f};
  auto* t = w.get<Transform>(e);
  const tool::Entity par = ed.parentOf(e);
  if (w.alive(par)) { // o deslocamento e do mundo: converte para o espaco local do pai
    const tool::Vec4 l = inverseAffine(ed.worldMat(par)).mul({dv.x, dv.y, dv.z, 0.f});
    t->pos[0] += l.x; t->pos[1] += l.y; t->pos[2] += l.z;
  } else { t->pos[0] += dv.x; t->pos[1] += dv.y; t->pos[2] += dv.z; }
  return true;
}



// ---------------------------------------------------------------------------------------------------------------------
// SELECAO POR RAIO: dispara um raio da camera pelo pixel clicado e testa contra a caixa/esfera de cada objeto
// (no espaco LOCAL do objeto, entao rotacao e escala sao respeitadas). Retorna o objeto mais proximo atingido.
// Planos (chao) nao disputam o clique. Objetos sem malha (luz, camera, audio) usam o raio contra uma esfera pequena (0.35 m).
// Custo: O(objetos) por CLIQUE (nunca por quadro); modelos importados usam a caixa envolvente da malha.
// ---------------------------------------------------------------------------------------------------------------------
struct Ray { tool::Vec3 o, d; };

inline Ray rayFromScreen(const tool::Camera& c, ImVec2 org, ImVec2 size, ImVec2 mouse) {
  const float nx = ((mouse.x - org.x) / size.x) * 2.f - 1.f, ny = 1.f - ((mouse.y - org.y) / size.y) * 2.f;
  const tool::Vec3 f = tool::normalize(c.target - c.eye), r = tool::normalize(tool::cross(f, c.up)), u = tool::cross(r, f);
  const float th = std::tan(c.fovY * 0.5f), asp = size.x / size.y;
  return {c.eye, tool::normalize(f + r * (nx * th * asp) + u * (ny * th))};
}

// Raio x caixa alinhada aos eixos (metodo das "lajes"). Retorna a distancia t >= 0 ou -1.
inline float rayBox(const Ray& r, tool::Vec3 lo, tool::Vec3 hi) {
  float t0 = 0.f, t1 = 1e30f;
  const float o[3] = {r.o.x, r.o.y, r.o.z}, d[3] = {r.d.x, r.d.y, r.d.z}, l[3] = {lo.x, lo.y, lo.z}, h[3] = {hi.x, hi.y, hi.z};
  for (int i = 0; i < 3; ++i) {
    if (std::fabs(d[i]) < 1e-9f) { if (o[i] < l[i] || o[i] > h[i]) return -1.f; continue; }
    float a = (l[i] - o[i]) / d[i], b = (h[i] - o[i]) / d[i]; if (a > b) std::swap(a, b);
    t0 = std::max(t0, a); t1 = std::min(t1, b); if (t0 > t1) return -1.f;
  }
  return t0;
}
inline float raySphere(const Ray& r, tool::Vec3 c, float rad) {
  const tool::Vec3 oc = r.o - c; const float b = tool::dot(oc, r.d), cc = tool::dot(oc, oc) - rad * rad, disc = b * b - cc;
  if (disc < 0.f) return -1.f;
  const float t = -b - std::sqrt(disc); return t >= 0.f ? t : (-b + std::sqrt(disc) >= 0.f ? 0.f : -1.f);
}

inline tool::Entity pickByRay(Editor& ed, const Ray& ray) {
  tool::World& w = ed.scene.world();
  tool::Entity best; float bestT = 1e30f;
  w.eachEntity([&](tool::Entity e) {
    auto* t = w.get<Transform>(e); if (!t) return;
    const tool::Mat4 wm = ed.worldMat(e); const tool::Vec3 P{wm.m[3][0], wm.m[3][1], wm.m[3][2]};
    auto* m = w.get<MeshRenderer>(e);
    float hit = -1.f;
    if (!m) { hit = raySphere(ray, P, 0.35f); }
    else if (m->shape != 2) {
      // raio -> espaco local: inverso da matriz de MUNDO (vale para objetos com pai)
      const tool::Mat4 inv = inverseAffine(wm);
      auto pt = [&](tool::Vec3 v) { const tool::Vec4 q = inv.mul({v.x, v.y, v.z, 1.f}); return tool::Vec3{q.x, q.y, q.z}; };
      auto dir = [&](tool::Vec3 v) { const tool::Vec4 q = inv.mul({v.x, v.y, v.z, 0.f}); return tool::Vec3{q.x, q.y, q.z}; };
      const Ray lr{pt(ray.o), dir(ray.d)}; // t e o mesmo no espaco local (direcao nao normalizada de proposito)
      if (m->shape == 1) {
        const float a = tool::dot(lr.d, lr.d), b = tool::dot(lr.o, lr.d), c = tool::dot(lr.o, lr.o) - 0.25f, disc = b * b - a * c;
        if (disc >= 0.f && a > 1e-12f) { const float s = std::sqrt(disc); const float t0 = (-b - s) / a; hit = t0 >= 0.f ? t0 : ((-b + s) / a >= 0.f ? 0.f : -1.f); }
      } else {
        tool::Vec3 lo{-0.5f, -0.5f, -0.5f}, hi{0.5f, 0.5f, 0.5f};
        if (m->shape == 3) if (const tool::Mesh* mesh = ed.meshOf(*m); mesh && !mesh->pos.empty()) {
          lo = hi = mesh->pos[0];
          for (auto& p : mesh->pos) { lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)}; hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)}; }
        }
        hit = rayBox(lr, lo, hi);
      }
    }
    if (hit >= 0.f && hit < bestT) { bestT = hit; best = e; }
  });
  return best;
}

} // namespace editor
