#pragma once

// Formas das janelas Scene e Jogo montadas a partir dos dados do Editor: particulas (e a trajetoria do objeto
// selecionado), UI do jogo, objetos cortados pela camera de jogo e formas de fisica. Cada funcao so grava em uma
// view::ShapeList (ou numa celula do jogo) e nao desenha: quem desenha e o painel SpriteUI. As contas e as cores
// sao as mesmas que fx.hpp e extra_ui.hpp usavam com ImDrawList (que continuam so na build sem SpriteUI).
//
// Exemplo: uma particula de fogo de 4 px vira um disco com halo. Uma UI "Vida" com fundo vermelho vira um
// retangulo, e o texto "Vida" entra por cima, centrado, dentro da celula da camera.
//
// Como funciona:
//   buildParticles:  particulas da vista; a trajetoria do objeto selecionado (Scene) sai antes.
//   buildUi:         elementos de UI de cada UiCanvas da camera (texto, painel, barra e botao). Botao vira um
//                    acerto (UiHit) na celula: o clique no Play devolve a acao dele.
//   buildCulled:     contorno vermelho dos objetos que a camera de jogo principal nao desenha.
//   buildPhysics:    contornos dos colisores (esferas e caixas) quando "Draw colliders" esta ligado.

#include "editor_state.hpp"
#include "fx.hpp"                 // fxdetail (trajetoria, alfa e cor) e uiFormat
#include "game_view_panel.hpp"
#include "scene_view_model.hpp"
#include "view_shapes.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace editor::view_overlays {

namespace vm = editor::scene_view_model;
namespace view = editor::view;
namespace gp = editor::game_view_panel;

inline constexpr float kUiFont = 15.f;   // fonte padrao do editor (AddFontFromFileTTF, 15 px)

// Contorno de retangulo com 4 linhas (cantos retos).
inline void outlineRect(view::ShapeList& out, vm::Pixel a, vm::Pixel b, view::Color col) {
  out.line({a.x, a.y}, {b.x, a.y}, 1.f, col);
  out.line({b.x, a.y}, {b.x, b.y}, 1.f, col);
  out.line({b.x, b.y}, {a.x, b.y}, 1.f, col);
  out.line({a.x, b.y}, {a.x, a.y}, 1.f, col);
}

// Particulas e trajetoria. `selected` mostra a trajetoria do objeto selecionado na Scene.
inline void buildParticles(Editor& ed, const tool::Camera& cam, sui::Box area, tool::Entity selected, view::ShapeList& out) {
  if (area.w < 2.f || area.h < 2.f) return;
  tool::World& w = ed.scene.world();
  const tool::Mat4 vp = cam.projection(area.w, area.h) * tool::Mat4::lookAt(cam.eye, cam.target, cam.up);
  const tool::Vec3 right = tool::normalize(tool::cross(tool::normalize(cam.target - cam.eye), cam.up));
  auto toScr = [&](tool::Vec3 p, vm::Pixel& s) {
    const tool::Vec4 c = vp.mul({p.x, p.y, p.z, 1.f});
    if (!cam.ortho && c.w < cam.nearZ) return false;
    s = {area.x + (c.x / c.w * 0.5f + 0.5f) * area.w, area.y + (1.f - (c.y / c.w * 0.5f + 0.5f)) * area.h};
    return true;
  };
  w.each<ParticleComp>([&](tool::Entity e, ParticleComp& pc) {
    if (w.has<Inactive>(e)) return;
    if (e == selected && pc.route && pc.routeCount >= 2) {
      const tool::Mat4 wm = ed.worldMat(e);
      vm::Pixel prev{};
      bool hasPrev = false;
      for (int k = 0; k <= 64; ++k) {
        const tool::Vec3 l = fxdetail::routeSample(pc, static_cast<float>(k) / 64.f);
        const tool::Vec4 wp = wm.mul({l.x, l.y, l.z, 1.f});
        vm::Pixel s{};
        if (!toScr({wp.x, wp.y, wp.z}, s)) { hasPrev = false; continue; }
        if (hasPrev) out.line(prev, s, 2.f, view::Rgba(255, 220, 80, 230));
        prev = s;
        hasPrev = true;
      }
      for (int i = 0; i < pc.routeCount; ++i) {
        const tool::Vec4 wp = wm.mul({pc.routePts[i][0], pc.routePts[i][1], pc.routePts[i][2], 1.f});
        vm::Pixel s{};
        if (toScr({wp.x, wp.y, wp.z}, s)) {
          out.circleFilled(s, 4.f, view::Rgba(255, 220, 80, 255));
          out.circle(s, 6.f, view::Rgba(0, 0, 0, 200), 1.f);
        }
      }
    }
    auto it = ed.particles.find(e.index);
    if (it == ed.particles.end()) return;
    for (const Editor::Particle& p : it->second.list) {
      const float t = std::clamp(p.age / std::max(p.life, 1e-4f), 0.f, 1.f);
      const float sz = pc.size + (pc.sizeEnd - pc.size) * t;
      if (sz <= 1e-3f) continue;
      const float a = fxdetail::particleAlpha(pc, p);
      if (a <= 0.003f) continue;
      vm::Pixel s{};
      if (!toScr(p.p, s)) continue;
      vm::Pixel sr{};
      if (!toScr(p.p + right * (sz * 0.5f), sr)) continue;
      const float r = std::max(1.f, std::hypot(sr.x - s.x, sr.y - s.y));
      if (s.x + r < area.x || s.x - r > area.x + area.w || s.y + r < area.y || s.y - r > area.y + area.h) { ++ed.prof.counters.culled; continue; }
      const tool::Vec3 c = fxdetail::particleColor(pc, t);
      const view::Color col = view::rgbaF(c.x, c.y, c.z, a);
      switch (pc.form) {
        case 1: out.rectFilled({s.x - r, s.y - r}, {s.x + r, s.y + r}, col); break;
        case 2: out.triangleFilled({s.x, s.y - r * 1.2f}, {s.x - r, s.y + r * 0.8f}, {s.x + r, s.y + r * 0.8f}, col); break;
        case 3: {   // estrela: leque de triangulos a partir do centro (a estrela e vista de dentro para fora)
          vm::Pixel pts[10];
          for (int k = 0; k < 10; ++k) {
            const float ang = -1.5708f + static_cast<float>(k) * 0.6283185f, rr = (k % 2) ? r * 0.45f : r * 1.2f;
            pts[k] = {s.x + std::cos(ang) * rr, s.y + std::sin(ang) * rr};
          }
          for (int k = 0; k < 10; ++k) out.triangleFilled(s, pts[k], pts[(k + 1) % 10], col);
          break;
        }
        case 4: out.circle(s, r, col, std::max(1.f, r * 0.35f)); break;
        case 5: {
          vm::Pixel q = s;
          vm::Pixel qb{};
          if (toScr(p.p - p.v * 0.04f, qb)) q = qb;
          out.line(q, s, std::max(1.f, r * 0.8f), col);
          break;
        }
        case 6:   // fumaca: halo leve por fora, borda densa e miolo quase transparente
          out.circleFilled(s, r, view::rgbaF(c.x, c.y, c.z, a * 0.22f));
          out.circle(s, r * 0.82f, view::rgbaF(c.x, c.y, c.z, a * 0.7f), std::max(1.f, r * 0.4f));
          out.circleFilled(s, r * 0.4f, view::rgbaF(c.x, c.y, c.z, a * 0.1f));
          break;
        default:   // bolinha suave: halo + nucleo
          out.circleFilled(s, r * 1.6f, view::rgbaF(c.x, c.y, c.z, a * 0.25f));
          out.circleFilled(s, r, col);
          break;
      }
    }
  });
}

// UI do jogo de uma camera: desenha os UiCanvas que pertencem a camEnt dentro da celula. Os botoes viram acertos na
// celula (com a cor de hover/pressionado vinda de hoverIdx/pressedIdx, indices de botao da celula).
inline void buildUi(Editor& ed, tool::Entity camEnt, sui::Box area, bool interactive, int hoverIdx, int pressedIdx, gp::Cell& cell) {
  tool::World& w = ed.scene.world();
  const auto* cn = w.get<Name>(camEnt);
  view::ShapeList& out = cell.overlay;
  w.each<UiCanvas>([&](tool::Entity, UiCanvas& cv) {
    if (!cv.visible) return;
    if (cv.camera[0]) {
      if (!cn || std::string(cv.camera) != cn->text) return;
    } else if (!ed.isPrincipal(camEnt)) {
      return;
    }
    for (int i = 0; i < cv.count && i < kMaxUi; ++i) {
      UiElem& it = cv.items[i];
      const vm::Pixel a{area.x + it.x * area.w, area.y + it.y * area.h};
      const vm::Pixel b{a.x + it.w * area.w, a.y + it.h * area.h};
      const std::string txt = uiFormat(ed, it.text);
      const float fs = kUiFont * it.fontScale * std::max(0.5f, area.h / 600.f);
      const view::Color col = view::rgbaF(it.color[0], it.color[1], it.color[2], it.color[3]);
      auto label = [&](view::Color c) { out.text({(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f - fs * 0.5f}, txt, fs, c, true, b.x - a.x); };
      switch (static_cast<UiKind>(it.kind)) {
        case UiKind::Text: out.text(a, txt, fs, col); break;
        case UiKind::Panel:
          out.rectFilled(a, b, col, 4.f);
          if (!txt.empty()) label(view::Rgba(255, 255, 255));
          break;
        case UiKind::Bar: {
          const auto vit = ed.vars.find(it.action);
          const float v = vit == ed.vars.end() ? 0.f : vit->second;
          const float f = std::clamp(v / it.maxValue, 0.f, 1.f);
          out.rectFilled(a, b, view::Rgba(20, 20, 20, 180), 3.f);
          out.rectFilled(a, {a.x + (b.x - a.x) * f, b.y}, col, 3.f);
          outlineRect(out, a, b, view::Rgba(255, 255, 255, 120));
          break;
        }
        case UiKind::Button: {
          const int k = static_cast<int>(cell.hits.size());
          cell.hits.push_back({a, b, it.action});
          const bool hov = interactive && hoverIdx == k, down = interactive && pressedIdx == k;
          const float m = down ? 0.7f : hov ? 1.15f : 1.f;
          out.rectFilled(a, b, view::rgbaF(std::min(1.f, it.color[0] * m), std::min(1.f, it.color[1] * m), std::min(1.f, it.color[2] * m), it.color[3]), 6.f);
          outlineRect(out, a, b, view::Rgba(255, 255, 255, 160));
          label(view::Rgba(0, 0, 0));
          break;
        }
        default: break;
      }
    }
  });
}

// Segmento 3D recortado no plano da camera, como linha da vista.
inline void axisSeg(const vm::Projection& proj, tool::Vec3 a, tool::Vec3 b, view::Color col, float th, view::ShapeList& out) {
  vm::Pixel pa{}, pb{};
  if (proj.segment(a, b, pa, pb)) out.line(pa, pb, th, col);
}

// Texto de um ponto 3D (legenda dos eixos), centrado no ponto projetado.
inline void labelAt(const vm::Projection& proj, tool::Vec3 p, const char* text, view::Color col, view::ShapeList& out) {
  vm::Pixel s{};
  if (proj.point(p, s)) out.text(s, text, kUiFont, col);
}

// Objetos que a camera de jogo principal NAO desenha (fora do campo de visao ou alem da distancia): contorno vermelho.
inline void buildCulled(Editor& ed, const vm::Projection& proj, sui::Box area, view::ShapeList& out) {
  const auto cams = ed.principalCameras();
  if (cams.empty()) return;
  const tool::Camera gc = ed.cameraFor(cams[0]);
  const auto planes = Editor::framePlanes(gc, std::max(1, static_cast<int>(area.w)), std::max(1, static_cast<int>(area.h)));
  tool::World& w = ed.scene.world();
  w.each<MeshRenderer>([&](tool::Entity e, MeshRenderer& mr) {
    if (w.has<Inactive>(e)) return;
    const tool::Mesh* m = ed.meshOf(mr);
    if (!m) return;
    const Editor::Bounds bd = ed.meshBounds(*m);
    const tool::Mat4 wm = ed.worldMat(e);
    const float sc = std::max({std::hypot(wm.m[0][0], wm.m[0][1], wm.m[0][2]), std::hypot(wm.m[1][0], wm.m[1][1], wm.m[1][2]), std::hypot(wm.m[2][0], wm.m[2][1], wm.m[2][2])});
    const tool::Vec3 c = wm.transformPoint(bd.center);
    const float r = bd.radius * sc;
    bool culled = false;
    for (const tool::Vec4& p : planes) {
      if (p.x * c.x + p.y * c.y + p.z * c.z + p.w < -r) { culled = true; break; }
    }
    if (!culled) return;   // dentro de todos os planos: a camera desenha
    const tool::Vec3 lo{c.x - r, c.y - r, c.z - r}, hi{c.x + r, c.y + r, c.z + r};
    const tool::Vec3 k[8] = {{lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z},
                             {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}};
    const int edges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (const auto& pr : edges) axisSeg(proj, k[pr[0]], k[pr[1]], view::Rgba(255, 70, 70, 230), 1.f, out);
  });
}

// Grade do chao e os eixos X, Y, Z (Scene e Jogo com a grade ligada).
inline void buildGrid(const tool::Camera& cam, const vm::Projection& proj, view::ShapeList& out) {
  for (const vm::GridLine& l : vm::gridLines(cam.eye)) {
    vm::Pixel a{}, b{};
    if (proj.segment(l.a, l.b, a, b)) out.line(a, b, 1.f, view::Rgba(200, 200, 215, l.alpha));
  }
  axisSeg(proj, {0, 0, 0}, {1.e4f, 0, 0}, view::Rgba(230, 70, 70, 190), 1.6f, out);
  axisSeg(proj, {0, 0, 0}, {0, 0, 1.e4f}, view::Rgba(70, 110, 240, 190), 1.6f, out);
  axisSeg(proj, {0, 0, 0}, {0, 6, 0}, view::Rgba(80, 220, 90, 220), 1.6f, out);
  labelAt(proj, {6.4f, 0, 0}, "X", view::Rgba(230, 90, 90, 255), out);
  labelAt(proj, {0, 6.4f, 0}, "Y", view::Rgba(100, 230, 110, 255), out);
  labelAt(proj, {0, 0, 6.4f}, "Z", view::Rgba(100, 140, 255, 255), out);
}

// Circulo no plano XZ (24 lados), como o circleXZ do ImGui.
inline void circleXZ(const vm::Projection& proj, tool::Vec3 c, float r, view::Color col, view::ShapeList& out) {
  constexpr int kSides = 24;
  for (int i = 0; i < kSides; ++i) {
    const float a0 = 6.2831853f * static_cast<float>(i) / kSides, a1 = 6.2831853f * static_cast<float>(i + 1) / kSides;
    axisSeg(proj, {c.x + r * std::cos(a0), c.y, c.z + r * std::sin(a0)}, {c.x + r * std::cos(a1), c.y, c.z + r * std::sin(a1)}, col, 1.f, out);
  }
}

// Icones dos objetos sem malha: camera (piramide), sol e luzes (cruz e seta), fonte de som (circulo). O objeto
// selecionado sai amarelo. Antes era drawGizmos (ImDrawList).
inline void buildSceneIcons(Editor& ed, const vm::Projection& proj, view::ShapeList& out) {
  tool::World& w = ed.scene.world();
  const tool::TagId tp = ed.scene.tag("Principal");
  auto colorFor = [&](tool::Entity e, view::Color base) { return e == ed.selected ? view::Rgba(255, 230, 80) : base; };
  w.each<CameraComp, Transform>([&](tool::Entity e, CameraComp& c, Transform& t) {
    const tool::Vec3 p{t.pos[0], t.pos[1], t.pos[2]}, f = forwardOf(t), u = upOf(t), r = tool::normalize(tool::cross(f, u));
    const float d = 1.1f, hh = std::tan(c.fovDeg * kDeg2Rad * 0.5f) * d, hw = hh * 16.f / 9.f;
    const tool::Vec3 ctr = p + f * d, corner[4] = {ctr + r * hw + u * hh, ctr - r * hw + u * hh, ctr - r * hw - u * hh, ctr + r * hw - u * hh};
    const view::Color col = colorFor(e, w.hasTag(e, tp) ? view::Rgba(255, 150, 40) : view::Rgba(170, 170, 180));
    for (int i = 0; i < 4; ++i) {
      axisSeg(proj, p, corner[i], col, 1.f, out);
      axisSeg(proj, corner[i], corner[(i + 1) % 4], col, 1.f, out);
    }
    axisSeg(proj, ctr + u * hh, ctr + u * (hh * 1.4f), col, 1.f, out);
  });
  w.each<GlobalLight, Transform>([&](tool::Entity e, GlobalLight& g, Transform& t) {
    const tool::Vec3 p{t.pos[0], t.pos[1], t.pos[2]}, d = sunDirection(g.azimuth, g.elevation);
    const view::Color col = colorFor(e, view::Rgba(255, 220, 90));
    for (tool::Vec3 a : {tool::Vec3{.4f, 0, 0}, tool::Vec3{0, .4f, 0}, tool::Vec3{0, 0, .4f}}) axisSeg(proj, p - a, p + a, col, 1.f, out);
    axisSeg(proj, p, p + d * 2.5f, col, 2.f, out);
    const tool::Vec3 tip = p + d * 2.5f, s = tool::normalize(tool::cross(d, {0, 1, 0}));
    axisSeg(proj, tip, tip - d * 0.4f + s * 0.2f, col, 1.f, out);
    axisSeg(proj, tip, tip - d * 0.4f - s * 0.2f, col, 1.f, out);
  });
  w.each<LightComp, Transform>([&](tool::Entity e, LightComp& lc, Transform& t) {
    const tool::Vec3 p{t.pos[0], t.pos[1], t.pos[2]};
    const view::Color col = colorFor(e, view::Rgba(255, 190, 120));
    for (tool::Vec3 a : {tool::Vec3{.25f, 0, 0}, tool::Vec3{0, .25f, 0}, tool::Vec3{0, 0, .25f}}) axisSeg(proj, p - a, p + a, col, 1.f, out);
    if (lc.light.type != tool::LightType::Point) axisSeg(proj, p, p + ed.lightDir(e, lc) * 1.5f, col, 1.5f, out);
    if (e == ed.selected && lc.light.type == tool::LightType::Point) circleXZ(proj, p, lc.light.range, view::Rgba(255, 190, 120, 90), out);
  });
  w.each<AudioSource, Transform>([&](tool::Entity e, AudioSource& a, Transform& t) {
    const tool::Vec3 p{t.pos[0], t.pos[1], t.pos[2]};
    const view::Color col = colorFor(e, view::Rgba(110, 210, 140));
    circleXZ(proj, p, 0.3f, col, out);
    if (e == ed.selected && a.spatial) {
      circleXZ(proj, p, a.minDistance, view::Rgba(110, 210, 140, 120), out);
      circleXZ(proj, p, a.maxDistance, view::Rgba(110, 210, 140, 60), out);
    }
  });
}

// Colisores: esfera = tres circulos, caixa = doze arestas. Cor pelo tipo de corpo (ou gatilho).
inline void buildPhysics(Editor& ed, const vm::Projection& proj, view::ShapeList& out) {
  if (!ed.sceneInfo.physics.drawColliders) return;
  tool::World& w = ed.scene.world();
  w.each<PhysicsComp, Transform>([&](tool::Entity e, PhysicsComp& p, Transform& t) {
    const Newtoon::Collider c = ed.colliderFor(e);
    const Newtoon::Mat3 R = eulerToQuat(t.rot).ToMat3();
    const Newtoon::Vec3 off = R * c.offset;
    const tool::Vec3 ctr{t.pos[0] + off.x, t.pos[1] + off.y, t.pos[2] + off.z};
    const view::Color col = p.trigger ? view::Rgba(230, 90, 230, 200) : p.body == 0 ? view::Rgba(90, 160, 255, 170) : p.body == 2 ? view::Rgba(255, 210, 80, 200) : view::Rgba(110, 230, 120, 190);
    if (c.type == Newtoon::ShapeType::Sphere) {
      const tool::Vec3 ax[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
      for (int a = 0; a < 3; ++a) {
        const tool::Vec3 u = ax[(a + 1) % 3], q = ax[(a + 2) % 3];
        tool::Vec3 prev = ctr + u * c.radius;
        for (int s = 1; s <= 24; ++s) {
          const float an = static_cast<float>(s) * 6.2831853f / 24.f;
          const tool::Vec3 cur = ctr + u * (std::cos(an) * c.radius) + q * (std::sin(an) * c.radius);
          axisSeg(proj, prev, cur, col, 1.f, out);
          prev = cur;
        }
      }
      return;
    }
    tool::Vec3 corner[8];
    for (int i = 0; i < 8; ++i) {
      const Newtoon::Vec3 l{(i & 1 ? 1.f : -1.f) * c.extents.x, (i & 2 ? 1.f : -1.f) * c.extents.y, (i & 4 ? 1.f : -1.f) * c.extents.z};
      const Newtoon::Vec3 wv = R * l;
      corner[i] = {ctr.x + wv.x, ctr.y + wv.y, ctr.z + wv.z};
    }
    for (int i = 0; i < 8; ++i) {
      for (int b = 0; b < 3; ++b) {
        if (!(i & (1 << b))) axisSeg(proj, corner[i], corner[i | (1 << b)], col, 1.f, out);
      }
    }
  });
}

}  // namespace editor::view_overlays
