// fx_particles.hpp - Particulas v2 no editor: componente ParticleFxComp (biblioteca engine/particles), arquivos de efeito
// em <projeto>/Effects/*.json, presets (fumaca, fogo, fluido), simulacao por quadro e entrega dos quads ao GpuRenderer.
// E o unico sistema de particulas do editor (o ParticleComp antigo foi removido; cenas antigas sao convertidas ao abrir).
#pragma once
#include "editor_state.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>

namespace editor {

namespace fxlib {

// Arquivo de efeito: relativo ao projeto, dentro dele e terminando em .json (entrada nao confiavel: vem do Inspector/cena).
inline std::filesystem::path effectPath(const Editor& ed, const std::string& rel) {
  if (rel.size() < 6 || rel.compare(rel.size() - 5, 5, ".json") != 0) return {};
  return projpath::resolveInside(ed.project.root, rel);
}

// Grava Effects/<nome>.json dos presets que ainda nao existem (nunca sobrescreve o que o usuario editou).
inline void ensurePresetFiles(const Editor& ed) {
  std::error_code ec;
  const std::filesystem::path dir = ed.project.root / "Effects";
  std::filesystem::create_directories(dir, ec);
  for (const char* name : tool::particles::kPresetNames) {
    const std::filesystem::path p = dir / (std::string(name) + ".json");
    if (std::filesystem::exists(p, ec)) continue;
    if (const char* txt = tool::particles::presetJson(name)) { std::ofstream f(p, std::ios::binary); f << txt; }
  }
}

// Carrega um efeito de Effects/<arquivo>.json no componente. Efeito invalido (JSON ruim, modulo/formato desconhecido,
// valor fora da faixa) e REJEITADO com aviso: o componente fica como estava e devolve false.
inline bool loadFile(Editor& ed, ParticleFxComp& pc, const std::string& rel) {
  const std::filesystem::path p = effectPath(ed, rel);
  if (p.empty()) { tool::logWarn("Particulas: caminho de efeito invalido: '" + rel + "' (use Effects/<nome>.json dentro do projeto)."); return false; }
  std::ifstream f(p, std::ios::binary);
  if (!f) { tool::logWarn("Particulas: nao achei o efeito '" + rel + "'."); return false; }
  std::stringstream ss; ss << f.rdbuf();
  auto fx = tool::particles::effectFromText(ss.str());
  if (!fx) { tool::logWarn("Particulas: efeito '" + rel + "' invalido (JSON, formato, modulo ou valor desconhecido); mantido o efeito atual."); return false; }
  pc.fx = std::move(*fx);
  pc.ui.synced = false;   // o Inspector relê cores e taxa da turbulencia
  ser::copyStr(pc.file, sizeof pc.file, rel);
  return true;
}

// Salva o efeito do componente em Effects/<arquivo>.json (cria a pasta). false se o caminho for invalido ou a escrita falhar.
inline bool saveFile(Editor& ed, const ParticleFxComp& pc, const std::string& rel) {
  const std::filesystem::path p = effectPath(ed, rel);
  if (p.empty()) { tool::logWarn("Particulas: caminho de efeito invalido: '" + rel + "'."); return false; }
  std::error_code ec;
  std::filesystem::create_directories(p.parent_path(), ec);
  std::ofstream f(p, std::ios::binary);
  if (!f) { tool::logWarn("Particulas: nao foi possivel gravar '" + rel + "'."); return false; }
  f << tool::particles::effectToJson(pc.fx).dump();
  return static_cast<bool>(f);
}

// Aplica um preset embutido (0 fumaca, 1 fogo, 2 fluido) e garante o arquivo correspondente em Effects/.
inline bool applyPreset(Editor& ed, ParticleFxComp& pc, int index) {
  if (index < 0 || index >= tool::particles::kPresetCount) return false;
  const char* name = tool::particles::kPresetNames[index];
  auto fx = tool::particles::effectFromText(tool::particles::presetJson(name));
  if (!fx) return false;   // nao acontece: os presets sao testados
  pc.fx = std::move(*fx);
  pc.ui.synced = false;
  ser::copyStr(pc.file, sizeof pc.file, std::string("Effects/") + name + ".json");
  ensurePresetFiles(ed);
  return true;
}

// Cria o objeto "Particle" (menu + Create da Hierarchy): objeto comum com Transform e ParticleFxComp, ja com o preset
// (0 fumaca, 1 fogo, 2 fluido). Devolve a entidade.
inline tool::Entity makeParticleObject(Editor& ed, int preset) {
  const tool::Entity e = ed.make("Particle");
  ParticleFxComp& pf = ed.scene.world().add<ParticleFxComp>(e);
  applyPreset(ed, pf, preset);
  return e;
}

} // namespace fxlib

// Avanca todos os sistemas de particulas v2 da cena. Cria o sistema na primeira vez (ou quando a capacidade muda), aplica o
// efeito atual do componente (edicao ao vivo no Inspector) e processa Play/Pause/Stop/Burst.
inline void Editor::updateFx(float dt) {
  tool::World& w = scene.world();
  dt = std::clamp(dt, 0.f, 0.1f);
  ++fxFrame;
  bool animating = false;
  w.each<ParticleFxComp>([&](tool::Entity e, ParticleFxComp& pc) {
    FxRuntime& rt = fxRt[e.index];
    rt.touched = fxFrame;
    if (!pc.ui.synced) fxPull(pc); else fxPush(pc);   // campos do Inspector <-> efeito
    const tool::u32 cap = static_cast<tool::u32>(std::clamp(pc.maxParticles, 1, 1000000));
    if (!rt.sys || rt.cap != cap) { rt.sys = std::make_unique<tool::particles::ParticleSystem>(pc.fx, cap); rt.cap = cap; }
    // Comandos pendentes (Inspector, scripts, testes).
    switch (pc.cmd) {
      case 1: pc.playing = true; break;
      case 2: pc.playing = false; break;
      case 3: pc.playing = false; rt.sys->reset(); break;   // Stop: para e limpa tudo
      case 4: rt.sys->burst(std::min<tool::u32>(cap, 500u)); break;
      default: break;
    }
    pc.cmd = 0;
    if (w.has<Inactive>(e)) return;
    // Origem = posicao do objeto no mundo; o eixo do cone/velocidade acompanha a rotacao do objeto.
    const tool::Mat4 wm = worldMat(e);
    tool::particles::Effect eff = pc.fx;
    const tool::Vec4 d4 = wm.mul({eff.init.dir[0], eff.init.dir[1], eff.init.dir[2], 0.f});
    const tool::Vec3 dn = tool::normalize({d4.x, d4.y, d4.z});
    if (tool::length(dn) > 0.5f) { eff.init.dir[0] = dn.x; eff.init.dir[1] = dn.y; eff.init.dir[2] = dn.z; }
    // Trajetoria e redemoinho sao locais ao objeto: pontos e eixo giram (e escalam) junto com ele; o piso e fixo no mundo.
    for (tool::particles::Module& m : eff.modules) {
      if (m.kind == tool::particles::ModuleKind::Route) {
        for (tool::u32 k = 0; k < m.routeCount && k < tool::particles::kMaxRoutePoints; ++k) {
          const tool::Vec4 q = wm.mul({m.pts[k][0], m.pts[k][1], m.pts[k][2], 0.f});
          m.pts[k][0] = q.x; m.pts[k][1] = q.y; m.pts[k][2] = q.z;
        }
      } else if (m.kind == tool::particles::ModuleKind::Swirl) {
        const tool::Vec4 q = wm.mul({m.axis[0], m.axis[1], m.axis[2], 0.f});
        const tool::Vec3 an = tool::normalize({q.x, q.y, q.z});
        if (tool::length(an) > 0.5f) { m.axis[0] = an.x; m.axis[1] = an.y; m.axis[2] = an.z; }
      }
    }
    rt.sys->setEffect(eff);
    rt.sys->setOrigin(wm.m[3][0], wm.m[3][1], wm.m[3][2]);
    if (pc.playing) { rt.sys->update(dt); animating = true; }
  });
  if (animating) fxAnimating = true;   // as vistas nao podem reaproveitar a imagem antiga enquanto ha particulas andando
  for (auto it = fxRt.begin(); it != fxRt.end();) { if (it->second.touched != fxFrame) it = fxRt.erase(it); else ++it; }
}

// Entrega ao GpuRenderer os quads das particulas v2 desta vista (billboards virados para `cam`; 2D no plano do objeto).
// Alfa: ordena de tras para frente (aproximado, em baldes); aditivo e bloom nao precisam de ordem.
inline void Editor::submitFx(const tool::Camera& cam) {
  fxQuadsLast = 0;
  if (!fxRender || fxRt.empty()) return;
  tool::World& w = scene.world();
  const tool::Vec3 fwd = tool::normalize(cam.target - cam.eye);
  const tool::Vec3 right = tool::normalize(tool::cross(fwd, cam.up));
  const tool::Vec3 upv = tool::cross(right, fwd);
  const tool::f32 R[3] = {right.x, right.y, right.z}, U[3] = {upv.x, upv.y, upv.z}, F[3] = {fwd.x, fwd.y, fwd.z};
  const tool::f32 eye[3] = {cam.eye.x, cam.eye.y, cam.eye.z};
  w.each<ParticleFxComp>([&](tool::Entity e, ParticleFxComp& pc) {
    if (w.has<Inactive>(e) || hiddenInView(e)) return;
    auto it = fxRt.find(e.index);
    if (it == fxRt.end() || !it->second.sys || it->second.sys->alive() == 0) return;
    FxRuntime& rt = it->second;
    const tool::particles::QuadView view = pc.mode2D ? tool::particles::QuadView::plane2D(worldMat(e).m[3][2])
                                                     : tool::particles::QuadView::billboard(R, U, F);
    tool::particles::buildQuads(*rt.sys, view, rt.batch);
    const std::vector<tool::particles::QuadInstance>* list = &rt.batch.quads;
    if (rt.batch.shader == tool::particles::ShaderKind::Alpha && !pc.mode2D) {
      tool::particles::sortQuadsBackToFront(rt.batch.quads, eye, F, rt.sorted, rt.scratch);
      list = &rt.sorted;
    }
    if (list->empty()) return;
    static_assert(sizeof(tool::particles::QuadInstance) == sizeof(tool::ParticleQuadGPU), "layout do quad: biblioteca x renderer");
    tool::ParticleDraw d;
    d.quads = reinterpret_cast<const tool::ParticleQuadGPU*>(list->data());
    d.count = static_cast<tool::u32>(list->size());
    d.shader = rt.batch.shader == tool::particles::ShaderKind::Alpha ? 0u : rt.batch.shader == tool::particles::ShaderKind::Additive ? 1u : 2u;
    d.emissive = rt.batch.emissive; d.soft = rt.batch.soft; d.is2D = pc.mode2D;
    gpu.drawParticles(d);
    fxQuadsLast += d.count;
  });
}

} // namespace editor
