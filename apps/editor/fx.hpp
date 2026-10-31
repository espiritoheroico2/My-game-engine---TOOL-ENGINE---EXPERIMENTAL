// fx.hpp - Particulas (CPU, integrador simples com gravidade/quique da fisica Newtoon da cena) e UI de jogo (overlay ImGui por camera).
// Particulas e UI sao DESENHADAS por cima da imagem (ImDrawList): sem teste de profundidade contra a cena (limite conhecido; ver docs/UI_FX.md).
#pragma once
#include "editor_state.hpp"
#include "viewport_tools.hpp"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace editor {

namespace fxdetail {
inline float rnd(uint32_t& s) { s = s * 1664525u + 1013904223u; return static_cast<float>((s >> 8) & 0xFFFFFF) / 16777216.f; } // 0..1
}

inline void Editor::updateParticles(float dt) {
  tool::World& w = scene.world();
  dt = std::clamp(dt, 0.f, 0.1f);
  std::vector<uint32_t> alive;
  w.each<ParticleComp>([&](tool::Entity e, ParticleComp& pc) {
    alive.push_back(e.index); ParticleSim& sim = particles[e.index];
    const tool::Mat4 wm = worldMat(e); const tool::Vec3 org{wm.m[3][0], wm.m[3][1], wm.m[3][2]};
    const tool::Vec4 up4 = wm.mul({0.f, 1.f, 0.f, 0.f}); tool::Vec3 up{up4.x, up4.y, up4.z}; const float ul = tool::length(up); up = ul > 1e-5f ? up * (1.f / ul) : tool::Vec3{0.f, 1.f, 0.f};
    // base ortonormal (u, v) perpendicular ao eixo de emissao
    const tool::Vec3 ref = std::fabs(up.y) < 0.99f ? tool::Vec3{0.f, 1.f, 0.f} : tool::Vec3{1.f, 0.f, 0.f};
    tool::Vec3 u = tool::cross(up, ref); u = u * (1.f / std::max(1e-5f, tool::length(u))); const tool::Vec3 v = tool::cross(up, u);
    const tool::Vec3 g{sceneInfo.physics.gravity[0] * pc.gravityScale, sceneInfo.physics.gravity[1] * pc.gravityScale, sceneInfo.physics.gravity[2] * pc.gravityScale};
    const size_t cap = static_cast<size_t>(std::clamp(pc.maxParticles, 1, 5000));
    if (pc.emitting) {
      sim.acc += pc.rate * dt;
      while (sim.acc >= 1.f && sim.list.size() < cap) {
        sim.acc -= 1.f;
        const float ang = fxdetail::rnd(sim.seed) * 6.2831853f, cone = std::sin(pc.spread * 0.0174533f * std::sqrt(fxdetail::rnd(sim.seed)));
        const tool::Vec3 dir = up * std::sqrt(std::max(0.f, 1.f - cone * cone)) + u * (std::cos(ang) * cone) + v * (std::sin(ang) * cone);
        Particle p; p.p = org; p.v = dir * (pc.speed * (0.75f + 0.5f * fxdetail::rnd(sim.seed))); p.life = pc.lifetime * (0.8f + 0.4f * fxdetail::rnd(sim.seed)); sim.list.push_back(p);
      }
      if (sim.list.size() >= cap) sim.acc = 0.f;
          }
    for (size_t i = 0; i < sim.list.size();) {
      Particle& p = sim.list[i]; p.age += dt;
      if (p.age >= p.life) { sim.list[i] = sim.list.back(); sim.list.pop_back(); continue; }
      p.v = p.v + g * dt; p.p = p.p + p.v * dt;
      if (pc.bounce > 0.f && p.p.y < pc.floorY) { p.p.y = pc.floorY; p.v.y = -p.v.y * pc.bounce; p.v.x *= 0.9f; p.v.z *= 0.9f; }
      ++i;
    }
  });
  for (auto it = particles.begin(); it != particles.end();) { if (std::find(alive.begin(), alive.end(), it->first) == alive.end()) it = particles.erase(it); else ++it; }
}

inline void drawParticles(ImDrawList* dl, const ViewXf& xf, Editor& ed) {
  tool::World& w = ed.scene.world();
  w.each<ParticleComp>([&](tool::Entity e, ParticleComp& pc) {
    auto it = ed.particles.find(e.index); if (it == ed.particles.end()) return;
    const auto& list = it->second.list;
    // mais distantes primeiro (alpha correto entre particulas)
    std::vector<std::pair<float, const Editor::Particle*>> order; order.reserve(list.size());
    for (const auto& p : list) { const tool::Vec4 c = xf.vp.mul({p.p.x, p.p.y, p.p.z, 1.f}); if (c.w > 0.05f) order.push_back({c.w, &p}); }
    std::sort(order.begin(), order.end(), [](auto& a, auto& b) { return a.first > b.first; });
    for (auto& [cw, pp] : order) {
      const float t = std::clamp(pp->age / pp->life, 0.f, 1.f);
      const float sz = pc.size + (pc.sizeEnd - pc.size) * t; const float rad = std::max(0.6f, sz * xf.focal / cw * xf.size.y * 0.5f);
      ImVec2 s; if (!xf.point(pp->p, s)) continue;
      auto mix = [&](int k) { return std::clamp(pc.color[k] + (pc.colorEnd[k] - pc.color[k]) * t, 0.f, 1.f); };
      const float a = pc.alpha * (1.f - t * t);
      dl->AddCircleFilled(s, rad, IM_COL32(static_cast<int>(mix(0) * 255), static_cast<int>(mix(1) * 255), static_cast<int>(mix(2) * 255), static_cast<int>(a * 255)), rad > 6.f ? 16 : 8);
    }
  });
}

// "{nome}" no texto vira o valor da variavel do jogo (ed.vars); suporta varias por texto.
inline std::string uiFormat(const Editor& ed, const char* text) {
  std::string out, src = text;
  for (size_t i = 0; i < src.size();) {
    if (src[i] == '{') { const size_t e = src.find('}', i); if (e != std::string::npos && e - i < 40) {
      const auto it = ed.vars.find(src.substr(i + 1, e - i - 1)); char buf[32]; std::snprintf(buf, sizeof buf, "%g", static_cast<double>(it == ed.vars.end() ? 0.f : it->second)); out += buf; i = e + 1; continue; } }
    out += src[i++];
  }
  return out;
}

// Desenha todos os UiCanvas que pertencem a `camEnt` dentro do retangulo [org, org+size]. Botoes so reagem durante o Play.
inline void drawUiOverlay(Editor& ed, tool::Entity camEnt, ImVec2 org, ImVec2 size, bool interactive) {
  tool::World& w = ed.scene.world(); ImDrawList* dl = ImGui::GetWindowDrawList();
  const auto* cn = w.get<Name>(camEnt);
  dl->PushClipRect(org, {org.x + size.x, org.y + size.y}, true);
  w.each<UiCanvas>([&](tool::Entity ce, UiCanvas& cv) {
    if (!cv.visible) return;
    if (cv.camera[0]) { if (!cn || std::string(cv.camera) != cn->text) return; }
    else if (!ed.isPrincipal(camEnt)) return;
    for (int i = 0; i < cv.count && i < kMaxUi; ++i) {
      UiElem& it = cv.items[i]; const ImVec2 a{org.x + it.x * size.x, org.y + it.y * size.y}, b{a.x + it.w * size.x, a.y + it.h * size.y};
      const ImU32 col = ImGui::ColorConvertFloat4ToU32({it.color[0], it.color[1], it.color[2], it.color[3]});
      const std::string txt = uiFormat(ed, it.text);
      auto label = [&](ImU32 c) { ImFont* f = ImGui::GetFont(); const float fs = ImGui::GetFontSize() * it.fontScale * std::max(0.5f, size.y / 600.f); const ImVec2 ts = f->CalcTextSizeA(fs, 1e9f, 0.f, txt.c_str());
        dl->AddText(f, fs, {a.x + std::max(2.f, (b.x - a.x - ts.x) * 0.5f), a.y + (b.y - a.y - ts.y) * 0.5f}, c, txt.c_str()); };
      switch (static_cast<UiKind>(it.kind)) {
        case UiKind::Text: { ImFont* f = ImGui::GetFont(); const float fs = ImGui::GetFontSize() * it.fontScale * std::max(0.5f, size.y / 600.f); dl->AddText(f, fs, a, col, txt.c_str()); break; }
        case UiKind::Panel: dl->AddRectFilled(a, b, col, 4.f); if (txt.size()) label(IM_COL32(255, 255, 255, 255)); break;
        case UiKind::Bar: { const auto vit = ed.vars.find(it.action); const float v = vit == ed.vars.end() ? 0.f : vit->second; const float f = std::clamp(v / it.maxValue, 0.f, 1.f);
          dl->AddRectFilled(a, b, IM_COL32(20, 20, 20, 180), 3.f); dl->AddRectFilled(a, {a.x + (b.x - a.x) * f, b.y}, col, 3.f); dl->AddRect(a, b, IM_COL32(255, 255, 255, 120), 3.f); break; }
        case UiKind::Button: {
          bool hov = false, down = false;
          if (interactive) { ImGui::SetCursorScreenPos(a); char id[48]; std::snprintf(id, sizeof id, "##ui%u_%d", ce.index, i);
            if (ImGui::InvisibleButton(id, {std::max(1.f, b.x - a.x), std::max(1.f, b.y - a.y)})) { if (it.action[0]) ed.uiEvents.push_back(it.action); }
            hov = ImGui::IsItemHovered(); down = ImGui::IsItemActive(); }
          const ImVec4 c4 = ImGui::ColorConvertU32ToFloat4(col); const float k = down ? 0.7f : hov ? 1.15f : 1.f;
          dl->AddRectFilled(a, b, ImGui::ColorConvertFloat4ToU32({std::min(1.f, c4.x * k), std::min(1.f, c4.y * k), std::min(1.f, c4.z * k), c4.w}), 6.f); dl->AddRect(a, b, IM_COL32(255, 255, 255, 160), 6.f);
          label(IM_COL32(0, 0, 0, 255)); break; }
        default: break;
      }
    }
  });
  dl->PopClipRect();
}

} // namespace editor
