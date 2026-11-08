// UI-CONTRATO: este arquivo usa Dear ImGui. Catalogo e regras em ui_contract.hpp (secao 2.13).
// Elementos: IsKeyPressed/IsKeyDown para movimento no modo Play (teclas do mapa de entrada), GetIO e
//   GetCurrentContext para saber se ha campo de texto ativo (WantTextInput).
// Dados: mapa de entrada (input_map.hpp), nao da UI.
// Troca SpriteUI: a leitura de teclas passa a vir da camada de entrada da SpriteUI, com a mesma regra de foco.
// editor_scripts.hpp - Ligacao dos scripts ToolObject (QuickJS) com o Editor. Roda so durante o Play.
//
// Ordem de cada quadro (tick): Narrative -> scripts (start/update, Transform copiado para o JS e de volta)
// -> fisica/Tools. Os pedidos dos scripts (fila ScriptHost::cmds) sao aplicados DEPOIS de cada chamada de script,
// no maximo 8 passadas por quadro (um evento pode disparar outro evento; passou disso, o resto e descartado).
// Um objeto desativado (Inactive) nao roda update; o start acontece na primeira vez que ele fica ativo.
#pragma once

#include "imgui.h"
#include "script_check.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>

namespace editor {

inline ScriptHost::Entry Editor::scriptEntryFor(tool::Entity e) {
  ScriptHost::Entry en;
  auto& w = scene.world();
  const ScriptComp* sc = w.get<ScriptComp>(e);
  const Name* n = w.get<Name>(e);
  en.entity = e.index; en.gen = e.generation;
  en.name = n ? std::string(n->text) : std::string("object");
  en.enabled = sc && sc->enabled;
  if (en.enabled && !readScript(project, std::string(sc->path), en.source)) {
    en.enabled = false;
    tool::logWarn("Script file missing for '" + en.name + "': " + std::string(sc->path));
  }
  return en;
}

inline void Editor::startScripts() {
  scriptDelayed.clear();
  std::vector<ScriptHost::Entry> list;
  scene.world().each<ScriptComp>([&](tool::Entity e, ScriptComp&) { list.push_back(scriptEntryFor(e)); });
  if (!list.empty() && !ScriptHost::compiledIn()) tool::logWarn("This build has no script runtime (TOOL_WITH_QUICKJS is off): scripts will not run.");
  // position("Nome"): le o Transform do objeto no momento da chamada (sem copia extra).
  scriptHost.lookup = [this](const std::string& n, float out[3]) {
    const tool::Entity t = findByName(n);
    if (!scene.world().alive(t)) return false;
    const Transform* tr = scene.world().get<Transform>(t);
    if (!tr) return false;
    std::memcpy(out, tr->pos, sizeof tr->pos);
    return true;
  };
  // Nome de objeto errado em um comando vira aviso TOOL-S005 (sem isso o comando some em silencio).
  scriptHost.exists = [this](const std::string& n) { return scene.world().alive(findByName(n)); };
  scriptHost.open(list);
  // Verificacao antes do Play: cada problema vai para o Console com o objeto e a linha.
  for (const ScriptHost::Entry& en : list) {
    if (!en.enabled) continue;
    for (const ScriptIssue& is : checkScriptSource(en.source, en.name)) {
      tool::debug().report(is.code.c_str(), is.sev,
                           "[" + en.name + "] line " + std::to_string(is.line) + (is.col > 0 ? ", col " + std::to_string(is.col) : std::string()) + ": " + is.msg,
                           "", is.line);
    }
  }
  if (!list.empty()) tool::logInfo("Scripts: " + std::to_string(list.size()) + " object(s) with a script loaded for Play.");
}

// Verifica TODOS os scripts da cena (botao "Check scripts" do Console e comando "scripts"). Nao mexe no jogo.
inline void Editor::checkAllScripts() {
  std::vector<ScriptHost::Entry> list;
  scene.world().each<ScriptComp>([&](tool::Entity e, ScriptComp&) { list.push_back(scriptEntryFor(e)); });
  if (list.empty()) { tool::logInfo("Scripts: no object in the scene has a script."); return; }
  size_t errors = 0, warnings = 0;
  for (const ScriptHost::Entry& en : list) {
    const auto issues = checkScriptSource(en.source, en.name);
    if (issues.empty()) { tool::logInfo("[" + en.name + "] script OK"); continue; }
    for (const ScriptIssue& is : issues) {
      (is.sev == tool::Severity::Error ? errors : warnings)++;
      tool::debug().report(is.code.c_str(), is.sev,
                           "[" + en.name + "] line " + std::to_string(is.line) + (is.col > 0 ? ", col " + std::to_string(is.col) : std::string()) + ": " + is.msg,
                           "", is.line);
    }
  }
  const std::string sum = std::to_string(list.size()) + " script(s): " + std::to_string(errors) + " error(s), " + std::to_string(warnings) + " warning(s)";
  if (errors > 0) tool::logError("Scripts check: " + sum); else tool::logInfo("Scripts check: " + sum);
}

// Entrada do quadro para os scripts: teclas mantidas, teclas pressionadas neste quadro e movimento do mouse.
// Mapa de nomes (usados em input.down / input.pressed) para teclas do ImGui.
inline void Editor::fillScriptInput() {
  struct KeyName { const char* name; ImGuiKey key; };
  static const KeyName kKeys[] = {
    {"W", ImGuiKey_W}, {"A", ImGuiKey_A}, {"S", ImGuiKey_S}, {"D", ImGuiKey_D}, {"Q", ImGuiKey_Q}, {"E", ImGuiKey_E},
    {"R", ImGuiKey_R}, {"F", ImGuiKey_F}, {"Space", ImGuiKey_Space}, {"Shift", ImGuiKey_LeftShift}, {"Ctrl", ImGuiKey_LeftCtrl},
    {"Alt", ImGuiKey_LeftAlt}, {"Up", ImGuiKey_UpArrow}, {"Down", ImGuiKey_DownArrow}, {"Left", ImGuiKey_LeftArrow},
    {"Right", ImGuiKey_RightArrow}, {"Enter", ImGuiKey_Enter}, {"Escape", ImGuiKey_Escape}, {"1", ImGuiKey_1}, {"2", ImGuiKey_2},
  };
  ScriptHost::InputState& in = scriptHost.input;
  in.held.clear(); in.pressed.clear(); in.mouseDx = in.mouseDy = 0.f;
  if (!ImGui::GetCurrentContext()) return; // sem interface (selftest, linha de comando): sem teclado
  const ImGuiIO& io = ImGui::GetIO();
  for (const KeyName& k : kKeys) {
    if (ImGui::IsKeyDown(k.key)) in.held.push_back(k.name);
    if (ImGui::IsKeyPressed(k.key, false)) in.pressed.push_back(k.name);
  }
  if (io.MouseDown[0]) in.held.push_back("Mouse0");
  if (io.MouseDown[1]) in.held.push_back("Mouse1");
  if (io.MouseClicked[0]) in.pressed.push_back("Mouse0");
  in.mouseDx = io.MouseDelta.x; in.mouseDy = io.MouseDelta.y;
}

// Copia o Transform do objeto para o JS, roda start (se preciso), evento ou update, e copia o Transform de volta.
inline void Editor::scriptCall(ScriptHost::Inst& in, int what, float dt, const std::string& ev) {
  auto& w = scene.world();
  tool::Entity e; e.index = in.entity; e.generation = in.gen;
  if (in.failed || !w.alive(e) || w.has<Inactive>(e)) return;
  Transform* t = w.get<Transform>(e);
  if (!t) return;
  scriptHost.pushTransform(in, t->pos, t->rot, t->scale);
  if (!in.started) scriptHost.callStart(in);
  if (what == 1) scriptHost.callEvent(in, ev);
  else if (what == 2) scriptHost.callUpdate(in, dt);
  if (in.failed) return;
  float p[3], r[3], s[3];
  if (scriptHost.pullTransform(in, p, r, s)) {
    std::memcpy(t->pos, p, sizeof p); std::memcpy(t->rot, r, sizeof r); std::memcpy(t->scale, s, sizeof s);
  }
}

inline void Editor::tickScripts(float dt) {
  if (!scriptHost.isOpen()) return;
  auto& w = scene.world();
  scriptHost.setTime(scriptHost.now_ + static_cast<double>(dt));
  fillScriptInput();
  for (size_t i = 0; i < scriptDelayed.size();) {   // destroy com atraso
    scriptDelayed[i].second -= dt;
    if (scriptDelayed[i].second > 0.f) { ++i; continue; }
    const tool::Entity t = findByName(scriptDelayed[i].first);
    scriptDelayed.erase(scriptDelayed.begin() + static_cast<std::ptrdiff_t>(i));
    if (w.alive(t)) destroy(t);
  }
  for (auto& up : scriptHost.instances()) scriptCall(*up, 2, dt, std::string());
  applyScriptCmds();
}

inline tool::Entity Editor::spawnCopy(const std::string& name, const std::string& newName, const float* pos) {
  auto& w = scene.world();
  const tool::Entity t = findByName(name);
  if (!w.alive(t)) return tool::Entity{};
  const ScriptComp* orig = w.get<ScriptComp>(t);
  const std::string origPath = orig ? std::string(orig->path) : std::string();
  const tool::Entity c = duplicate(t);
  if (!newName.empty()) if (Name* nm = w.get<Name>(c)) ser::copyStr(nm->text, sizeof nm->text, newName);
  if (pos) if (Transform* tr = w.get<Transform>(c)) std::memcpy(tr->pos, pos, sizeof tr->pos);
  // A copia usa o MESMO arquivo de script (sem criar arquivos novos durante o Play): o clone roda em contexto proprio.
  if (ScriptComp* sc = w.get<ScriptComp>(c); sc && !origPath.empty()) {
    if (std::string(sc->path) != origPath) { std::error_code ec; fs::remove(project.resolve(std::string(sc->path)), ec); }
    ser::copyStr(sc->path, sizeof sc->path, origPath);
  }
  onCreated(c, name);
  if (scriptHost.isOpen() && w.get<ScriptComp>(c)) {
    ScriptHost::Entry en = scriptEntryFor(c);
    scriptHost.add(en);
  }
  return c;
}

// Pedidos de componente: so os tipos seguros de trocar durante o Play (corpo de fisica e luz exigem novo Play).
inline void Editor::applyScriptCmds() {
  using K = ScriptHost::CmdKind;
  auto& w = scene.world();
  for (int pass = 0; pass < 8 && !scriptHost.cmds.empty(); ++pass) {
    std::vector<ScriptHost::Cmd> batch;
    batch.swap(scriptHost.cmds);
    for (const ScriptHost::Cmd& c : batch) {
      switch (c.kind) {
        case K::Send: sendEvent(c.a, c.b); break;
        case K::SetActive: setActiveByName(c.a, c.flag); break;
        case K::Instantiate: spawnCopy(c.a, c.b, c.flag ? c.v : nullptr); break;
        case K::Destroy:
          if (c.delay > 0.f) { scriptDelayed.push_back({c.a, c.delay}); break; }
          { const tool::Entity t = findByName(c.a); if (w.alive(t)) destroy(t); }
          break;
        case K::AddComp:
        case K::RemoveComp: {
          const tool::Entity t = findByName(c.a);
          if (!w.alive(t)) break;
          const bool add = c.kind == K::AddComp;
          if (c.b == "MeshRenderer") {
            if (add) { if (!w.has<MeshRenderer>(t)) w.add<MeshRenderer>(t, MeshRenderer{}); }
            else if (w.has<MeshRenderer>(t)) w.remove<MeshRenderer>(t);
          } else if (c.b == "AudioSource") {
            if (add) { if (!w.has<AudioSource>(t)) w.add<AudioSource>(t, AudioSource{}); }
            else if (w.has<AudioSource>(t)) w.remove<AudioSource>(t);
          } else if (c.b == "ParticleComp") {
            if (add) { if (!w.has<ParticleComp>(t)) w.add<ParticleComp>(t, ParticleComp{}); }
            else if (w.has<ParticleComp>(t)) w.remove<ParticleComp>(t);
          } else {
            tool::logWarn("addComponent/removeComponent: '" + c.b + "' is not available from scripts (MeshRenderer, AudioSource, ParticleComp).");
          }
          break;
        }
        case K::SetPos: {
          const tool::Entity t = findByName(c.a);
          if (!w.alive(t)) break;
          if (Transform* tr = w.get<Transform>(t)) std::memcpy(tr->pos, c.v, sizeof tr->pos);
          break;
        }
        case K::SetRot: {
          const tool::Entity t = findByName(c.a);
          if (!w.alive(t)) break;
          if (Transform* tr = w.get<Transform>(t)) std::memcpy(tr->rot, c.v, sizeof tr->rot);
          break;
        }
        case K::SetColor: {
          const tool::Entity t = findByName(c.a);
          if (!w.alive(t)) break;
          if (MeshRenderer* mr = w.get<MeshRenderer>(t)) for (int i = 0; i < 3; ++i) mr->color[i] = std::clamp(c.v[i], 0.f, 1.f);
          break;
        }
      }
    }
  }
  if (!scriptHost.cmds.empty()) {
    tool::logWarn("Scripts: too many chained events in one frame; the rest was dropped.");
    scriptHost.cmds.clear();
  }
}

} // namespace editor
