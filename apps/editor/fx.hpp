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
// Emissores sem Play/edicao continuam simulando (como antes). Emissor que sumiu do mundo e descartado sem std::find.
namespace fxdetail {
struct EmitterJob {
  Editor::ParticleSim* sim = nullptr; const ParticleComp* pc = nullptr;
  tool::Vec3 org{}, up{}, u{}, v{}, g{};
  int spawn = 0;   // particulas novas neste quadro (ja limitadas)
};
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
    fxdetail::EmitterJob job; job.sim = &sim; job.pc = &pc; job.org = org; job.up = up; job.u = u; job.v = v; job.g = g0 * pc.gravityScale;
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
        Particle p; p.p = jb.org; p.v = dir * (pc.speed * (0.75f + 0.5f * fxdetail::rnd(sim.seed))); p.life = pc.lifetime * (0.8f + 0.4f * fxdetail::rnd(sim.seed));
        sim.list.push_back(p);
      }
      for (size_t i = 0; i < sim.list.size();) {
        Particle& p = sim.list[i]; p.age += dt;
        if (p.age >= p.life) { sim.list[i] = sim.list.back(); sim.list.pop_back(); continue; }
        p.v = p.v + jb.g * dt; p.p = p.p + p.v * dt;
        if (pc.bounce > 0.f && p.p.y < pc.floorY) { p.p.y = pc.floorY; p.v.y = -p.v.y * pc.bounce; p.v.x *= 0.9f; p.v.z *= 0.9f; }
        ++i;
      }
    }
  });
  // Limpeza: emissores que nao apareceram neste quadro (apagados) saem do mapa.
  for (auto it = particles.begin(); it != particles.end();) { if (it->second.touched != particleFrame) it = particles.erase(it); else ++it; }
}

// Lotes de particulas para a camera: quads voltados para ela, agrupados por cor (4 niveis por canal = ate 64 lotes).
// Cada lote vira UMA malha dinamica = UMA chamada de desenho (antes eram N chamadas, uma por particula).
// Particulas fora do frustum sao descartadas aqui (culling por esfera).
inline void Editor::buildParticleBatches(const tool::Camera& cam, int w, int h) {
  for (auto& m : partBatch) { m.pos.clear(); m.normal.clear(); m.uv.clear(); m.idx.clear(); }
  const tool::Vec3 fwd = tool::normalize(cam.target - cam.eye);
  const tool::Vec3 right = tool::normalize(tool::cross(fwd, cam.up)), up = tool::cross(right, fwd);
  const tool::Vec3 nrm = fwd * -1.f;   // quad olha para a camera
  const auto planes = framePlanes(cam, w, h);
  scene.world().each<ParticleComp>([&](tool::Entity e, ParticleComp& pc) {
    if (scene.world().has<Inactive>(e)) return;
    auto it = particles.find(e.index); if (it == particles.end()) return;
    for (const Particle& p : it->second.list) {
      const float t = std::clamp(p.age / std::max(p.life, 1e-4f), 0.f, 1.f);
      const float sz = pc.size + (pc.sizeEnd - pc.size) * t;
      if (sz <= 1e-3f) continue;
      bool out = false;
      for (const tool::Vec4& pl : planes) if (pl.x * p.p.x + pl.y * p.p.y + pl.z * p.p.z + pl.w < -sz) { out = true; break; }
      if (out) { ++prof.counters.culled; continue; }
      const float k = pc.alpha * (1.f - t * t);   // "some" aos poucos: escurece e encolhe
      const float col[3] = {(pc.color[0] + (pc.colorEnd[0] - pc.color[0]) * t) * k, (pc.color[1] + (pc.colorEnd[1] - pc.color[1]) * t) * k, (pc.color[2] + (pc.colorEnd[2] - pc.color[2]) * t) * k};
      auto lvl = [](float c) { return std::clamp(static_cast<int>(std::lround(std::clamp(c, 0.f, 1.f) * 3.f)), 0, 3); };
      const int bucket = lvl(col[0]) * 16 + lvl(col[1]) * 4 + lvl(col[2]);
      tool::Mesh& m = partBatch[static_cast<size_t>(bucket)];
      partColor[static_cast<size_t>(bucket)] = {lvl(col[0]) / 3.f, lvl(col[1]) / 3.f, lvl(col[2]) / 3.f};
      const float hs = sz * 0.5f;
      const tool::Vec3 r = right * hs, u = up * hs;
      const uint32_t base = static_cast<uint32_t>(m.pos.size());
      m.pos.push_back(p.p - r - u); m.pos.push_back(p.p + r - u); m.pos.push_back(p.p + r + u); m.pos.push_back(p.p - r + u);
      for (int c = 0; c < 4; ++c) m.normal.push_back(nrm);
      m.uv.insert(m.uv.end(), {0.f, 1.f, 1.f, 1.f, 1.f, 0.f, 0.f, 0.f});
      m.idx.insert(m.idx.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
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
