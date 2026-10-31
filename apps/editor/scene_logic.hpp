// scene_logic.hpp - Codigo da cena + execucao do Narrative Controller durante o Play.
//   Toda cena tem: um grafo (Assets/Narrative/<cena>.narr.json) e um codigo (.js) que o chama. O Play compila o grafo e o executa
//   com os gatilhos Start / Update / Create(objeto) e os blocos SavePoint/LoadPoint; botoes da UI disparam eventos com o mesmo nome.
#pragma once
#include "editor_state.hpp"
#include "script.hpp"

#include <cstring>

namespace editor {

inline std::string sceneGraphRel(const std::string& scene) { return "Assets/Narrative/" + scene + ".narr.json"; }
inline std::string sceneScriptRel(const std::string& scene) { return "Assets/Scripts/Scenes/" + scene + ".scene.tool.js"; }

// Garante grafo + codigo da cena (cria se faltar). Retorna true se algo foi criado.
inline bool Editor::ensureSceneCode() {
  if (!safeName(sceneInfo.name)) return false;
  bool made = false;
  if (sceneInfo.narrative.empty()) sceneInfo.narrative = sceneGraphRel(sceneInfo.name);
  if (sceneInfo.script.empty()) sceneInfo.script = sceneScriptRel(sceneInfo.name);
  const fs::path g = project.resolve(sceneInfo.narrative);
  if (!g.empty() && !fs::exists(g)) {
    static const char* kDefault = R"({"version":1,"variables":{"score":{"type":"number","value":0}},"nodes":[
 {"id":"start1","type":"start","x":40,"y":40},
 {"id":"log1","type":"action","name":"log","arg":"Scene started","x":300,"y":40},
 {"id":"click1","type":"trigger","event":"add_score","mode":"reactivable","x":40,"y":200},
 {"id":"save1","type":"savepoint","slot":"slot1","x":300,"y":200}],
 "links":[{"from":"start1","to":"log1"},{"from":"click1","to":"save1"}]})";
    made |= writeTextFile(g, kDefault);
  }
  const fs::path sp = project.resolve(sceneInfo.script);
  if (!sp.empty() && !fs::exists(sp) && scriptPathOk(project, sceneInfo.script)) {
    made |= writeScript(project, sceneInfo.script,
      "// Scene code for \"" + sceneInfo.name + "\" - attached to the scene automatically.\n"
      "// When you press Play, the engine runs this scene's Narrative Controller graph:\n//   " + sceneInfo.narrative + "\n"
      "// Triggers: Start (once), Update (every frame), Create (object name). Blocks: SavePoint / LoadPoint.\n"
      "// Game UI buttons fire a Narrative event with the same name as the button's \"Event name\".\n"
      "export function start(scene) {\n  scene.runNarrative(\"" + sceneInfo.narrative + "\");\n}\n\n"
      "export function update(scene, dt) {\n  // per-frame code for this scene (the script runtime is planned; the graph already runs natively)\n}\n");
  }
  return made;
}

inline void Editor::openSceneNarrative() {
  if (sceneInfo.narrative.empty()) return;
  const fs::path g = project.resolve(sceneInfo.narrative);
  if (!g.empty() && narrative.path() != g.string()) narrative.open(g.string());
}

inline tool::Entity Editor::findByName(const std::string& n) {
  tool::Entity r{}; auto& w = scene.world();
  w.each<Name>([&](tool::Entity e, Name& nm) { if (!w.alive(r) && n == nm.text) r = e; });
  return r;
}

inline void Editor::startNarrative() {
  narrRun.reset();
  if (sceneInfo.narrative.empty()) return;
  const fs::path g = project.resolve(sceneInfo.narrative);
  if (g.empty()) return;
  tool::Json graph;
  if (narrative.path() == g.string()) graph = narrative.graph();   // o que esta na tela (inclusive sem salvar)
  else { std::string text; if (!readTextFile(g, text)) return; auto j = tool::Json::parse(text); if (!j) return; graph = *j; }
  auto run = std::make_unique<NarrRun>(); std::string err;
  if (!tool::narr::Compiler().compile(graph, run->prog, err)) { statusLine = "Narrative: " + err; tool::logError(statusLine); return; }
  run->rt = std::make_unique<tool::narr::Runtime>(run->prog);
  tool::narr::Runtime* rt = run->rt.get();
  rt->setActionHandler([this](const std::string& name, const std::string& arg) { narrativeAction(name, arg); });
  rt->setEmitHandler([this](const std::string& ev) { uiEvents.push_back(ev); });
  rt->setSaveHandler([this, rt](const std::string& slot) { if (!safeName(slot)) return; const fs::path p = project.root / "Saves" / (slot + ".save.json"); std::error_code ec; fs::create_directories(p.parent_path(), ec); if (rt->saveToFile(p.string())) tool::logInfo("Narrative: saved " + slot); });
  narrRun = std::move(run);
  for (size_t i = 0; i < rt->varNames().size(); ++i) vars[rt->varNames()[i]] = static_cast<float>(rt->varAt(i));
  rt->fire("Start");
  syncNarrativeVars();
}

inline void Editor::syncNarrativeVars() {
  if (!narrRun) return;
  const auto& names = narrRun->rt->varNames();
  for (size_t i = 0; i < names.size(); ++i) vars[names[i]] = static_cast<float>(narrRun->rt->varAt(i));
}

// events: eventos de UI (botoes) deste quadro. Ordem: variaveis da UI -> narrativa, eventos, Update, carregamento pendente, variaveis -> UI.
inline void Editor::tickNarrative(float dt, const std::vector<std::string>& events) {
  if (!narrRun) return;
  tool::narr::Runtime& rt = *narrRun->rt; const auto& names = rt.varNames();
  for (size_t i = 0; i < names.size(); ++i) { auto it = vars.find(names[i]); if (it != vars.end() && it->second != static_cast<float>(rt.varAt(i))) rt.setVar(names[i], static_cast<double>(it->second)); }
  rt.beginTick();
  for (const std::string& ev : events) rt.fire(ev);
  rt.fire("Update"); rt.update(static_cast<double>(dt));
  if (const std::string slot = rt.takePendingLoad(); !slot.empty() && safeName(slot)) {
    const fs::path p = project.root / "Saves" / (slot + ".save.json");
    if (rt.loadFromFile(p.string())) tool::logInfo("Narrative: loaded " + slot); else tool::logWarn("Narrative: no valid save in slot '" + slot + "'");
  }
  syncNarrativeVars();
}

// Acoes disponiveis nos blocos "action" do grafo.
inline void Editor::narrativeAction(const std::string& name, const std::string& arg) {
  auto& w = scene.world();
  if (name == "log") tool::logInfo("[narrative] " + arg);
  else if (name == "spawn") { const tool::Entity t = findByName(arg); if (w.alive(t)) { const tool::Entity c = duplicate(t); onCreated(c, arg); } }
  else if (name == "destroy") { const tool::Entity t = findByName(arg); if (w.alive(t)) destroy(t); }
  else if (name == "ui_show" || name == "ui_hide") { const tool::Entity t = findByName(arg); if (auto* u = w.alive(t) ? w.get<UiCanvas>(t) : nullptr) u->visible = name == "ui_show"; }
  else if (name == "particles_on" || name == "particles_off") { const tool::Entity t = findByName(arg); if (auto* p = w.alive(t) ? w.get<ParticleComp>(t) : nullptr) p->emitting = name == "particles_on"; }
  else tool::logWarn("[narrative] unknown action '" + name + "'");
}

// Um objeto foi criado durante o Play: dispara "Create:<nome>" (e "Create:<modelo>" se veio de um spawn) e "Create".
inline void Editor::onCreated(tool::Entity e, const std::string& templateName) {
  if (!narrRun || !scene.world().alive(e)) return;
  if (const auto* n = scene.world().get<Name>(e)) narrRun->rt->fire(std::string("Create:") + n->text);
  if (!templateName.empty()) narrRun->rt->fire("Create:" + templateName);
  narrRun->rt->fire("Create");
}

} // namespace editor
