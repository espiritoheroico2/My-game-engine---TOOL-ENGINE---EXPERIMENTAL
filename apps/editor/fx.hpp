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

// Particulas: simulacao em duas fases.
//  1) SERIAL: le o mundo (worldMat nao e thread-safe) e calcula, por emissor, base de emissao, gravidade e quantas
//     particulas pode criar neste quadro (respeitando o limite do emissor e o ORCAMENTO global de particulas).
//  2) PARALELA: cada emissor cria e integra as proprias particulas (lista e semente proprias: nao ha dado compartilhado).
// Desenho: drawParticleOverlay, sobre a vista (cor verdadeira, alfa real, sem iluminacao).
namespace fxdetail {
struct EmitterJob {
  Editor::ParticleSim* sim = nullptr; const ParticleComp* pc = nullptr;
  tool::Vec3 org{}, up{}, u{}, v{}, g{};
  tool::Mat4 wm{};   // matriz do emissor (a trajetoria e em coordenadas locais)
  int spawn = 0;     // particulas novas neste quadro (ja limitadas)
};

// Deslocamento do nascimento dentro da area (local: x = u, y = up, z = v).
inline tool::Vec3 areaOffset(const ParticleComp& pc, uint32_t& s) {
  if (pc.area == 1) { const float x = rnd(s) * 2.f - 1.f, y = rnd(s) * 2.f - 1.f, z = rnd(s) * 2.f - 1.f;
    return {x * pc.areaSize[0] * 0.5f, y * pc.areaSize[1] * 0.5f, z * pc.areaSize[2] * 0.5f}; }
  if (pc.area == 2) { const float z = rnd(s) * 2.f - 1.f, a = rnd(s) * 6.2831853f, rr = std::cbrt(rnd(s)) * pc.areaSize[0] * 0.5f;
    const float q = std::sqrt(std::max(0.f, 1.f - z * z)); return {q * std::cos(a) * rr, z * rr, q * std::sin(a) * rr}; }
  return {};
}

// Trajetoria: curva Catmull-Rom que passa por todos os pontos. t em 0..1 percorre a curva inteira.
inline tool::Vec3 routeSample(const ParticleComp& pc, float t) {
  const int n = std::clamp(pc.routeCount, 0, 8);
  if (n == 0) return {};
  auto P = [&](int i) { i = std::clamp(i, 0, n - 1); return tool::Vec3{pc.routePts[i][0], pc.routePts[i][1], pc.routePts[i][2]}; };
  if (n == 1) return P(0);
  const float f = std::clamp(t, 0.f, 1.f) * static_cast<float>(n - 1);
  const int i = std::min(static_cast<int>(f), n - 2);
  const float s = f - static_cast<float>(i);
  const tool::Vec3 p0 = P(i - 1), p1 = P(i), p2 = P(i + 1), p3 = P(i + 2);
  auto cr = [s](float a, float b, float c, float d) { return 0.5f * ((2.f * b) + (-a + c) * s + (2.f * a - 5.f * b + 4.f * c - d) * s * s + (-a + 3.f * b - 3.f * c + d) * s * s * s); };
  return {cr(p0.x, p1.x, p2.x, p3.x), cr(p0.y, p1.y, p2.y, p3.y), cr(p0.z, p1.z, p2.z, p3.z)};
}

// Cor em 4 paradas (inicio, 1/3, 2/3, fim): interpolacao linear entre elas.
inline tool::Vec3 particleColor(const ParticleComp& pc, float t) {
  const float* st[4] = {pc.color, pc.color2, pc.color3, pc.colorEnd};
  const float f = std::clamp(t, 0.f, 1.f) * 3.f;
  const int i = std::min(static_cast<int>(f), 2);
  const float k = f - static_cast<float>(i);
  return {st[i][0] + (st[i + 1][0] - st[i][0]) * k, st[i][1] + (st[i + 1][1] - st[i][1]) * k, st[i][2] + (st[i + 1][2] - st[i][2]) * k};
}

// Opacidade: fica no nivel 'alpha' ate o atraso acabar e cai ate zero no fim da vida (se fadeOpacity estiver ligado).
inline float particleAlpha(const ParticleComp& pc, const Editor::Particle& p) {
  float a = pc.alpha;
  if (pc.fadeOpacity) {
    const float life = std::max(p.life, 1e-3f), d = std::clamp(pc.fadeDelay, 0.f, life * 0.999f);
    a *= 1.f - std::clamp((p.age - d) / (life - d), 0.f, 1.f);
  }
  return std::clamp(a, 0.f, 1.f);
}
}

inline void Editor::updateParticles(float dt) {
  tool::World& w = scene.world();
  dt = std::clamp(dt, 0.f, 0.1f);
  ++particleFrame;
  std::vector<fxdetail::EmitterJob> jobs; jobs.reserve(64);
  size_t alive = 0;
  for (auto& [k, sim] : particles) alive += sim.list.size();   // ocupacao atual (antes de criar novas)
  // Ha emissor vivo na cena? Enquanto houver, as vistas NAO podem reaproveitar a imagem antiga (as particulas andam).
  fxAnimating = false;
  w.each<ParticleComp>([&](tool::Entity, ParticleComp&) { fxAnimating = true; });
  long long budgetLeft = std::max<long long>(0, static_cast<long long>(particleBudget) - static_cast<long long>(alive));
  const tool::Vec3 g0{sceneInfo.physics.gravity[0], sceneInfo.physics.gravity[1], sceneInfo.physics.gravity[2]};
  w.each<ParticleComp>([&](tool::Entity e, ParticleComp& pc) {
    ParticleSim& sim = particles[e.index]; sim.touched = particleFrame;
    if (w.has<Inactive>(e)) return;
    const tool::Mat4 wm = worldMat(e); const tool::Vec3 org{wm.m[3][0], wm.m[3][1], wm.m[3][2]};
    const tool::Vec4 up4 = wm.mul({0.f, 1.f, 0.f, 0.f}); tool::Vec3 up{up4.x, up4.y, up4.z}; const float ul = tool::length(up); up = ul > 1e-5f ? up * (1.f / ul) : tool::Vec3{0.f, 1.f, 0.f};
    const tool::Vec3 ref = std::fabs(up.y) < 0.99f ? tool::Vec3{0.f, 1.f, 0.f} : tool::Vec3{1.f, 0.f, 0.f};
    tool::Vec3 u = tool::cross(up, ref); u = u * (1.f / std::max(1e-5f, tool::length(u))); const tool::Vec3 v = tool::cross(up, u);
    fxdetail::EmitterJob job; job.sim = &sim; job.pc = &pc; job.org = org; job.up = up; job.u = u; job.v = v; job.g = g0 * pc.gravityScale; job.wm = wm;
    const long long cap = std::clamp(pc.maxParticles, 1, 5000);
    if (pc.emitting) {
      sim.acc += pc.rate * dt;
      const long long want = static_cast<long long>(sim.acc);
      const long long room = std::min<long long>(cap - static_cast<long long>(sim.list.size()), budgetLeft);
      const long long n = std::clamp<long long>(std::min(want, room), 0, want);
      sim.acc = n < want ? 0.f : sim.acc - static_cast<float>(n);   // bateu no limite: nao acumula atraso
      budgetLeft -= n; job.spawn = static_cast<int>(n);
    }
    jobs.push_back(job);
  });
  // Fase 2: paralela. Cada job so mexe na propria lista e na propria semente.
  tool::parallelFor(jobs.size(), 4, [&](size_t j0, size_t j1) {
    for (size_t j = j0; j < j1; ++j) {
      const fxdetail::EmitterJob& jb = jobs[j]; ParticleSim& sim = *jb.sim; const ParticleComp& pc = *jb.pc;
      for (int k = 0; k < jb.spawn; ++k) {
        const float ang = fxdetail::rnd(sim.seed) * 6.2831853f, cone = std::sin(pc.spread * 0.0174533f * std::sqrt(fxdetail::rnd(sim.seed)));
        const tool::Vec3 dir = jb.up * std::sqrt(std::max(0.f, 1.f - cone * cone)) + jb.u * (std::cos(ang) * cone) + jb.v * (std::sin(ang) * cone);
        Particle p; p.v = dir * (pc.speed * (0.75f + 0.5f * fxdetail::rnd(sim.seed))); p.life = pc.lifetime * (0.8f + 0.4f * fxdetail::rnd(sim.seed));
        p.phase = fxdetail::rnd(sim.seed) * 6.2831853f; p.jit = fxdetail::areaOffset(pc, sim.seed);
        p.p = jb.org + jb.u * p.jit.x + jb.up * p.jit.y + jb.v * p.jit.z;
        sim.list.push_back(p);
      }
      const bool path = pc.route && pc.routeCount >= 1, spiral = !path && pc.tornado;
      for (size_t i = 0; i < sim.list.size();) {
        Particle& p = sim.list[i]; p.age += dt;
        if (p.age >= p.life) { sim.list[i] = sim.list.back(); sim.list.pop_back(); continue; }
        const float t = std::clamp(p.age / std::max(p.life, 1e-4f), 0.f, 1.f);
        if (path) { // trajetoria: a particula percorre a curva (pontos locais do objeto) durante a vida
          const tool::Vec3 l = fxdetail::routeSample(pc, t) + p.jit;
          const tool::Vec4 wp = jb.wm.mul({l.x, l.y, l.z, 1.f}); p.p = {wp.x, wp.y, wp.z};
        } else if (spiral) { // tornado: gira em torno do eixo do objeto e sobe abrindo em funil
          const float th = p.phase + 6.2831853f * pc.tornadoSpeed * p.age, rr = pc.tornadoRadius * (0.3f + 0.7f * t);
          const tool::Vec3 base = jb.org + jb.u * p.jit.x + jb.up * p.jit.y + jb.v * p.jit.z + jb.up * (pc.tornadoHeight * t);
          p.p = base + jb.u * (std::cos(th) * rr) + jb.v * (std::sin(th) * rr);
        } else {
          p.v = p.v + jb.g * dt; p.p = p.p + p.v * dt;
          if (pc.bounce > 0.f && p.p.y < pc.floorY) { p.p.y = pc.floorY; p.v.y = -p.v.y * pc.bounce; p.v.x *= 0.9f; p.v.z *= 0.9f; }
        }
        ++i;
      }
    }
  });
  // Limpeza: emissores que nao apareceram neste quadro (apagados) saem do mapa.
  for (auto it = particles.begin(); it != particles.end();) { if (it->second.touched != particleFrame) it = particles.erase(it); else ++it; }
}

// Particulas desenhadas sobre a vista: cor e alfa reais (sem iluminacao e sem escurecer). `selected` mostra a
// trajetoria do objeto selecionado na Scene. Nao ha teste de profundidade: ficam por cima dos objetos.
inline void drawParticleOverlay(Editor& ed, const tool::Camera& cam, ImVec2 org, ImVec2 size, tool::Entity selected) {
  if (size.x < 2.f || size.y < 2.f) return;
  tool::World& w = ed.scene.world(); ImDrawList* dl = ImGui::GetWindowDrawList();
  const tool::Mat4 vp = cam.projection(size.x, size.y) * tool::Mat4::lookAt(cam.eye, cam.target, cam.up);
  const tool::Vec3 fwd = tool::normalize(cam.target - cam.eye), right = tool::normalize(tool::cross(fwd, cam.up));
  auto toScr = [&](tool::Vec3 p, ImVec2& s) {
    const tool::Vec4 c = vp.mul({p.x, p.y, p.z, 1.f});
    if (!cam.ortho && c.w < cam.nearZ) return false;
    s = {org.x + (c.x / c.w * 0.5f + 0.5f) * size.x, org.y + (1.f - (c.y / c.w * 0.5f + 0.5f)) * size.y};
    return true;
  };
  dl->PushClipRect(org, {org.x + size.x, org.y + size.y}, true);
  w.each<ParticleComp>([&](tool::Entity e, ParticleComp& pc) {
    if (w.has<Inactive>(e)) return;
    if (e == selected && pc.route && pc.routeCount >= 2) { // trajetoria: linha amarela + pontos de controle
      const tool::Mat4 wm = ed.worldMat(e); ImVec2 prev{}; bool hasPrev = false;
      for (int k = 0; k <= 64; ++k) {
        const tool::Vec3 l = fxdetail::routeSample(pc, static_cast<float>(k) / 64.f); const tool::Vec4 wp = wm.mul({l.x, l.y, l.z, 1.f});
        ImVec2 s; if (!toScr({wp.x, wp.y, wp.z}, s)) { hasPrev = false; continue; }
        if (hasPrev) dl->AddLine(prev, s, IM_COL32(255, 220, 80, 230), 2.f);
        prev = s; hasPrev = true;
      }
      for (int i = 0; i < pc.routeCount; ++i) {
        const tool::Vec4 wp = wm.mul({pc.routePts[i][0], pc.routePts[i][1], pc.routePts[i][2], 1.f}); ImVec2 s;
        if (toScr({wp.x, wp.y, wp.z}, s)) { dl->AddCircleFilled(s, 4.f, IM_COL32(255, 220, 80, 255)); dl->AddCircle(s, 6.f, IM_COL32(0, 0, 0, 200), 0, 1.f); }
      }
    }
    auto it = ed.particles.find(e.index); if (it == ed.particles.end()) return;
    for (const Editor::Particle& p : it->second.list) {
      const float t = std::clamp(p.age / std::max(p.life, 1e-4f), 0.f, 1.f);
      const float sz = pc.size + (pc.sizeEnd - pc.size) * t;
      if (sz <= 1e-3f) continue;
      const float a = fxdetail::particleAlpha(pc, p);
      if (a <= 0.003f) continue;
      ImVec2 s; if (!toScr(p.p, s)) continue;
      ImVec2 sr; if (!toScr(p.p + right * (sz * 0.5f), sr)) continue;
      const float r = std::max(1.f, std::hypot(sr.x - s.x, sr.y - s.y));
      if (s.x + r < org.x || s.x - r > org.x + size.x || s.y + r < org.y || s.y - r > org.y + size.y) { ++ed.prof.counters.culled; continue; }
      const tool::Vec3 c = fxdetail::particleColor(pc, t);
      const ImU32 col = ImGui::GetColorU32({c.x, c.y, c.z, a});
      switch (pc.form) {
        case 1: dl->AddRectFilled({s.x - r, s.y - r}, {s.x + r, s.y + r}, col); break;
        case 2: dl->AddTriangleFilled({s.x, s.y - r * 1.2f}, {s.x - r, s.y + r * 0.8f}, {s.x + r, s.y + r * 0.8f}, col); break;
        case 3: { ImVec2 pts[10]; for (int k = 0; k < 10; ++k) { const float ang = -1.5708f + k * 0.6283185f, rr = (k % 2) ? r * 0.45f : r * 1.2f; pts[k] = {s.x + std::cos(ang) * rr, s.y + std::sin(ang) * rr}; }
          dl->AddConcavePolyFilled(pts, 10, col); break; }
        case 4: dl->AddCircle(s, r, col, 16, std::max(1.f, r * 0.35f)); break;
        case 5: { ImVec2 q = s; const tool::Vec3 back = p.p - p.v * 0.04f; ImVec2 qb; if (toScr(back, qb)) q = qb;
          dl->AddLine(q, s, col, std::max(1.f, r * 0.8f)); break; }
        default: { // bolinha suave: halo + nucleo
          dl->AddCircleFilled(s, r * 1.6f, ImGui::GetColorU32({c.x, c.y, c.z, a * 0.25f}), 16);
          dl->AddCircleFilled(s, r, col, 16); break; }
      }
    }
  });
  dl->PopClipRect();
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
