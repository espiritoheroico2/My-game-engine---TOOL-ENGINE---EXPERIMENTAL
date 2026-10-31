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

struct GizmoState {
  int hot = -1;          // alca sob o mouse: 0..2 = eixos X/Y/Z, 3 = centro (escala uniforme)
  int active = -1;       // alca sendo arrastada (-1 = nenhuma)
  Transform start;       // Transform no inicio do arrasto
  ImVec2 mouse0{0, 0};   // mouse no inicio do arrasto
  float angle0 = 0.f;    // (girar) angulo do mouse em torno do centro no inicio
  tool::Vec3 startWorld; // posicao de MUNDO no inicio do arrasto (objeto com pai: o Transform e local)
  tool::Mat4 parentInv;  // inversa da matriz de mundo do pai (identidade se nao tem pai)
  bool hasParent = false;
  bool dragging() const { return active >= 0; }
};

// Desenha o gizmo do objeto selecionado e trata o mouse. Retorna true se o gizmo "consumiu" o clique (nao selecionar outro objeto).
// `hovered` = o mouse esta sobre a imagem da janela Scene.
inline bool transformGizmo(Editor& ed, ImDrawList* dl, const ViewXf& v, GizmoState& gs, bool hovered) {
  tool::World& w = ed.scene.world();
  if (!w.alive(ed.selected)) { gs = GizmoState{}; return false; }
  auto* t = w.get<Transform>(ed.selected);
  if (!t) { gs = GizmoState{}; return false; }
  ImGuiIO& io = ImGui::GetIO();
  const tool::Vec3 P = ed.worldPos(ed.selected);
  ImVec2 c0; if (!v.point(P, c0)) return false;
  const float len = std::max(0.2f, tool::length(P - v.eye) * 0.16f); // comprimento do eixo no mundo: tamanho ~constante na tela
  const tool::Vec3 ax[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
  const ImU32 base[3] = {IM_COL32(235, 70, 70, 255), IM_COL32(90, 220, 90, 255), IM_COL32(80, 120, 245, 255)};
  const ImU32 hi = IM_COL32(255, 235, 90, 255);
  const int mode = ed.gizmoMode;
  const bool ctrl = io.KeyCtrl;

  // ---- 1) qual alca esta sob o mouse? (so se nao estiver arrastando)
  int hot = -1;
  if (!gs.dragging() && hovered) {
    float best = 9.f; // raio de captura em pixels
    for (int i = 0; i < 3; ++i) {
      if (mode == 1) { // anel: poligono de 40 lados no plano perpendicular ao eixo
        const tool::Vec3 u = ax[(i + 1) % 3], r = ax[(i + 2) % 3];
        ImVec2 prev; bool havePrev = false;
        for (int k = 0; k <= 40; ++k) {
          const float a = 6.2831853f * static_cast<float>(k) / 40.f;
          ImVec2 s; if (!v.point(P + (u * std::cos(a) + r * std::sin(a)) * len, s)) { havePrev = false; continue; }
          if (havePrev) { const float d = distToSegment(io.MousePos, prev, s); if (d < best) { best = d; hot = i; } }
          prev = s; havePrev = true;
        }
      } else {
        ImVec2 s; if (!v.point(P + ax[i] * len, s)) continue;
        const float d = distToSegment(io.MousePos, c0, s); if (d < best) { best = d; hot = i; }
      }
    }
    if (mode == 2 && std::hypot(io.MousePos.x - c0.x, io.MousePos.y - c0.y) < 9.f) hot = 3;
    gs.hot = hot;
  } else if (!gs.dragging()) gs.hot = -1;

  // ---- 2) comeca o arrasto
  if (hot >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    gs.active = hot; gs.start = *t; gs.mouse0 = io.MousePos; gs.startWorld = P;
    const tool::Entity par = ed.parentOf(ed.selected); gs.hasParent = ed.scene.world().alive(par); gs.parentInv = gs.hasParent ? inverseAffine(ed.worldMat(par)) : tool::Mat4{};
    gs.angle0 = std::atan2(io.MousePos.y - c0.y, io.MousePos.x - c0.x);
  }
  if (gs.dragging() && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) gs = GizmoState{};

  // ---- 3) arrasto: atualiza o Transform
  std::string readout;
  if (gs.dragging()) {
    const int a = gs.active;
    const ImVec2 d{io.MousePos.x - gs.mouse0.x, io.MousePos.y - gs.mouse0.y};
    const tool::Vec3 P0 = gs.startWorld;
    if (mode == 1 && a < 3) { // girar: angulo do mouse em torno do centro, no sentido que combina com a vista
      const float cur = std::atan2(io.MousePos.y - c0.y, io.MousePos.x - c0.x);
      float delta = cur - gs.angle0;
      while (delta > 3.14159265f) delta -= 6.2831853f;
      while (delta < -3.14159265f) delta += 6.2831853f;
      const float facing = tool::dot(ax[a], v.eye - P0) > 0.f ? -1.f : 1.f; // o eixo aponta para a camera? entao o sentido horario na tela e negativo
      float deg = delta * facing * 57.2957795f;
      if (ctrl) deg = std::round(deg / 15.f) * 15.f;
      t->rot[a] = gs.start.rot[a] + deg;
      while (t->rot[a] > 180.f) t->rot[a] -= 360.f;
      while (t->rot[a] < -180.f) t->rot[a] += 360.f;
      readout = std::string(a == 0 ? "X" : a == 1 ? "Y" : "Z") + " rot " + std::to_string(static_cast<int>(std::round(t->rot[a]))) + " deg";
    } else if (mode == 2 && a == 3) { // escala uniforme: arrastar para a direita/cima aumenta
      float k = 1.f + (d.x - d.y) * 0.006f;
      k = std::clamp(k, 0.01f, 100.f);
      for (int i = 0; i < 3; ++i) { float s = gs.start.scale[i] * k; if (ctrl) s = std::round(s / 0.1f) * 0.1f; t->scale[i] = std::clamp(s, 0.01f, 10000.f); }
      char b[48]; std::snprintf(b, sizeof b, "scale x%.2f", static_cast<double>(k)); readout = b;
    } else if (a < 3) { // mover / escalar num eixo: projeta o movimento do mouse na direcao do eixo na tela
      ImVec2 s0, s1;
      if (v.point(P0, s0) && v.point(P0 + ax[a] * len, s1)) {
        const float sx = s1.x - s0.x, sy = s1.y - s0.y, sl2 = sx * sx + sy * sy;
        if (sl2 > 4.f) {
          const float along = (d.x * sx + d.y * sy) / sl2 * len; // metros ao longo do eixo
          if (mode == 0) {
            float val = gs.start.pos[a] + along; if (ctrl) val = std::round(val / 0.25f) * 0.25f;
            if (gs.hasParent) { // o eixo do gizmo e do MUNDO: converte o deslocamento para o espaco do pai
              const float al = ctrl ? std::round(along / 0.25f) * 0.25f : along; const tool::Vec4 dl4 = gs.parentInv.mul({ax[a].x * al, ax[a].y * al, ax[a].z * al, 0.f});
              t->pos[0] = gs.start.pos[0] + dl4.x; t->pos[1] = gs.start.pos[1] + dl4.y; t->pos[2] = gs.start.pos[2] + dl4.z; val = al;
            } else t->pos[a] = val;
            char b[48]; std::snprintf(b, sizeof b, "%s %.2f m", a == 0 ? "X" : a == 1 ? "Y" : "Z", static_cast<double>(val)); readout = b;
          } else if (mode == 2) {
            float val = gs.start.scale[a] * (1.f + along / len); if (ctrl) val = std::round(val / 0.1f) * 0.1f;
            t->scale[a] = std::clamp(val, 0.01f, 10000.f);
            char b[48]; std::snprintf(b, sizeof b, "scale %s %.2f", a == 0 ? "X" : a == 1 ? "Y" : "Z", static_cast<double>(t->scale[a])); readout = b;
          }
        }
      }
    }
  }

  // ---- 4) desenho (sempre na posicao ATUAL do objeto)
  const tool::Vec3 Pc = ed.worldPos(ed.selected);
  ImVec2 c1; if (!v.point(Pc, c1)) return gs.dragging();
  const int shown = gs.dragging() ? gs.active : gs.hot;
  for (int i = 0; i < 3; ++i) {
    const ImU32 col = shown == i ? hi : base[i];
    const float th = shown == i ? 3.5f : 2.5f;
    if (mode == 1) {
      const tool::Vec3 u = ax[(i + 1) % 3], r = ax[(i + 2) % 3];
      for (int k = 0; k < 40; ++k) {
        const float a0 = 6.2831853f * static_cast<float>(k) / 40.f, a1 = 6.2831853f * static_cast<float>(k + 1) / 40.f;
        line3(dl, v, Pc + (u * std::cos(a0) + r * std::sin(a0)) * len, Pc + (u * std::cos(a1) + r * std::sin(a1)) * len, col, th);
      }
    } else {
      const tool::Vec3 tip = Pc + ax[i] * len;
      line3(dl, v, Pc, tip, col, th);
      ImVec2 s; if (!v.point(tip, s)) continue;
      if (mode == 0) { // ponta de seta: pequeno triangulo
        const float dx = s.x - c1.x, dy = s.y - c1.y, l = std::max(1.f, std::hypot(dx, dy)), ux = dx / l, uy = dy / l;
        dl->AddTriangleFilled({s.x + ux * 10.f, s.y + uy * 10.f}, {s.x - uy * 5.f, s.y + ux * 5.f}, {s.x + uy * 5.f, s.y - ux * 5.f}, col);
      } else dl->AddRectFilled({s.x - 5.f, s.y - 5.f}, {s.x + 5.f, s.y + 5.f}, col); // escala: cubinho
    }
  }
  if (mode == 2) dl->AddRectFilled({c1.x - 6.f, c1.y - 6.f}, {c1.x + 6.f, c1.y + 6.f}, shown == 3 ? hi : IM_COL32(230, 230, 230, 255));
  else dl->AddCircleFilled(c1, 3.f, IM_COL32(255, 255, 255, 220));
  if (!readout.empty()) dl->AddText({io.MousePos.x + 14.f, io.MousePos.y + 12.f}, IM_COL32(255, 245, 160, 255), readout.c_str());
  return gs.dragging() || hot >= 0;
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
