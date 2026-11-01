// editor_scripts.hpp - Ligacao dos scripts ToolObject (QuickJS) com o Editor. Roda so durante o Play.
//
// Ordem de cada quadro (tick): Narrative -> scripts (start/update, Transform copiado para o JS e de volta)
// -> fisica/Tools. Os pedidos dos scripts (fila ScriptHost::cmds) sao aplicados DEPOIS de cada chamada de script,
// no maximo 8 passadas por quadro (um evento pode disparar outro evento; passou disso, o resto e descartado).
// Um objeto desativado (Inactive) nao roda update; o start acontece na primeira vez que ele fica ativo.
#pragma once

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
  scriptHost.open(list);
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

inline tool::Entity Editor::spawnCopy(const std::string& name) {
  auto& w = scene.world();
  const tool::Entity t = findByName(name);
  if (!w.alive(t)) return tool::Entity{};
  const ScriptComp* orig = w.get<ScriptComp>(t);
  const std::string origPath = orig ? std::string(orig->path) : std::string();
  const tool::Entity c = duplicate(t);
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
        case K::Instantiate: spawnCopy(c.a); break;
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
      }
    }
  }
  if (!scriptHost.cmds.empty()) {
    tool::logWarn("Scripts: too many chained events in one frame; the rest was dropped.");
    scriptHost.cmds.clear();
  }
}

} // namespace editor
