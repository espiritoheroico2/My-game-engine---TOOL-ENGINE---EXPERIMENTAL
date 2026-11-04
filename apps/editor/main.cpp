// ToolEngine Editor - janelas ancoraveis estilo Unity (ImGui docking) + SDL3.
// Janelas: Hierarchy, Inspector, Scene, Game, Assets, Scenes, Console, Profiler, Narrative Controller, Settings.
// O estado esta em editor_state.hpp; Assets/Scenes em assets_panel.hpp; componentes em components.hpp.
// Uso: tool_editor [--frames N] [--api vulkan|opengl|d3d12] [--lang pt-BR] [--tab N] [--play] [--screenshot-tab ...]
//   (N > 0 roda sem interacao e sai; usado nos testes automaticos)
#include "assets_panel.hpp"
#include <cctype>
#include "editor_state.hpp"
#include "export_ui.hpp"
#include "extra_ui.hpp"
#include "fx.hpp"
#include "i18n.hpp"
#include "project_dialog.hpp"
#include "selftest.hpp"
#include "viewport_tools.hpp"

#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_sdlrenderer3.h"
#include "imgui.h"
#include "imgui_internal.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <functional>
#include <unordered_map>
#include <chrono>
#include <cmath>
#include <memory>
#include <new>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

using editor::Editor;
using editor::T;
using editor::F;
using editor::W;
using editor::ViewXf;
using editor::line3;
namespace ed_ = editor;

// Lista (caminhos relativos ao projeto) os assets de um tipo, para os menus de escolha do Inspector.
// Cache por tipo: a varredura recursiva so roda ao mudar a pasta de Assets, ao haver operacao de arquivo
// (ed_::assetsRevision) ou a cada 2 s. Antes rodava a cada abertura de combo.
std::vector<std::string> assetsOfKind(const Editor& ed, ed_::AssetKind kind) {
  struct Cache { unsigned rev = ~0u; double t = -1e9; std::string root; std::vector<std::string> list; };
  static Cache caches[8];
  const size_t slot = static_cast<size_t>(kind) < 8 ? static_cast<size_t>(kind) : 7;
  Cache& c = caches[slot];
  const std::string root = ed.project.assetsDir().string();
  const double now = ImGui::GetTime();
  if (c.rev == ed_::assetsRevision() && c.root == root && now - c.t < 2.0) return c.list;
  c.rev = ed_::assetsRevision(); c.t = now; c.root = root; c.list.clear();
  std::error_code ec;
  if (!std::filesystem::exists(ed.project.assetsDir(), ec)) return c.list;
  for (auto& e : std::filesystem::recursive_directory_iterator(ed.project.assetsDir(), std::filesystem::directory_options::skip_permission_denied, ec)) {
    if (c.list.size() >= 500) break;
    if (e.is_regular_file(ec) && ed_::kindOf(e.path(), false) == kind) c.list.push_back(ed.project.relative(e.path()));
  }
  std::sort(c.list.begin(), c.list.end());
  return c.list;
}

// Campo "escolher asset": combo com os arquivos do tipo + aceita arrastar um asset do painel Assets.
bool assetField(Editor& ed, const char* label, char* buf, size_t cap, ed_::AssetKind kind) {
  bool changed = false;
  if (ImGui::BeginCombo(label, buf[0] ? buf : T("(none)"))) {
    if (ImGui::Selectable(T("(none)"), !buf[0])) { buf[0] = 0; changed = true; }
    for (auto& a : assetsOfKind(ed, kind)) if (ImGui::Selectable(a.c_str(), a == buf)) { ed_::ser::copyStr(buf, cap, a); changed = true; ed.assignAsset(a); }
    ImGui::EndCombo();
  }
  if (ImGui::BeginDragDropTarget()) {
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
      const std::string rel(static_cast<const char*>(p->Data));
      if (ed_::kindOf(rel, false) == kind) { ed_::ser::copyStr(buf, cap, rel); changed = true; ed.assignAsset(rel); }
    }
    ImGui::EndDragDropTarget();
  }
  return changed;
}

// Controles de pos-processamento (usados na camera e em Settings > Lighting).
bool postFxUi(tool::PostFx& p, bool showMode) {
  bool c = false;
  c |= ImGui::Checkbox(T("Enable post-processing"), &p.enabled);
  if (!p.enabled) return c;
  if (showMode) {
    const std::string modes = std::string(T("Add to the scene's global effect")) + '\0' + T("Replace the global effect") + '\0';
    int m = p.additive ? 0 : 1; if (ImGui::Combo(T("Mode"), &m, modes.c_str())) { p.additive = m == 0; c = true; }
  }
  c |= ImGui::SliderFloat(T("Exposure (EV)"), &p.exposure, -3.f, 3.f, "%.2f");
  c |= ImGui::SliderFloat(T("Contrast"), &p.contrast, 0.5f, 1.8f, "%.2f");
  c |= ImGui::SliderFloat(T("Saturation"), &p.saturation, 0.f, 2.f, "%.2f");
  c |= ImGui::ColorEdit3(T("Tint color"), p.tint); c |= ImGui::SliderFloat(T("Tint strength"), &p.tintStrength, 0.f, 1.f, "%.2f");
  c |= ImGui::SliderFloat(T("Vignette"), &p.vignette, 0.f, 1.f, "%.2f");
  if (p.vignette > 0.f) c |= ImGui::SliderFloat(T("Vignette size"), &p.vignetteSize, 0.2f, 1.4f, "%.2f");
  c |= ImGui::SliderFloat(T("Sharpen"), &p.sharpen, 0.f, 1.f, "%.2f");
  if (ImGui::SmallButton(T("Reset effect"))) { const bool add = p.additive; p = tool::PostFx{}; p.enabled = true; p.additive = add; c = true; }
  return c;
}

// Editor de tags do objeto selecionado: remove (x), adiciona (lista + campo) e renomeia a tag em TODOS os objetos.
void tagsUi(Editor& ed) {
  tool::World& w = ed.scene.world();
  const tool::TagMask m = w.tagMask(ed.selected);
  ImGui::SeparatorText("Tags");
  static char newTag[48] = ""; static char renameBuf[48] = ""; static int renaming = -1; static std::string tagMsg;
  for (tool::TagId i = 0; i < ed.scene.tags().size(); ++i) {
    if (!m.test(i)) continue;
    const std::string name(ed.scene.tags().name(i));
    ImGui::PushID(static_cast<int>(i));
    if (renaming == static_cast<int>(i)) {
      ImGui::SetNextItemWidth(110);
      if (ImGui::InputText("##ren", renameBuf, sizeof renameBuf, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
        const std::string to = renameBuf;
        if (ed.renameTag(i, to)) tagMsg.clear(); else tagMsg = T("Could not rename: the name is empty, too long, already used, or a built-in tag.");
        renaming = -1;
      }
      ImGui::SameLine(); if (ImGui::SmallButton(T("cancel"))) renaming = -1;
    } else {
      ImGui::Button(name.c_str()); // chip da tag
      if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !ed_::isBuiltinTag(name)) { renaming = static_cast<int>(i); ed_::ser::copyStr(renameBuf, sizeof renameBuf, name); }
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", ed_::isBuiltinTag(name) ? T("Built-in tag (cannot be renamed). Link your own tag to it in Settings > Tags.") : T("Double-click to rename (changes every object that has this tag)."));
      ImGui::SameLine(0, 2); if (ImGui::SmallButton("x")) w.removeTag(ed.selected, i);
    }
    ImGui::PopID();
    ImGui::SameLine(0, 10);
  }
  ImGui::NewLine();
  ImGui::SetNextItemWidth(130);
  const bool add = ImGui::InputTextWithHint("##newtag", T("new tag..."), newTag, sizeof newTag, ImGuiInputTextFlags_EnterReturnsTrue);
  ImGui::SameLine();
  if ((ImGui::Button(T("Add tag")) || add) && newTag[0]) { const tool::TagId id = ed.scene.tag(newTag); if (id != tool::kInvalidIndex) w.addTag(ed.selected, id); else tagMsg = T("Too many tags (limit 128)."); newTag[0] = 0; }
  if (ImGui::BeginCombo("##existing", T("existing..."))) {
    for (tool::TagId i = 0; i < ed.scene.tags().size(); ++i) if (!m.test(i) && ImGui::Selectable(std::string(ed.scene.tags().name(i)).c_str())) w.addTag(ed.selected, i);
    ImGui::EndCombo();
  }
  if (!tagMsg.empty()) ImGui::TextColored({1.f, .6f, .4f, 1.f}, "%s", tagMsg.c_str());
}

// Script do objeto: caminho, abrir/editar o texto, salvar.
using editor::ScriptIssue;
using editor::checkScriptSource;
using editor::issueSummary;

void scriptUi(Editor& ed) {
  tool::World& w = ed.scene.world();
  auto* sc = w.get<ed_::ScriptComp>(ed.selected);
  if (!ImGui::CollapsingHeader("ToolObject Script", ImGuiTreeNodeFlags_DefaultOpen)) return;
  if (!sc) { if (ImGui::SmallButton(T("Help: script examples"))) editor::openHelp("script_examples"); if (ImGui::Button(T("Add Script"))) ed.attachScript(ed.selected); if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("Creates a script file for this object (optional).")); return; }
  if (ImGui::SmallButton(T("Help: script examples"))) editor::openHelp("script_examples");
  ImGui::Checkbox(T("Script enabled"), &sc->enabled);
  ImGui::TextDisabled("%s", sc->path[0] ? sc->path : T("(no file)"));
  if (sc->path[0]) { ImGui::SameLine(); if (ImGui::SmallButton(T("Edit external"))) editor::openInExternalEditor(ed, sc->path); if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("Opens the file in your code editor (Settings > Project).")); }
  static tool::Entity editing; static std::string text; static bool loaded = false, dirty = false; static std::string msg;
  // Verificador em tempo real: roda 0,4 s depois da ultima tecla (e logo ao abrir o script).
  static std::vector<ScriptIssue> issues; static double dirtyAt = -10.0; static bool checkPending = false;
  if (editing != ed.selected) { editing = ed.selected; loaded = false; dirty = false; msg.clear(); issues.clear(); checkPending = false; }
  if (!loaded && ImGui::Button(T("Edit script"))) {
    if (!sc->path[0]) { const std::string rel = ed_::createScriptFile(ed.project, w.get<ed_::Name>(ed.selected)->text); if (!rel.empty()) ed_::ser::copyStr(sc->path, sizeof sc->path, rel); }
    loaded = ed_::readScript(ed.project, sc->path, text); msg = loaded ? "" : T("Could not read the script file."); dirty = false;
    checkPending = loaded; dirtyAt = -10.0;
  }
  if (loaded) {
    if (ImGui::InputTextMultiline("##script", &text[0], text.capacity() + 1, ImVec2(-1, 220), ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_AllowTabInput,
        [](ImGuiInputTextCallbackData* d) { if (d->EventFlag == ImGuiInputTextFlags_CallbackResize) { auto* t = static_cast<std::string*>(d->UserData); t->resize(static_cast<size_t>(d->BufTextLen)); d->Buf = &(*t)[0]; } return 0; }, &text)) {
      dirty = true; checkPending = true; dirtyAt = ImGui::GetTime();
    }
    if (checkPending && ImGui::GetTime() - dirtyAt >= 0.4) { issues = checkScriptSource(text, sc->path); checkPending = false; }
    if (ImGui::Button(T("Save script"))) {
      msg = ed_::writeScript(ed.project, sc->path, text) ? T("Script saved.") : T("Could not save the script."); dirty = false;
      // Ao salvar, o resultado tambem vai para o Console (com o objeto e a linha).
      issues = checkScriptSource(text, sc->path); checkPending = false;
      const std::string who = w.get<ed_::Name>(ed.selected)->text;
      for (const ScriptIssue& is : issues) {
        tool::debug().report(is.code.c_str(), is.sev, "[" + who + "] line " + std::to_string(is.line) + ": " + is.msg, "", is.line);
      }
      tool::logInfo("[" + who + "] saved: " + issueSummary(issues));
    }
    ImGui::SameLine(); if (ImGui::Button(T("Close"))) loaded = false;
    if (dirty) { ImGui::SameLine(); ImGui::TextDisabled("%s", T("(unsaved)")); }
    // Resultado da verificacao: OK, ou a lista de erros e avisos com a linha de cada um.
    bool anyError = false;
    for (const ScriptIssue& is : issues) if (is.sev == tool::Severity::Error) anyError = true;
    if (issues.empty()) ImGui::TextColored({.5f, 1.f, .5f, 1.f}, "%s", T("Check: OK (no problems found)"));
    else ImGui::TextColored(anyError ? ImVec4(1, .4f, .4f, 1) : ImVec4(1, .85f, .3f, 1), "%s", (std::string(T("Check")) + ": " + issueSummary(issues)).c_str());
    ImGui::BeginChild("##scriptissues", ImVec2(-1, 120), true);
    const std::vector<std::string> src = [&] { std::vector<std::string> v; size_t p = 0; while (p <= text.size()) { size_t e = text.find('\n', p); if (e == std::string::npos) e = text.size(); v.push_back(text.substr(p, e - p)); p = e + 1; } return v; }();
    for (size_t i = 0; i < issues.size() && i < 60; ++i) {
      const ScriptIssue& is = issues[i];
      const ImVec4 c = is.sev == tool::Severity::Error ? ImVec4(1, .4f, .4f, 1) : ImVec4(1, .85f, .3f, 1);
      ImGui::TextColored(c, "%s", ("line " + std::to_string(is.line) + "  " + is.code + "  " + is.msg).c_str());
      if (is.line >= 1 && static_cast<size_t>(is.line) <= src.size()) ImGui::TextDisabled("    %s", src[static_cast<size_t>(is.line) - 1].c_str());
    }
    if (issues.size() > 60) ImGui::TextDisabled("%s", ("... " + std::to_string(issues.size() - 60) + " more (see Console: Check scripts)").c_str());
    ImGui::EndChild();
  }
  if (!msg.empty()) ImGui::TextDisabled("%s", msg.c_str());
  // Estado do script durante o Play (QuickJS). Fora do Play, o script so e guardado e editado.
  if (ed.playing) {
    const std::string st = ed.scriptHost.statusOf(ed.selected.index, ed.selected.generation);
    ImGui::TextDisabled("%s: %s", T("Script status"), st.c_str());
    if (ImGui::SmallButton(T("Reload scripts"))) ed.startScripts();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("Restarts the scripts of this scene (state is reset)."));
  } else {
    ImGui::TextDisabled("%s", T("Scripts run while the game is playing (Play)."));
  }
  if (!ed_::ScriptHost::compiledIn()) ImGui::TextColored({1.f, .6f, .4f, 1.f}, "%s", T("This build has no script runtime (QuickJS is off)."));
}

// --- Hierarquia ------------------------------------------------------------------------------------------------
// Icones da Hierarquia (olho e cadeado), desenhados com primitivas para nao depender de fonte.
// kind 0 = olho (on = visivel); kind 1 = cadeado (on = travado). Retorna true no clique.
bool rowIcon(const char* id, int kind, bool on, const char* tip) {
  const float s = ImGui::GetFrameHeight() * 0.8f;
  ImGui::PushID(id);
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const bool clicked = ImGui::InvisibleButton("##icon", ImVec2(s, s));
  const bool hov = ImGui::IsItemHovered();
  if (hov && tip && tip[0]) ImGui::SetTooltip("%s", tip);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImU32 col = on ? ImGui::GetColorU32(ImGuiCol_Text) : ImGui::GetColorU32(ImGuiCol_TextDisabled);
  if (hov) dl->AddRectFilled(p, ImVec2(p.x + s, p.y + s), ImGui::GetColorU32(ImGuiCol_FrameBgHovered), 3.f);
  const ImVec2 c(p.x + s * 0.5f, p.y + s * 0.5f);
  if (kind == 0) { // olho: amendoa + pupila; apagado = com barra
    const float w = s * 0.46f, h = s * 0.26f;
    dl->AddBezierCubic(ImVec2(c.x - w, c.y), ImVec2(c.x - w * 0.5f, c.y - h * 1.6f), ImVec2(c.x + w * 0.5f, c.y - h * 1.6f), ImVec2(c.x + w, c.y), col, 1.4f);
    dl->AddBezierCubic(ImVec2(c.x - w, c.y), ImVec2(c.x - w * 0.5f, c.y + h * 1.6f), ImVec2(c.x + w * 0.5f, c.y + h * 1.6f), ImVec2(c.x + w, c.y), col, 1.4f);
    dl->AddCircleFilled(c, s * 0.12f, col);
    if (!on) dl->AddLine(ImVec2(c.x - s * 0.38f, c.y - s * 0.38f), ImVec2(c.x + s * 0.38f, c.y + s * 0.38f), col, 1.5f);
  } else { // cadeado: corpo + haste; travado = haste fechada e corpo cheio
    const float bw = s * 0.5f, top = c.y - s * 0.02f, bot = c.y + s * 0.36f;
    const float arcY = on ? top : top - s * 0.12f; // destravado: haste levantada
    dl->PathArcTo(ImVec2(c.x, arcY), s * 0.16f, IM_PI, 2.f * IM_PI, 12);
    dl->PathStroke(col, 0, 1.5f);
    if (on) dl->AddRectFilled(ImVec2(c.x - bw * 0.5f, top), ImVec2(c.x + bw * 0.5f, bot), col, 2.f);
    else dl->AddRect(ImVec2(c.x - bw * 0.5f, top), ImVec2(c.x + bw * 0.5f, bot), col, 2.f, 0, 1.4f);
  }
  ImGui::PopID();
  return clicked;
}

void drawHierarchy(Editor& ed) {
  if (!ImGui::Begin(W("Hierarchy", "hier").c_str(), &ed.showHierarchy)) { ImGui::End(); return; }
  editor::helpIcon("objects");
  tool::World& w = ed.scene.world();
  if (ImGui::Button(T("+ Create"))) ImGui::OpenPopup("createmenu");
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("Create an object, camera, light or sound"));
  if (ImGui::BeginPopup("createmenu")) {
    if (ImGui::MenuItem(T("Empty object"))) ed.selected = ed.make("Empty");
    if (ImGui::MenuItem(T("Cube"))) ed.selected = ed.makeMesh("Cube", 0, 0, 0.5f, 0);
    if (ImGui::MenuItem(T("Sphere"))) ed.selected = ed.makeMesh("Sphere", 1, 0, 0.5f, 0);
    if (ImGui::MenuItem(T("Plane"))) ed.selected = ed.makeMesh("Plane", 2, 0, 0, 0);
    if (ImGui::BeginMenu(T("More shapes"))) {
      if (ImGui::MenuItem(T("Cylinder"))) ed.selected = ed.makeMesh("Cylinder", 4, 0, 0.5f, 0);
      if (ImGui::MenuItem(T("Cone"))) ed.selected = ed.makeMesh("Cone", 5, 0, 0.5f, 0);
      if (ImGui::MenuItem(T("Capsule"))) ed.selected = ed.makeMesh("Capsule", 6, 0, 0.5f, 0);
      if (ImGui::MenuItem(T("Wheel"))) ed.selected = ed.makeMesh("Wheel", 7, 0, 0.5f, 0);
      if (ImGui::MenuItem(T("Triangle prism"))) ed.selected = ed.makeMesh("Triangle", 8, 0, 0.5f, 0);
      if (ImGui::MenuItem(T("Hexagon prism"))) ed.selected = ed.makeMesh("Hexagon", 9, 0, 0.5f, 0);
      ImGui::EndMenu();
    }
    ImGui::Separator();
    if (ImGui::MenuItem(T("Camera"))) ed.selected = ed.makeCamera(false);
    if (ImGui::MenuItem(T("Main Camera (Principal)"))) ed.selected = ed.makeCamera(true);
    if (ImGui::MenuItem(T("Global Light"))) ed.selected = ed.makeGlobalLight();
    if (ImGui::MenuItem(T("Point Light"))) ed.selected = ed.makePointLight();
    if (ImGui::MenuItem(T("Audio Source"))) ed.selected = ed.makeAudio("");
    ImGui::Separator();
    if (ImGui::MenuItem(T("Voxel chunk (blocks)"))) ed.selected = ed.makeVoxel("Chunk", 0, 0, true);
    if (ImGui::MenuItem(T("Particle System"))) { ed.selected = ed.make("Particles"); w.add<ed_::ParticleComp>(ed.selected); w.get<ed_::Transform>(ed.selected)->pos[1] = 1.f; }
    if (ImGui::MenuItem(T("Game UI (canvas)"))) { ed.selected = ed.make("UI"); ed_::UiCanvas u; ed_::UiElem& el = u.items[u.count++]; ed_::ser::copyStr(el.text, sizeof el.text, "Score: {score}"); w.add<ed_::UiCanvas>(ed.selected, u); }
    ImGui::EndPopup();
  }
  ImGui::SameLine();
  const bool has = w.alive(ed.selected);
  if (ImGui::Button(T("Duplicate")) && has) ed.selected = ed.duplicate(ed.selected);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("Copy the selected object"));
  ImGui::SameLine();
  if (ImGui::Button(T("Delete")) && has) { if (w.has<ed_::Locked>(ed.selected)) ed.statusLine = T("Object is locked: unlock it (padlock) before deleting."); else ed.destroy(ed.selected); }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("Delete the selected object and its children (Delete key)"));
  if (ImGui::CollapsingHeader(T("Pool demo (DeleteAndReuse)"))) {
    if (ImGui::Button(T("+ Enemy"))) ed.spawnEnemy();
    ImGui::SameLine();
    if (ImGui::Button("DeleteAndReuse") && has) { tool::ToolObject(&ed.scene, ed.selected).DeleteAndReuse(); ed.selected = {}; }
    ImGui::SameLine();
    if (ImGui::Button(T("Clear pool"))) ed.scene.flushPool();
  }
  ImGui::Separator();
  // Arvore: mapa pai -> filhos (1 passada), desenho recursivo. Arrastar um objeto sobre outro o torna filho (mantem a posicao no mundo).
  std::unordered_map<uint64_t, std::vector<tool::Entity>> kids; std::vector<tool::Entity> roots;
  auto key = [](tool::Entity e) { return (static_cast<uint64_t>(e.generation) << 32) | e.index; };
  w.eachEntity([&](tool::Entity e) { const tool::Entity par = ed.parentOf(e); if (w.alive(par)) kids[key(par)].push_back(e); else roots.push_back(e); });
  tool::Entity toDelete{}; tool::Entity dragChild{}, dragParent{}; bool doReparent = false, doUnparent = false;
  tool::Entity renameE{}; bool wantRename = false; static char renBuf[40] = "";
  auto beginRename = [&](tool::Entity e) { renameE = e; wantRename = true; if (auto* nm = w.get<ed_::Name>(e)) std::snprintf(renBuf, sizeof renBuf, "%s", nm->text); };
  // Com muitos objetos, a arvore vira uma lista achatada (indentada) dentro de um ImGuiListClipper: so as linhas
  // visiveis sao montadas. Abaixo do limite, a arvore com setas continua como antes.
  constexpr size_t kFlatHierarchy = 300;
  std::vector<std::pair<tool::Entity, int>> flat;
  std::function<void(tool::Entity, int)> collect = [&](tool::Entity e, int depth) {
    flat.push_back({e, depth});
    auto it = kids.find(key(e));
    if (it != kids.end() && depth < 32) for (tool::Entity c : it->second) collect(c, depth + 1);
  };
  for (tool::Entity r : roots) collect(r, 0);
  const bool flatMode = flat.size() > kFlatHierarchy;
  std::function<void(tool::Entity, int)> row = [&](tool::Entity e, int depth) {
    auto* n = w.get<ed_::Name>(e);
    const char* prefix = w.has<ed_::CameraComp>(e) ? (ed.isPrincipal(e) ? "[cam*] " : "[cam] ") : w.has<ed_::GlobalLight>(e) ? "[sun] " : w.has<ed_::LightComp>(e) ? "[light] " : w.has<ed_::AudioSource>(e) ? "[snd] " : w.has<ed_::ParticleComp>(e) ? "[fx] " : w.has<ed_::UiCanvas>(e) ? "[ui] " : "";
    ImGui::PushID(static_cast<int>(e.index) ^ (static_cast<int>(e.generation) << 20));
    const float indent = flatMode ? static_cast<float>(depth) * 14.f : 0.f;
    if (indent > 0.f) ImGui::Indent(indent);
    { // olho (ocultar/mostrar) e cadeado (travar), antes do nome
      const bool hiddenE = w.has<ed_::Inactive>(e), lockedE = w.has<ed_::Locked>(e);
      if (rowIcon("eye", 0, !hiddenE, T("Show / hide the object. Hidden objects are not drawn, simulated or lit."))) {
        if (hiddenE) w.remove<ed_::Inactive>(e); else w.add<ed_::Inactive>(e, ed_::Inactive{});
      }
      ImGui::SameLine(0, 2);
      if (rowIcon("lock", 1, lockedE, T("Lock the object: it can't be moved, edited or deleted in the editor."))) {
        if (lockedE) w.remove<ed_::Locked>(e); else w.add<ed_::Locked>(e, ed_::Locked{});
      }
      ImGui::SameLine(0, 4);
    }
    char label[96]; std::snprintf(label, sizeof label, "%s%s##%u", prefix, n ? n->text : T("(unnamed)"), e.index);
    auto it = kids.find(key(e)); const bool leaf = it == kids.end();
    bool open = false;
    if (flatMode) ImGui::Selectable(label, ed.selected == e, ImGuiSelectableFlags_SpanAvailWidth);
    else {
      ImGuiTreeNodeFlags fl = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen | (leaf ? ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen : 0) | (ed.selected == e ? ImGuiTreeNodeFlags_Selected : 0);
      open = ImGui::TreeNodeEx(label, fl);
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) { ed.selected = e; if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) ed.focusOn(e); }
    if (ImGui::BeginDragDropSource()) { ImGui::SetDragDropPayload("HIER_ENTITY", &e, sizeof e); ImGui::TextUnformatted(n ? n->text : "?"); ImGui::EndDragDropSource(); }
    if (ImGui::BeginDragDropTarget()) {
      if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("HIER_ENTITY"); pl && pl->DataSize == sizeof(tool::Entity)) { dragChild = *static_cast<const tool::Entity*>(pl->Data); dragParent = e; doReparent = true; }
      ImGui::EndDragDropTarget();
    }
    if (ImGui::BeginPopupContextItem()) {
      ed.selected = e;
      if (ImGui::MenuItem(T("Focus"))) ed.focusOn(e);
      if (ImGui::MenuItem(T("Create child"))) { const tool::Entity c = ed.make("Child"); ed.setParent(c, e, false); ed.selected = c; }
      if (ImGui::MenuItem(T("Rename"), "F2")) beginRename(e);
      if (ImGui::MenuItem(T("Duplicate"))) ed.selected = ed.duplicate(e);
      if (ed.parentOf(e).index != tool::Entity{}.index && w.alive(ed.parentOf(e)) && ImGui::MenuItem(T("Detach from parent"))) { dragChild = e; doUnparent = true; }
      if (ImGui::MenuItem(T("Delete"))) toDelete = e;
      ImGui::EndPopup();
    }
    if (!flatMode && !leaf && open) { if (depth < 32) for (tool::Entity c : it->second) row(c, depth + 1); ImGui::TreePop(); }
    if (indent > 0.f) ImGui::Unindent(indent);
    ImGui::PopID();
  };
  if (flatMode) {
    ImGuiListClipper clip;
    clip.Begin(static_cast<int>(flat.size()));
    while (clip.Step()) for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) row(flat[static_cast<size_t>(i)].first, flat[static_cast<size_t>(i)].second);
    clip.End();
  } else for (tool::Entity r : roots) row(r, 0);
  // soltar no espaco vazio da janela = remove o pai
  ImGui::Dummy({ImGui::GetContentRegionAvail().x, std::max(24.f, ImGui::GetContentRegionAvail().y)});
  if (ImGui::BeginDragDropTarget()) {
    if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("HIER_ENTITY"); pl && pl->DataSize == sizeof(tool::Entity)) { dragChild = *static_cast<const tool::Entity*>(pl->Data); doUnparent = true; }
    ImGui::EndDragDropTarget();
  }
  if (doReparent && w.alive(dragChild) && w.alive(dragParent) && !w.has<ed_::Locked>(dragChild)) { ed.setParent(dragChild, dragParent, true); }
  if (doUnparent && w.alive(dragChild) && !w.has<ed_::Locked>(dragChild)) { ed.setParent(dragChild, tool::Entity{}, true); }
  if (w.alive(toDelete) && !w.has<ed_::Locked>(toDelete)) ed.destroy(toDelete);
  else if (w.alive(toDelete)) ed.statusLine = T("Object is locked: unlock it (padlock) before deleting.");
  // Teclas de objeto so valem fora de campos de texto (senao Delete/F2 digitados apagavam o objeto).
  const bool textNow = ImGui::GetIO().WantTextInput;
  if (has && !textNow && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_F2)) beginRename(ed.selected);
  if (wantRename) ImGui::OpenPopup("renobj");
  if (ImGui::BeginPopupModal("renobj", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted(T("New name:"));
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    const bool enter = ImGui::InputText("##renbox", renBuf, sizeof renBuf, ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::Button(T("OK"), {110, 0}) || enter) {
      if (w.alive(renameE) && renBuf[0]) if (auto* nm = w.get<ed_::Name>(renameE)) ed_::ser::copyStr(nm->text, sizeof nm->text, renBuf);
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine(); if (ImGui::Button(T("Cancel"), {110, 0})) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
  if (has && !textNow && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Delete)) {
    if (w.has<ed_::Locked>(ed.selected)) ed.statusLine = T("Object is locked: unlock it (padlock) before deleting.");
    else ed.destroy(ed.selected);
  }
  ImGui::End();
}

// --- Inspetor --------------------------------------------------------------------------------------------------
// Inspetor: helpers de layout. Linha X/Y/Z colorida (rotulo a esquerda), secoes que podem ser abertas/fechadas em massa.
bool vec3Row(const char* label, float* v, float speed, const char* fmt) {
  ImGui::PushID(label);
  ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(label);
  ImGui::SameLine(92.f);
  const float w = (ImGui::GetContentRegionAvail().x - 2.f * ImGui::GetStyle().ItemSpacing.x) / 3.f;
  static const ImVec4 axis[3] = {{0.95f, 0.38f, 0.38f, 1.f}, {0.50f, 0.85f, 0.45f, 1.f}, {0.42f, 0.62f, 1.f, 1.f}};
  static const char* names[3] = {"X", "Y", "Z"};
  bool changed = false;
  for (int i = 0; i < 3; ++i) {
    if (i) ImGui::SameLine();
    ImGui::PushID(i);
    ImGui::AlignTextToFramePadding(); ImGui::TextColored(axis[i], "%s", names[i]); ImGui::SameLine(0.f, 4.f);
    ImGui::SetNextItemWidth(w - 18.f);
    changed |= ImGui::DragFloat("##v", &v[i], speed, 0.f, 0.f, fmt);
    ImGui::PopID();
  }
  ImGui::PopID();
  return changed;
}
int gSecMode = 0;   // aplicado neste quadro: 1 = abrir todas as secoes, -1 = fechar todas
int gSecNext = 0;   // pedido pelo rodape; vale no quadro seguinte
bool secHeader(const char* label, ImGuiTreeNodeFlags fl = 0) {
  if (gSecMode) ImGui::SetNextItemOpen(gSecMode > 0, ImGuiCond_Always);
  return ImGui::CollapsingHeader(label, fl);
}

void drawInspector(Editor& ed) {
  gSecMode = gSecNext; gSecNext = 0;
  if (!ImGui::Begin(W("Inspector", "insp").c_str(), &ed.showInspector)) { ImGui::End(); return; }
  editor::helpIcon("objects");
  tool::World& w = ed.scene.world();
  if (!w.alive(ed.selected)) { ImGui::TextDisabled("%s", T("no object selected")); ImGui::End(); return; }
  ImGui::TextDisabled("%s", F("Entity %u (generation %u)", ed.selected.index, ed.selected.generation).c_str());
  if (auto* n = w.get<ed_::Name>(ed.selected)) ImGui::InputText(T("Name"), n->text, sizeof n->text);
  { bool active = !w.has<ed_::Inactive>(ed.selected);
    if (ImGui::Checkbox(T("Active"), &active)) { if (active) { if (w.has<ed_::Inactive>(ed.selected)) w.remove<ed_::Inactive>(ed.selected); } else if (!w.has<ed_::Inactive>(ed.selected)) w.add<ed_::Inactive>(ed.selected, ed_::Inactive{}); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("Inactive objects are not drawn, simulated or lit, and their Tools do not run.")); }
  ImGui::SameLine(); { bool lk = w.has<ed_::Locked>(ed.selected);
    if (ImGui::Checkbox(T("Locked (editor only)"), &lk)) { if (lk) w.add<ed_::Locked>(ed.selected, ed_::Locked{}); else w.remove<ed_::Locked>(ed.selected); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("Locked objects can't be moved, edited or deleted in the editor. The game is not affected."));
  }
  // Travado: o resto do Inspector fica so de leitura (o checkbox acima continua funcionando).
  const bool lockedNow = w.has<ed_::Locked>(ed.selected);
  ImGui::BeginDisabled(lockedNow);
  if (auto* t = w.get<ed_::Transform>(ed.selected)) {
    vec3Row(T("Position"), t->pos, 0.05f, "%.2f"); vec3Row(T("Rotation"), t->rot, 0.5f, "%.1f"); vec3Row(T("Scale"), t->scale, 0.01f, "%.2f");
  }
  if (auto* mr = w.get<ed_::MeshRenderer>(ed.selected)) {
    if (secHeader("Mesh Renderer", ImGuiTreeNodeFlags_DefaultOpen)) {
    const std::string shapes = std::string(T("Cube")) + '\0' + T("Sphere") + '\0' + T("Plane") + '\0' + T("Model") + '\0' + T("Cylinder") + '\0' +
                               T("Cone") + '\0' + T("Capsule") + '\0' + T("Wheel") + '\0' + T("Triangle prism") + '\0' + T("Hexagon prism") + '\0';
    ImGui::Combo(T("Shape"), &mr->shape, shapes.c_str());
    if (mr->shape == 3 && assetField(ed, T("Model file"), mr->model, sizeof mr->model, ed_::AssetKind::Model)) ed.lib.invalidate(mr->model);
    if (mr->shape == 3 && !mr->model[0]) ImGui::TextDisabled("%s", T("Import a .glb/.gltf/.fbx in Assets and pick it here."));
    if (assetField(ed, T("Texture"), mr->texture, sizeof mr->texture, ed_::AssetKind::Image)) ed.lib.invalidate(mr->texture);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("Image in Assets (PNG, JPG, BMP, TGA) used as the colour of the object. Multiplies the Color."));
    ImGui::ColorEdit3(T("Color"), mr->color);
    ImGui::SliderFloat(T("Roughness"), &mr->roughness, 0.05f, 1.f); ImGui::SliderFloat(T("Metallic"), &mr->metallic, 0.f, 1.f);
    ImGui::Checkbox(T("Cast shadow"), &mr->castShadow); ImGui::SameLine(); ImGui::Checkbox(T("Receive shadow"), &mr->receiveShadow);
    }
  }
  if (auto* lc = w.get<ed_::LightComp>(ed.selected)) {
    if (secHeader(T("Light source"), ImGuiTreeNodeFlags_DefaultOpen)) {
    const std::string types = std::string(T("Directional")) + '\0' + T("Point") + '\0' + "Spot" + '\0';
    int ty = static_cast<int>(lc->light.type); if (ImGui::Combo(T("Type"), &ty, types.c_str())) lc->light.type = static_cast<tool::LightType>(ty);
    ImGui::ColorEdit3(T("Light color"), &lc->light.color.x); ImGui::DragFloat(T("Intensity"), &lc->light.intensity, 0.1f, 0.f, 200.f); ImGui::DragFloat(T("Range"), &lc->light.range, 0.1f, 0.1f, 200.f);
    ImGui::DragFloat3(T("Direction"), &lc->light.direction.x, 0.02f);
    }
  }
  if (auto* g = w.get<ed_::GlobalLight>(ed.selected)) {
    if (secHeader(T("Global Light (sun)"), ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::ColorEdit3(T("Light color"), g->color);
    ImGui::DragFloat(T("Intensity"), &g->intensity, 0.05f, 0.f, 20.f);
    ImGui::SliderFloat(T("Direction (azimuth)"), &g->azimuth, 0.f, 360.f, "%.0f deg");
    ImGui::SliderFloat(T("Height (elevation)"), &g->elevation, -10.f, 90.f, "%.0f deg");
    ImGui::ColorEdit3(T("Ambient light"), g->ambient);
    ImGui::Checkbox(T("Sun casts shadows"), &g->castShadow);
    ImGui::Checkbox(T("Dynamic (day/night cycle while playing)"), &g->dynamic);
    if (g->dynamic) ImGui::DragFloat(T("Speed (deg/s)"), &g->speed, 0.5f, 0.f, 360.f);
    ImGui::TextDisabled("%s", T("Only the first Global Light in the scene is used."));
    }
  }
  if (auto* c = w.get<ed_::CameraComp>(ed.selected)) {
    if (secHeader(T("Camera"), ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Checkbox(T("2D camera (orthographic)"), &c->ortho2D);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("No perspective: objects keep their size with distance. Use it for 2D games. Field of view is ignored."));
    if (c->ortho2D) ImGui::DragFloat(T("Visible height (world units)"), &c->orthoSize, 0.1f, 0.5f, 500.f, "%.1f");
    else ImGui::SliderFloat(T("Field of view"), &c->fovDeg, 10.f, 120.f, "%.0f deg");
    ImGui::DragFloat(T("Near"), &c->nearZ, 0.01f, 0.01f, 10.f); ImGui::DragFloat(T("Far"), &c->farZ, 1.f, 1.f, 5000.f);
    bool pr = ed.isPrincipal(ed.selected);
    if (ImGui::Checkbox(T("Principal (active game camera)"), &pr)) ed.setPrincipal(ed.selected, pr);
    if (pr) {
      const size_t n = ed.principalCameras().size();
      if (n >= 2) ImGui::TextColored({0.6f, 0.9f, 0.6f, 1.f}, "%s", F("%zu Principal cameras: split screen is on.", n).c_str());
      const std::string modes = std::string(T("Vertical split (side by side)")) + '\0' + T("Horizontal split (top / bottom)") + '\0';
      int sv = ed.sceneInfo.splitVertical ? 0 : 1;
      if (ImGui::Combo(T("Split screen"), &sv, modes.c_str())) ed.sceneInfo.splitVertical = sv == 0;  // valor unico: vale para todas as Principais
      ImGui::TextDisabled("%s", T("Shared by every Principal camera: changing it here changes it for all."));
      if (n > 2) ImGui::TextColored({1.f, .8f, .3f, 1.f}, "%s", T("More than two Principal cameras: only the first two are shown."));
    }
    ImGui::Checkbox(T("Sound ear (audio listener)"), &c->soundEar);
    if (c->soundEar) ImGui::SliderFloat(T("Master volume"), &c->earVolume, 0.f, 2.f);
    if (ImGui::TreeNode(T("Post-processing (this camera)"))) { postFxUi(c->post, true); ImGui::TreePop(); }
    }
  }
  if (auto* a = w.get<ed_::AudioSource>(ed.selected)) {
    if (secHeader(T("Audio Source"), ImGuiTreeNodeFlags_DefaultOpen)) {
    if (assetField(ed, T("Sound file"), a->clip, sizeof a->clip, ed_::AssetKind::Audio)) ed.lib.invalidate(a->clip);
    ImGui::SliderFloat(T("Volume"), &a->volume, 0.f, 2.f);
    ImGui::Checkbox(T("Loop"), &a->loop); ImGui::SameLine(); ImGui::Checkbox(T("Play on start"), &a->playOnStart);
    ImGui::Checkbox(T("3D sound (needs a Sound Ear)"), &a->spatial);
    if (a->spatial) { ImGui::DragFloat(T("Min distance"), &a->minDistance, 0.1f, 0.1f, 100.f); ImGui::DragFloat(T("Max distance"), &a->maxDistance, 0.5f, 1.f, 1000.f); }
    if (ImGui::Button(T("Preview"))) { if (a->clip[0]) ed.previewSound(a->clip); }
    ImGui::SameLine(); if (ImGui::Button(T("Stop"))) ed.mixer.stop(ed.previewVoice);
    }
  }
  editor::physicsUi(ed);
  editor::toolsUi(ed);
  editor::voxelUi(ed);
  editor::uiCanvasUi(ed);
  editor::particleUi(ed);
  editor::addResourceUi(ed);
  scriptUi(ed);
  tagsUi(ed);
  // Rodape: adicionar componente (mesmo menu do "Add New"), abrir/fechar todas as secoes.
  ImGui::Separator();
  if (ImGui::Button(T("Add Component"))) ImGui::OpenPopup("addres");
  ImGui::SameLine(); if (ImGui::Button(T("Collapse All"))) gSecNext = -1;
  ImGui::SameLine(); if (ImGui::Button(T("Open All"))) gSecNext = 1;
  ImGui::EndDisabled();
  gSecMode = 0;
  ImGui::End();
}

void drawConsole(Editor& ed) {
  if (!ImGui::Begin(W("Console", "cons").c_str(), &ed.showConsole)) { ImGui::End(); return; }
  editor::helpIcon("start");
  if (ImGui::Button(T("Clear"))) ed.console.clear();
  ImGui::SameLine(); if (ImGui::Button(T("Check scripts"))) ed.checkAllScripts();
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("Checks every script of the scene (syntax and common mistakes) and lists them here."));
  ImGui::SameLine(); const bool copyNow = ImGui::Button(T("Copy"));
  ImGui::SameLine(); ImGui::Text("%s", F("errors: %zu  warnings: %zu", tool::debug().count(tool::Severity::Error), tool::debug().count(tool::Severity::Warning)).c_str());
  // Filtros: por nivel e por texto (ex.: "Cubo" mostra so o que fala do Cubo).
  static bool showInfo = true, showWarn = true, showErr = true;
  static char filt[128] = "";
  ImGui::Checkbox(T("Info"), &showInfo); ImGui::SameLine();
  ImGui::Checkbox(T("Warnings"), &showWarn); ImGui::SameLine();
  ImGui::Checkbox(T("Errors"), &showErr); ImGui::SameLine();
  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##logfilter", T("filter (e.g. object name or TOOL-S007)"), filt, sizeof filt);
  ImGui::Separator();
  // Procura sem diferenca de maiusculas
  auto hasText = [](const std::string& m, const char* f) {
    if (!f[0]) return true;
    auto low = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
    const std::string fl = [&] { std::string r; for (const char* q = f; *q; ++q) r += low(static_cast<unsigned char>(*q)); return r; }();
    std::string ml; ml.reserve(m.size()); for (unsigned char c : m) ml += low(c);
    return ml.find(fl) != std::string::npos;
  };
  std::string copied;
  // A area do log reserva a linha de comando embaixo. Sem essa altura, o log ocupava tudo e o campo saia da tela.
  const float cmdRow = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
  ImGui::BeginChild("log", ImVec2(0, -cmdRow));
  size_t rowId = 0;
  for (auto& [lvl, msg] : ed.console) {
    ImGui::PushID(static_cast<int>(rowId++));
    const bool show = lvl == tool::LogLevel::Error ? showErr : lvl == tool::LogLevel::Warn ? showWarn : showInfo;
    if (!show || !hasText(msg, filt)) { ImGui::PopID(); continue; }
    if (copyNow) copied += msg + "\n";
    const ImVec4 c = lvl == tool::LogLevel::Error ? ImVec4(1, .4f, .4f, 1) : lvl == tool::LogLevel::Warn ? ImVec4(1, .85f, .3f, 1) : ImGui::GetStyleColorVec4(ImGuiCol_Text);
    ImGui::PushStyleColor(ImGuiCol_Text, c);
    // Clicar numa linha "[Nome] ..." seleciona o objeto Nome na cena.
    if (ImGui::Selectable(msg.c_str())) {
      const size_t lb = msg.find('['), rb = msg.find(']');
      if (lb != std::string::npos && rb != std::string::npos && rb > lb + 1) {
        const tool::Entity e = ed.findByName(msg.substr(lb + 1, rb - lb - 1));
        if (ed.scene.world().alive(e)) ed.selected = e;
      }
    }
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered() && msg.find('[') != std::string::npos) ImGui::SetTooltip("%s", T("Click to select the object in the brackets."));
    ImGui::PopID();
  }
  if (copyNow) ImGui::SetClipboardText(copied.c_str());
  if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.f);
  ImGui::EndChild();
  // Linha de comando: "help", "test ECS: 10000", "clear". Setas Cima/Baixo mostram os comandos anteriores.
  struct CmdHistory {
    std::vector<std::string> items; int pos = -1;
    static int callback(ImGuiInputTextCallbackData* d) {
      auto* h = static_cast<CmdHistory*>(d->UserData);
      if (d->EventFlag != ImGuiInputTextFlags_CallbackHistory || h->items.empty()) return 0;
      const int before = h->pos;
      if (d->EventKey == ImGuiKey_UpArrow) { if (h->pos == -1) h->pos = static_cast<int>(h->items.size()) - 1; else if (h->pos > 0) --h->pos; }
      else if (d->EventKey == ImGuiKey_DownArrow && h->pos != -1) { if (++h->pos >= static_cast<int>(h->items.size())) h->pos = -1; }
      if (before != h->pos) {
        const std::string s = h->pos >= 0 ? h->items[static_cast<size_t>(h->pos)] : std::string();
        d->DeleteChars(0, d->BufTextLen); d->InsertChars(0, s.c_str());
      }
      return 0;
    }
  };
  static char cmdBuf[256] = "";
  static CmdHistory hist;
  const float runW = ImGui::CalcTextSize(T("Run")).x + ImGui::GetStyle().FramePadding.x * 2.f;
  ImGui::SetNextItemWidth(-(runW + ImGui::GetStyle().ItemSpacing.x));
  const bool enter = ImGui::InputTextWithHint("##consolecmd", "test ECS: 10000   (help)", cmdBuf, sizeof cmdBuf,
                                              ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory, CmdHistory::callback, &hist);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("Type a command and press Enter. Up/Down: previous commands. Try 'help'."));
  if (enter) ImGui::SetKeyboardFocusHere(-1); // mantem o foco para digitar o proximo
  ImGui::SameLine();
  if ((ImGui::Button(T("Run")) || enter) && cmdBuf[0]) {
    const std::string cmd = cmdBuf;
    if (hist.items.empty() || hist.items.back() != cmd) hist.items.push_back(cmd);
    if (hist.items.size() > 100) hist.items.erase(hist.items.begin());
    hist.pos = -1;
    editor::runConsoleCommand(ed, cmd);
    cmdBuf[0] = 0;
  }
  ImGui::End();
}

void drawProfiler(Editor& ed) {
  if (!ImGui::Begin(W("Profiler", "prof").c_str(), &ed.showProfiler)) { ImGui::End(); return; }
  editor::helpIcon("profiler");
  using editor::Zone;
  editor::FrameProfiler& p = ed.prof;
  const auto st = p.stats();
  ImGui::Text("%s", F("%.0f FPS   avg %.2f ms   min %.2f   max %.2f", static_cast<double>(st.fps), static_cast<double>(st.avg), static_cast<double>(st.min), static_cast<double>(st.max)).c_str());
  ImGui::Text("%s", F("1%% low: %.0f FPS (p99 %.2f ms)   jitter: %.2f ms", static_cast<double>(st.low1), static_cast<double>(st.p99), static_cast<double>(st.jitter)).c_str());
  ImGui::Checkbox(T("Pause graph"), &p.paused);
  static float buf[editor::FrameProfiler::kHistory];
  p.ordered(p.frameMs, buf);
  ImGui::PlotLines("##frames", buf, editor::FrameProfiler::kHistory, 0, F("frame time (ms), last %d frames", editor::FrameProfiler::kHistory).c_str(), 0.f, std::max(16.7f, st.max * 1.1f), ImVec2(-1, 70));
  if (ImGui::CollapsingHeader(T("Time per zone (average ms)"), ImGuiTreeNodeFlags_DefaultOpen)) {
    float work = 0.f;
    if (ImGui::BeginTable("zones", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
      for (int z = 0; z < static_cast<int>(Zone::Count); ++z) {
        const float a = p.zoneAvg(static_cast<Zone>(z)); work += a;
        ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted(T(editor::zoneName(static_cast<Zone>(z))));
        ImGui::TableNextColumn(); ImGui::Text("%.2f", static_cast<double>(a));
        ImGui::TableNextColumn(); ImGui::ProgressBar(st.avg > 0 ? std::min(1.f, a / st.avg) : 0.f, ImVec2(-1, 0), "");
      }
      ImGui::EndTable();
    }
    ImGui::TextDisabled("%s", F("measured work: %.2f ms of %.2f ms (the rest is waiting: VSync / frame limit)", static_cast<double>(work), static_cast<double>(st.avg)).c_str());
  }
  if (ImGui::CollapsingHeader(T("Rendering"), ImGuiTreeNodeFlags_DefaultOpen)) {
    const auto& c = p.last;
    ImGui::Text("%s", F("API: %s", ed.apiStatus.c_str()).c_str());
    ImGui::Text("%s", F("views: %u   pixels: %.2f M   objects drawn: %u", c.views, static_cast<double>(c.pixels) / 1e6, c.draws).c_str());
    ImGui::Text("%s", F("triangles: %llu   shadow casters: %u   shadow triangles (CPU): %llu   shadow map reused: %u   culled: %u", c.tris, c.shadowCasters, c.shadowTris, c.shadowReused, c.culled).c_str());
    ImGui::Text("%s", F("shadows: %s   map %d px   post-processing: %s", ed.sceneInfo.render.shadow.enabled ? "on" : "off", ed.sceneInfo.render.shadow.resolution, ed.sceneInfo.render.post.active() ? "on" : "off").c_str());
    if (ed.gpuDev) { const auto g = ed.gpuDev->frameStats(); ImGui::Text("%s", F("GPU: %u draws, %u triangles, %u uploads, %u pipeline switches", g.draws, g.triangles, g.bufferUploads, g.pipelineSwitches).c_str()); }
  }
  if (ImGui::CollapsingHeader(T("System: CPU, RAM, VRAM and cache"), ImGuiTreeNodeFlags_DefaultOpen)) {
    const editor::SysSnapshot& sy = p.sys.get();
    if (!sy.cpuName.empty()) ImGui::TextDisabled("%s", sy.cpuName.c_str());
    if (sy.cpuOk) {
      ImGui::Text("CPU total: %.0f%%   cores in use: %d of %d (in use = above 25%%)", static_cast<double>(sy.cpuTotal), sy.coresBusy, sy.coresOnline);
      ImGui::ProgressBar(sy.cpuTotal / 100.f, ImVec2(-1, 0));
      ImGui::Text("editor (this process): %.1f%% of all cores", static_cast<double>(sy.editorCpu));
      if (!sy.core.empty() && ImGui::TreeNode("per core")) {
        for (size_t i = 0; i < sy.core.size(); ++i) {
          ImGui::Text("core %zu", i); ImGui::SameLine(90);
          ImGui::SetNextItemWidth(-1);
          ImGui::ProgressBar(sy.core[i] / 100.f, ImVec2(-1, 0), F("%.0f%%", static_cast<double>(sy.core[i])).c_str());
        }
        ImGui::TreePop();
      }
    } else {
      ImGui::TextDisabled("%s", T("CPU: measuring..."));
    }
    if (sy.ramOk) ImGui::Text("%s", F("RAM (system): %.0f of %.0f MB in use   editor: %.0f MB (peak %.0f)", sy.ramUsedMB, sy.ramTotalMB, p.memoryMB, p.peakMemoryMB).c_str());
    else ImGui::TextDisabled("%s", T("RAM (system): not available on this platform"));
    if (sy.vramOk) ImGui::Text("%s", F("VRAM (this process, as the driver reports): %.0f of %.0f MB budget", sy.vramUsedMB, sy.vramBudgetMB).c_str());
    else ImGui::TextDisabled("%s", (std::string(T("VRAM")) + ": " + sy.vramNote).c_str());
    if (ImGui::TreeNode(T("Cache sizes"))) {
      for (const auto& c : sy.caches) ImGui::Text("%s", F("L%d %s: %llu KB, line %u bytes", c.level, c.type.c_str(), static_cast<unsigned long long>(c.sizeKB), c.lineBytes).c_str());
      if (sy.caches.empty()) ImGui::TextDisabled("%s", sy.cacheNote.c_str());
      ImGui::TextDisabled("%s", T("Hit rate is not measured: it needs hardware counters (perf / VTune)."));
      ImGui::TreePop();
    }
  }
  if (ImGui::CollapsingHeader(T("Scene and memory"), ImGuiTreeNodeFlags_DefaultOpen)) {
    const auto s = ed.scene.stats();
    ImGui::Text("%s", F("live entities: %zu   DeleteAndReuse pool: dormant=%zu reused=%zu freed=%zu", ed.scene.world().liveCount(), ed.scene.dormantCount(), s.reused, s.freed).c_str());
    ImGui::Text("%s", F("loaded models: %zu   sounds: %zu   tags: %zu", ed.lib.meshCount(), ed.lib.soundCount(), ed.scene.tags().size()).c_str());
    ImGui::Text("%s", p.memoryMB > 0 ? F("process memory: %.0f MB (peak %.0f MB)", p.memoryMB, p.peakMemoryMB).c_str() : T("process memory: not available on this platform"));
    ImGui::Text("%s", F("errors: %zu  warnings: %zu", tool::debug().count(tool::Severity::Error), tool::debug().count(tool::Severity::Warning)).c_str());
  }
  ImGui::End();
}

// --- Janelas de visualizacao (Scene e Game) -------------------------------------------------------------------
// Grid no plano Y=0 (linhas de 1 m, destaque a cada 5 m) + eixos X (vermelho), Y (verde), Z (azul).
void drawGrid(ImDrawList* dl, const ViewXf& v) {
  // Grid infinito: as linhas acompanham a camera (o passo cresce com a altura) e a vista corta o excesso.
  // Assim o grid nunca termina, nao importa quao longe a camera esteja do centro.
  const float s = std::max(1.f, std::pow(10.f, std::floor(std::log10(std::max(1.f, std::abs(v.eye.y)) / 10.f))));
  constexpr int N = 60;
  const float cx = std::round(v.eye.x / s) * s, cz = std::round(v.eye.z / s) * s, reach = N * s;
  for (int i = -N; i <= N; ++i) {
    const float off = static_cast<float>(i) * s;
    const bool major = i % 5 == 0;
    const float d = std::abs(off) / reach, fade = std::clamp(1.f - d * d, 0.f, 1.f);
    const ImU32 col = IM_COL32(200, 200, 215, static_cast<int>((major ? 70 : 34) * fade));
    line3(dl, v, {cx + off, 0, cz - reach}, {cx + off, 0, cz + reach}, col);
    line3(dl, v, {cx - reach, 0, cz + off}, {cx + reach, 0, cz + off}, col);
  }
  const float big = 1.e4f;
  line3(dl, v, {-big, 0, 0}, {big, 0, 0}, IM_COL32(230, 70, 70, 190), 1.6f);
  line3(dl, v, {0, 0, -big}, {0, 0, big}, IM_COL32(70, 110, 240, 190), 1.6f);
  line3(dl, v, {0, 0, 0}, {0, 6, 0}, IM_COL32(80, 220, 90, 220), 1.6f);
  auto label = [&](tool::Vec3 p, const char* t, ImU32 c) { ImVec2 s2; if (v.point(p, s2)) dl->AddText(s2, c, t); };
  label({6.4f, 0, 0}, "X", IM_COL32(230, 90, 90, 255)); label({0, 6.4f, 0}, "Y", IM_COL32(100, 230, 110, 255)); label({0, 0, 6.4f}, "Z", IM_COL32(100, 140, 255, 255));
}

// Visualizacao de culling (Scene > Culled): em vermelho, os objetos que a camera de jogo principal NAO desenha
// (fora do campo de visao ou alem da distancia de desenho). Mostra o que fica invisivel durante o Play.
void drawCulledOverlay(Editor& ed, ImDrawList* dl, const ViewXf& xf) {
  const auto cams = ed.principalCameras();
  if (cams.empty()) return;
  const tool::Camera gc = ed.cameraFor(cams[0]);
  const auto planes = Editor::framePlanes(gc, std::max(1, static_cast<int>(xf.size.x)), std::max(1, static_cast<int>(xf.size.y)));
  tool::World& w = ed.scene.world();
  w.each<ed_::MeshRenderer>([&](tool::Entity e, ed_::MeshRenderer& mr) {
    if (w.has<ed_::Inactive>(e)) return;
    const tool::Mesh* m = ed.meshOf(mr); if (!m) return;
    const Editor::Bounds bd = ed.meshBounds(*m); const tool::Mat4 wm = ed.worldMat(e);
    const float sc = std::max({std::hypot(wm.m[0][0], wm.m[0][1], wm.m[0][2]), std::hypot(wm.m[1][0], wm.m[1][1], wm.m[1][2]), std::hypot(wm.m[2][0], wm.m[2][1], wm.m[2][2])});
    const tool::Vec3 c = wm.transformPoint(bd.center); const float r = bd.radius * sc;
    bool culled = false;
    for (const tool::Vec4& p : planes) if (p.x * c.x + p.y * c.y + p.z * c.z + p.w < -r) { culled = true; break; }
    if (!culled) return;
    const ImU32 red = IM_COL32(255, 70, 70, 230);
    const tool::Vec3 lo{c.x - r, c.y - r, c.z - r}, hi{c.x + r, c.y + r, c.z + r};
    const tool::Vec3 k[8] = {{lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z}, {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}};
    const int ed4[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (const auto& pr : ed4) line3(dl, xf, k[pr[0]], k[pr[1]], red, 1.f);
  });
}

void circleXZ(ImDrawList* dl, const ViewXf& v, tool::Vec3 c, float r, ImU32 col) {
  constexpr int S = 24;
  for (int i = 0; i < S; ++i) {
    const float a0 = 6.2831853f * static_cast<float>(i) / S, a1 = 6.2831853f * static_cast<float>(i + 1) / S;
    line3(dl, v, {c.x + r * std::cos(a0), c.y, c.z + r * std::sin(a0)}, {c.x + r * std::cos(a1), c.y, c.z + r * std::sin(a1)}, col);
  }
}

// Icones 3D simples dos objetos sem malha: camera (piramide), luzes (cruz/seta), fonte de som (circulo).
void drawGizmos(Editor& ed, ImDrawList* dl, const ViewXf& v) {
  tool::World& w = ed.scene.world();
  const tool::TagId tp = ed.scene.tag("Principal");
  auto colorFor = [&](tool::Entity e, ImU32 base) { return e == ed.selected ? IM_COL32(255, 230, 80, 255) : base; };
  w.each<ed_::CameraComp, ed_::Transform>([&](tool::Entity e, ed_::CameraComp& c, ed_::Transform& t) {
    const tool::Vec3 p{t.pos[0], t.pos[1], t.pos[2]}, f = ed_::forwardOf(t), u = ed_::upOf(t), r = tool::normalize(tool::cross(f, u));
    const float d = 1.1f, hh = std::tan(c.fovDeg * ed_::kDeg2Rad * 0.5f) * d, hw = hh * 16.f / 9.f;
    const tool::Vec3 ctr = p + f * d, corner[4] = {ctr + r * hw + u * hh, ctr - r * hw + u * hh, ctr - r * hw - u * hh, ctr + r * hw - u * hh};
    const ImU32 col = colorFor(e, w.hasTag(e, tp) ? IM_COL32(255, 150, 40, 255) : IM_COL32(170, 170, 180, 255));
    for (int i = 0; i < 4; ++i) { line3(dl, v, p, corner[i], col); line3(dl, v, corner[i], corner[(i + 1) % 4], col); }
    line3(dl, v, ctr + u * hh, ctr + u * (hh * 1.4f), col);
  });
  w.each<ed_::GlobalLight, ed_::Transform>([&](tool::Entity e, ed_::GlobalLight& g, ed_::Transform& t) {
    const tool::Vec3 p{t.pos[0], t.pos[1], t.pos[2]}; const tool::Vec3 d = ed_::sunDirection(g.azimuth, g.elevation);
    const ImU32 col = colorFor(e, IM_COL32(255, 220, 90, 255));
    for (tool::Vec3 a : {tool::Vec3{.4f, 0, 0}, tool::Vec3{0, .4f, 0}, tool::Vec3{0, 0, .4f}}) line3(dl, v, p - a, p + a, col);
    line3(dl, v, p, p + d * 2.5f, col, 2.f);
    const tool::Vec3 tip = p + d * 2.5f, s = tool::normalize(tool::cross(d, {0, 1, 0}));
    line3(dl, v, tip, tip - d * 0.4f + s * 0.2f, col); line3(dl, v, tip, tip - d * 0.4f - s * 0.2f, col);
  });
  w.each<ed_::LightComp, ed_::Transform>([&](tool::Entity e, ed_::LightComp& lc, ed_::Transform& t) {
    const tool::Vec3 p{t.pos[0], t.pos[1], t.pos[2]}; const ImU32 col = colorFor(e, IM_COL32(255, 190, 120, 255));
    for (tool::Vec3 a : {tool::Vec3{.25f, 0, 0}, tool::Vec3{0, .25f, 0}, tool::Vec3{0, 0, .25f}}) line3(dl, v, p - a, p + a, col);
    if (lc.light.type != tool::LightType::Point) line3(dl, v, p, p + tool::normalize(lc.light.direction) * 1.5f, col, 1.5f);
    if (e == ed.selected && lc.light.type == tool::LightType::Point) circleXZ(dl, v, p, lc.light.range, IM_COL32(255, 190, 120, 90));
  });
  w.each<ed_::AudioSource, ed_::Transform>([&](tool::Entity e, ed_::AudioSource& a, ed_::Transform& t) {
    const tool::Vec3 p{t.pos[0], t.pos[1], t.pos[2]}; const ImU32 col = colorFor(e, IM_COL32(110, 210, 140, 255));
    circleXZ(dl, v, p, 0.3f, col);
    if (e == ed.selected && a.spatial) { circleXZ(dl, v, p, a.minDistance, IM_COL32(110, 210, 140, 120)); circleXZ(dl, v, p, a.maxDistance, IM_COL32(110, 210, 140, 60)); }
  });
}

// Botao Play/Stop (igual nas janelas Scene e Game).
void playButton(Editor& ed) {
  if (ImGui::Button(ed.playing ? T("Stop") : T("Play"))) { if (ed.playing) ed.stopPlay(); else ed.startPlay(); }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("Run the game in the editor. Stop restores the scene."));
  if (ed.playing) {
    ImGui::SameLine(); if (ImGui::Button(ed.paused ? T("Resume") : T("Pause"))) ed.paused = !ed.paused;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("Freeze physics, tools and narrative (Step advances 1/60 s)"));
    if (ed.paused) { ImGui::SameLine(); if (ImGui::Button(T("Step"))) ed.stepOnce = true; }
  }
}

void drawSceneView(Editor& ed, SDL_Renderer* sdl) {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  if (!ImGui::Begin(W("Scene", "view").c_str(), &ed.showScene)) { ImGui::End(); ImGui::PopStyleVar(); return; }
  editor::helpIcon("scene");
  ImGui::PopStyleVar();
  ImGui::SetCursorPos({ImGui::GetCursorPosX() + 6.f, ImGui::GetCursorPosY() + 4.f});
  playButton(ed);
  ImGui::SameLine(); ImGui::Checkbox(T("Grid"), &ed.showGridScene);
  ImGui::SameLine(); ImGui::Checkbox(T("Gizmos"), &ed.showGizmos);
  ImGui::SameLine(); ImGui::Checkbox(T("Culled"), &ed.showCulled);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("Shows in red the objects the game camera does not draw (outside its view or beyond the draw distance)."));
  {
    ImGui::SameLine(); ImGui::Checkbox(T("Post-fx"), &ed.showPostInScene);
    ImGui::SameLine(); ImGui::TextDisabled("%s", ed.freeFly ? T("FREE NAVIGATION: WASD move, Q/E down/up, Shift fast, wheel speed, Alt/Esc exit")
                                                          : T("right: orbit | middle: pan | wheel: zoom | Alt: free navigation | double-click in Hierarchy: focus"));
  }
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  const int w = std::clamp(static_cast<int>(avail.x), 64, 1920), h = std::clamp(static_cast<int>(avail.y), 64, 1080);
  const int rw = std::max(64, static_cast<int>(static_cast<float>(w) * ed.renderScale)), rh = std::max(36, static_cast<int>(static_cast<float>(h) * ed.renderScale));
  const tool::Camera cam = ed.sceneCamera();
  // Com upscaling/AA por software, a textura tem o tamanho da janela (w x h) e a escala do SDL e 1:1.
  const bool soft = ed.softScaling();
  const int tw = soft ? w : rw, th = soft ? h : rh;
  const bool reuseScene = ed.viewsIdle() && ed.sceneTex.tex && ed.sceneTex.w == tw && ed.sceneTex.h == th;   // ocioso: a imagem anterior continua valendo
  const tool::u8* pixels = nullptr;
  if (!reuseScene) {
    const tool::u8* raw = ed.renderView(cam, rw, rh, true, editor::Zone::SceneRender, ed.showPostInScene ? &ed.sceneInfo.render.post : nullptr);
    pixels = soft ? ed.finishView(ed.sceneUp, raw, rw, rh, w, h) : raw;
    ed.sceneTex.ensure(sdl, tw, th);
  }
  if (ed.sceneTex.tex) {
    SDL_SetTextureScaleMode(ed.sceneTex.tex, soft ? SDL_SCALEMODE_NEAREST : (ed.pixelFilter ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR));
    if (!reuseScene) SDL_UpdateTexture(ed.sceneTex.tex, nullptr, pixels, tw * 4);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Image(reinterpret_cast<ImTextureID>(ed.sceneTex.tex), ImVec2(static_cast<float>(w), static_cast<float>(h)));
    editor::drawParticleOverlay(ed, cam, origin, ImVec2(static_cast<float>(w), static_cast<float>(h)), ed.selected); // particulas + trajetoria do selecionado
    const ViewXf xf = ViewXf::make(cam, origin, {static_cast<float>(w), static_cast<float>(h)});
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(origin, {origin.x + static_cast<float>(w), origin.y + static_cast<float>(h)}, true);
    if (ed.showGridScene) drawGrid(dl, xf);
    if (ed.showCulled) drawCulledOverlay(ed, dl, xf);
    if (ed.showGizmos) { drawGizmos(ed, dl, xf); editor::drawPhysicsShapes(ed, dl, xf); }
    dl->PopClipRect();
    // soltar um modelo/som do painel Assets cria o objeto na cena
    if (ImGui::BeginDragDropTarget()) {
      if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_PATH")) ed_::instantiateAsset(ed, static_cast<const char*>(p->Data));
      ImGui::EndDragDropTarget();
    }
    const bool hovered = ImGui::IsItemHovered();
    ImGuiIO& io = ImGui::GetIO();
    static editor::GizmoState gizmo;
    // Objeto sob o mouse: raio; sem acerto, o centro mais proximo (luzes, cameras, vazios).
    auto pickAt = [&](ImVec2 mp) -> tool::Entity {
      tool::Entity hit = editor::pickByRay(ed, editor::rayFromScreen(cam, origin, {static_cast<float>(w), static_cast<float>(h)}, mp));
      if (!ed.scene.world().alive(hit)) {
        float best = 28.f;
        ed.scene.world().eachEntity([&](tool::Entity e) {
          auto* t = ed.scene.world().get<ed_::Transform>(e); if (!t) return;
          auto* m = ed.scene.world().get<ed_::MeshRenderer>(e);
          if (m && m->shape == 2) return; // o chao nao disputa o clique
          ImVec2 sp; if (!xf.point({t->pos[0], t->pos[1], t->pos[2]}, sp)) return;
          const float d = std::hypot(sp.x - mp.x, sp.y - mp.y); if (d < best) { best = d; hit = e; }
        });
      }
      return hit;
    };
    // Botao direito SEM arrastar = menu de contexto do objeto sob o mouse. Arrastar continua girando a camera.
    static tool::Entity ctxTarget{};
    if (hovered && !ed.freeFly && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) ctxTarget = pickAt(io.MousePos);
    if (hovered && !ed.freeFly && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Right] < 16.f && ed.scene.world().alive(ctxTarget)) {
      ed.selected = ctxTarget;
      ImGui::OpenPopup("scenectx");
    }
    if (ImGui::BeginPopup("scenectx")) {
      auto& ww = ed.scene.world();
      const tool::Entity s = ed.selected; const bool okSel = ww.alive(s);
      if (okSel) {
        const bool locked = ww.has<ed_::Locked>(s), hidden = ww.has<ed_::Inactive>(s);
        const ed_::Name* nm = ww.get<ed_::Name>(s);
        ImGui::TextDisabled("%s", nm ? nm->text : T("(unnamed)"));
        ImGui::Separator();
        if (ImGui::MenuItem(T("Edit in Inspector"))) ed.showInspector = true;
        if (ImGui::MenuItem(T("Focus"))) ed.focusOn(s);
        if (ImGui::MenuItem(T("Duplicate"))) ed.selected = ed.duplicate(s);
        if (ImGui::MenuItem(T("Delete"), "Del", false, !locked)) ed.destroy(s);
        ImGui::Separator();
        if (ImGui::MenuItem(T("Snap to contact"), nullptr, false, !locked) && !snapToContact(ed, s)) ed.statusLine = T("Nothing to snap to here");
        ImGui::Separator();
        if (ImGui::MenuItem(hidden ? T("Show") : T("Hide"))) { if (hidden) ww.remove<ed_::Inactive>(s); else ww.add<ed_::Inactive>(s, ed_::Inactive{}); }
        if (ImGui::MenuItem(locked ? T("Unlock") : T("Lock"))) { if (locked) ww.remove<ed_::Locked>(s); else ww.add<ed_::Locked>(s, ed_::Locked{}); }
      }
      ImGui::EndPopup();
    }
    // Barra flutuante da Scene (estilo Unity): ferramentas, ima e colar. Janela propria, por cima da imagem.
    // Fica depois de calcular `hovered`, porque mudar de janela troca o "ultimo item".
    {
      ImGui::SetNextWindowPos(ImVec2(origin.x + 10.f, origin.y + static_cast<float>(h) - 10.f), ImGuiCond_Always, ImVec2(0.f, 1.f));
      ImGui::SetNextWindowBgAlpha(0.88f);
      const ImGuiWindowFlags flt = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking;
      if (ImGui::Begin("##scenefloat", nullptr, flt)) {
        ImGui::RadioButton(T("Move (W)"), &ed.gizmoMode, 0); ImGui::SameLine(); ImGui::RadioButton(T("Rotate (E)"), &ed.gizmoMode, 1); ImGui::SameLine(); ImGui::RadioButton(T("Scale (R)"), &ed.gizmoMode, 2);
        ImGui::SameLine(0, 14.f); ImGui::Checkbox(T("Magnet"), &ed.snapOn);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("Magnet: the gizmo snaps to a grid (step on the right). Same as holding Ctrl while dragging."));
        ImGui::SameLine(); ImGui::SetNextItemWidth(110.f); ImGui::SliderFloat("##snapstep", &ed.snapStep, 0.05f, 2.f, "%.2f m");
        ImGui::SameLine(0, 14.f);
        const bool okSel = ed.scene.world().alive(ed.selected) && !ed.scene.world().has<ed_::Locked>(ed.selected);
        ImGui::BeginDisabled(!okSel);
        if (ImGui::Button(T("Snap to contact"))) { if (!snapToContact(ed, ed.selected)) ed.statusLine = T("Nothing to snap to here"); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("Moves the selected object until a face touches the nearest object: no overlap, no gap."));
        ImGui::EndDisabled();
      }
      ImGui::End();
    }
    // soltar um modelo/som ja tratado acima. Teclas: Alt = navegacao livre; W/E/R = ferramenta do gizmo.
    const bool typing = io.WantTextInput;
    if (!typing && (hovered || ImGui::IsWindowFocused()) && ImGui::IsKeyPressed(ImGuiKey_LeftAlt, false)) { ed.toggleFreeFly(); ed.flyChanged = true; }
    if (ed.freeFly) {
      if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) { ed.toggleFreeFly(); ed.flyChanged = true; }
      else {
        const float dt = std::min(0.1f, io.DeltaTime);
        ed.flyLook(ed.mouseDX, ed.mouseDY); ed.mouseDX = ed.mouseDY = 0.f;
        auto k = [&](ImGuiKey key) { return ImGui::IsKeyDown(key) ? 1.f : 0.f; };
        ed.flyMove(dt, k(ImGuiKey_W) - k(ImGuiKey_S), k(ImGuiKey_D) - k(ImGuiKey_A), k(ImGuiKey_E) - k(ImGuiKey_Q), io.KeyShift);
        if (io.MouseWheel != 0.f) ed.flySpeed = std::clamp(ed.flySpeed * (1.f + io.MouseWheel * 0.15f), 0.5f, 80.f);
      }
    } else {
      if (!typing && (hovered || ImGui::IsWindowFocused())) {
        if (ImGui::IsKeyPressed(ImGuiKey_W, false)) ed.gizmoMode = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_E, false)) ed.gizmoMode = 1;
        if (ImGui::IsKeyPressed(ImGuiKey_R, false)) ed.gizmoMode = 2;
      }
      dl->PushClipRect(origin, {origin.x + static_cast<float>(w), origin.y + static_cast<float>(h)}, true);
      const bool consumed = ed.showGizmos && !ed.playing && editor::transformGizmo(ed, dl, xf, gizmo, hovered);
      dl->PopClipRect();
      if (hovered && !gizmo.dragging()) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) { ed.yaw -= io.MouseDelta.x * 0.008f; ed.pitch = std::clamp(ed.pitch + io.MouseDelta.y * 0.008f, -1.5f, 1.5f); }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
          const tool::Vec3 f = tool::normalize(cam.target - cam.eye), r = tool::normalize(tool::cross(f, {0, 1, 0})), u = tool::cross(r, f);
          const float kk = ed.dist * 0.0016f;
          ed.target = ed.target - r * (io.MouseDelta.x * kk) + u * (io.MouseDelta.y * kk);
        }
        ed.dist = std::clamp(ed.dist - io.MouseWheel * ed.dist * 0.1f, 1.5f, 200.f);
        if (!consumed && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { // selecao por raio (mesmo pick do menu de contexto)
          const tool::Entity hit = pickAt(io.MousePos);
          if (ed.scene.world().alive(hit)) ed.selected = hit;
        }
      }
    }
  }
  ImGui::End();
}

void drawGameView(Editor& ed, SDL_Renderer* sdl, bool player = false) {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  if (player) { // jogo exportado (--player): janela cheia, sem barra de ferramentas nem docas
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos); ImGui::SetNextWindowSize(vp->Size);
    if (!ImGui::Begin("##player", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoDocking)) { ImGui::End(); ImGui::PopStyleVar(); return; }
  } else {
    if (!ImGui::Begin(W("Game", "game").c_str(), &ed.showGame)) { ImGui::End(); ImGui::PopStyleVar(); return; }
    editor::helpIcon("start");
  }
  ImGui::PopStyleVar();
  if (!player) { ImGui::SetCursorPos({ImGui::GetCursorPosX() + 6.f, ImGui::GetCursorPosY() + 4.f}); playButton(ed); }
  const auto cams = ed.principalCameras();
  if (!player) { ImGui::SameLine(); ImGui::TextDisabled("%s", F("Principal cameras: %zu%s", cams.size(), cams.size() >= 2 ? (ed.sceneInfo.splitVertical ? " (vertical split)" : " (horizontal split)") : "").c_str()); }
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  if (cams.empty()) {
    ImGui::SetCursorPos({12, ImGui::GetCursorPosY() + 12});
    ImGui::TextWrapped("%s", T("No camera has the 'Principal' tag. Create a camera (Hierarchy > + Create > Main Camera) or tick 'Principal' on a camera in the Inspector."));
    ImGui::End(); return;
  }
  const int n = static_cast<int>(std::min<size_t>(2, cams.size()));
  const bool vertical = ed.sceneInfo.splitVertical;
  const float gap = 2.f;
  float vw = avail.x, vh = avail.y;
  if (n == 2) { if (vertical) vw = (avail.x - gap) * 0.5f; else vh = (avail.y - gap) * 0.5f; }
  const int w = std::clamp(static_cast<int>(vw), 32, 1920), h = std::clamp(static_cast<int>(vh), 32, 1080);
  int rw = std::max(32, static_cast<int>(static_cast<float>(w) * ed.renderScale)), rh = std::max(18, static_cast<int>(static_cast<float>(h) * ed.renderScale));
  // Split "preencher": cada metade usa o tamanho da propria area (a camera corta a vista, sem barras pretas).
  const bool fill = n == 2 && ed.sceneInfo.splitFill;
  const bool fixedRes = !fill && ed.fixedRes(rw, rh); // resolucao fixa: a imagem mantem a proporcao e ganha barras pretas
  for (int i = 0; i < n; ++i) {
    const tool::Entity e = cams[static_cast<size_t>(i)];
    auto* t = ed.scene.world().get<ed_::Transform>(e); auto* c = ed.scene.world().get<ed_::CameraComp>(e);
    if (!t || !c) continue;
    const tool::Camera cam = ed.cameraFor(e);
    const tool::PostFx fx = tool::combinePostFx(ed.sceneInfo.render.post, c->post); // global da cena + local da camera
    // Upscaling/AA por software so no modo normal (com resolucao fixa a imagem ganha barras pretas: fica no caminho antigo)
    const bool soft = ed.softScaling() && !fixedRes;
    const int tw = soft ? w : rw, th = soft ? h : rh;
    const bool reuseGame = ed.viewsIdle() && ed.gameTex[i].tex && ed.gameTex[i].w == tw && ed.gameTex[i].h == th;
    const tool::u8* pixels = nullptr;
    if (!reuseGame) {
      const tool::u8* raw = ed.renderView(cam, rw, rh, false, editor::Zone::GameRender, &fx);
      pixels = soft ? ed.finishView(ed.gameUp[i], raw, rw, rh, w, h) : raw;
      ed.gameTex[i].ensure(sdl, tw, th);
    }
    if (!ed.gameTex[i].tex) continue;
    if (!reuseGame) SDL_UpdateTexture(ed.gameTex[i].tex, nullptr, pixels, tw * 4);
    if (i == 1) { if (vertical) ImGui::SameLine(0, gap); else ImGui::Dummy({0, gap}); }
    SDL_SetTextureScaleMode(ed.gameTex[i].tex, soft ? SDL_SCALEMODE_NEAREST : (ed.pixelFilter ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR));
    const ImVec2 cell = ImGui::GetCursorScreenPos();
    ImVec2 origin = cell, size{static_cast<float>(w), static_cast<float>(h)};
    ImGui::Dummy(size);
    if (fixedRes) { // encaixa a imagem na area mantendo a proporcao
      const float k = std::min(size.x / static_cast<float>(rw), size.y / static_cast<float>(rh));
      const ImVec2 fit{static_cast<float>(rw) * k, static_cast<float>(rh) * k};
      ImGui::GetWindowDrawList()->AddRectFilled(cell, {cell.x + size.x, cell.y + size.y}, IM_COL32(0, 0, 0, 255));
      origin = {cell.x + (size.x - fit.x) * 0.5f, cell.y + (size.y - fit.y) * 0.5f}; size = fit;
    }
    ImGui::GetWindowDrawList()->AddImage(reinterpret_cast<ImTextureID>(ed.gameTex[i].tex), origin, {origin.x + size.x, origin.y + size.y});
    if (ed.showGridGame) { // o grid so aparece no jogo se ligado em Settings > Game
      const ViewXf xf = ViewXf::make(cam, origin, size);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
      drawGrid(dl, xf);
      dl->PopClipRect();
    }
        editor::drawParticleOverlay(ed, cam, origin, size, tool::Entity{});
        editor::drawUiOverlay(ed, e, origin, size, ed.playing);
  }
  ImGui::End();
}


void drawGraphicsTab(Editor& ed) {
  ImGui::Text("%s", F("Active API: %s", ed.apiStatus.c_str()).c_str());
  ImGui::SeparatorText(T("Graphics API"));
  int pick = ed.apiChoice;
  ImGui::RadioButton("Software (CPU)", &pick, 0);
#ifdef TOOL_EDITOR_GPU
  for (auto b : tool::rhi::compiledBackends()) {
    if (b == tool::rhi::Backend::Vulkan) ImGui::RadioButton("Vulkan", &pick, 1);
    else if (b == tool::rhi::Backend::GLES) ImGui::RadioButton("OpenGL", &pick, 2);
    else if (b == tool::rhi::Backend::D3D12) ImGui::RadioButton("Direct3D 12", &pick, 3); // so existe se compilado com TOOL_WITH_D3D12
  }
#else
  ImGui::TextDisabled("%s", T("GPU backends are not available in this build (use the 'beta' preset)."));
#endif
  if (pick != ed.apiChoice) { ed.applyApi(pick); ed.settingsDirty = true; }
  ImGui::SeparatorText(T("Quality"));
  bool c = false;
  ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
  const std::string res = std::string(T("Match window size (x render scale)")) + '\0' + "640 x 360" + '\0' + "1280 x 720 (HD)" + '\0' + "1920 x 1080 (Full HD)" + '\0' + "2560 x 1440 (QHD)" + '\0' + T("Custom") + '\0';
  c |= ImGui::Combo(T("Game resolution"), &ed.resMode, res.c_str());
  if (ed.resMode == 5) {
    c |= ImGui::InputInt(T("Width"), &ed.customW, 16, 128); c |= ImGui::InputInt(T("Height"), &ed.customH, 16, 128);
    ed.customW = std::clamp(ed.customW, 160, 3840); ed.customH = std::clamp(ed.customH, 90, 2160);
  }
  ImGui::BeginDisabled(ed.resMode != 0);
  if (ImGui::SliderFloat(T("Render scale"), &ed.renderScale, 0.25f, 2.f, "%.2f")) { ed.renderScaleSet = true; c = true; }
  ImGui::EndDisabled();
  ImGui::TextDisabled("%s", T("Below 1.0 renders fewer pixels (faster); above 1.0 renders more pixels (sharper, slower)."));
  { // upscaling e AA por software (CPU): valem para a Scene e o Game
    static const char* upItems[] = {"Off (SDL linear)", "Bilinear", "Bicubic (Catmull-Rom)", "Lanczos2"};
    static const char* aaItems[] = {"Off", "FXAA"};
    if (ImGui::Combo(T("Upscaling (software)"), &ed.upscaleMode, upItems, 4)) c = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("Scales the rendered image to the window size on the CPU. Lanczos2 is the sharpest; Bilinear the cheapest."));
    if (ImGui::Combo(T("Anti-aliasing (software)"), &ed.aaMode, aaItems, 2)) c = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("FXAA smooths stair-step edges on the final image (CPU, cheap)."));
  }
  ImGui::SameLine(); if (ImGui::SmallButton(T("Reset"))) { ed.renderScale = ed.defaultScale(); ed.renderScaleSet = false; c = true; }
  c |= ImGui::Checkbox(T("Sharp pixels when enlarging (no smoothing)"), &ed.pixelFilter);
  const std::string fps = std::string("VSync") + '\0' + "30 FPS" + '\0' + "60 FPS" + '\0' + "120 FPS" + '\0' + T("Unlimited") + '\0';
  if (ImGui::Combo(T("Frame rate limit"), &ed.fpsMode, fps.c_str())) { ed.fpsDirty = true; c = true; }
  { int fw = 0, fh = 0; if (ed.fixedRes(fw, fh)) ImGui::TextDisabled("%s", F("Game renders at %d x %d.", fw, fh).c_str()); }
  ImGui::PopItemWidth();
  if (c) ed.settingsDirty = true;
  ImGui::SeparatorText(T("GPU details"));
  { // ajustes rapidos de sombra tambem aqui (mesmos valores de Settings > Lighting, salvos na cena)
    auto& sh = ed.sceneInfo.render.shadow; bool sc = false;
    sc |= ImGui::Checkbox(T("Enable shadows"), &sh.enabled);
    const std::string q = std::string(T("Low (512)")) + '\0' + T("Medium (1024)") + '\0' + T("High (2048)") + '\0' + T("Ultra (4096)") + '\0';
    int qi = sh.resolution <= 512 ? 0 : sh.resolution <= 1024 ? 1 : sh.resolution <= 2048 ? 2 : 3;
    if (ImGui::Combo(T("Shadow quality"), &qi, q.c_str())) { sh.resolution = 512 << qi; sc = true; }
    if (sc) ed.settingsDirty = true;
  }
  ImGui::Text("%s", F("Adapter: %s", ed.gpuDev ? ed.gpuDev->deviceName().c_str() : "(software renderer)").c_str());
  ImGui::Text("%s", F("Pixels per frame: %.2f M (all views)", static_cast<double>(ed.prof.last.pixels) / 1e6).c_str());
  ImGui::Text("%s", F("Consecutive GPU error frames: %d / 5 (then the editor falls back to the CPU renderer)", ed.gpuBadFrames).c_str());
  if (ed.gpuDev) { const auto s = ed.gpuDev->frameStats(); ImGui::Text("%s", F("GPU: %u draws, %u triangles, %u uploads/frame", s.draws, s.triangles, s.bufferUploads).c_str()); }
  ImGui::TextDisabled("%s", T("Tip: lower render scale and shadow quality first when you need more FPS."));
}

// --- Configuracoes > Jogo ----------------------------------------------------------------------------------------
void drawGameTab(Editor& ed) {
  ImGui::SeparatorText(T("Grid and gizmos"));
  bool c = false;
  c |= ImGui::Checkbox(T("Show grid in the Scene window"), &ed.showGridScene);
  c |= ImGui::Checkbox(T("Show grid in the Game window"), &ed.showGridGame);
  c |= ImGui::Checkbox(T("Show gizmos (cameras, lights, sounds)"), &ed.showGizmos);
  ImGui::TextDisabled("%s", T("The grid is an editor aid: it only appears in the Game window if you turn it on here."));
  ImGui::SeparatorText(T("Split screen"));
  const std::string modes = std::string(T("Vertical split (side by side)")) + '\0' + T("Horizontal split (top / bottom)") + '\0';
  int sv = ed.sceneInfo.splitVertical ? 0 : 1;
  if (ImGui::Combo(T("Split screen"), &sv, modes.c_str())) ed.sceneInfo.splitVertical = sv == 0;
  ImGui::TextDisabled("%s", T("Used when two cameras have the 'Principal' tag. Saved with the scene."));
  if (ImGui::Checkbox(T("Fill each half (no black bars)"), &ed.sceneInfo.splitFill)) ed.settingsDirty = true;
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("Each camera renders into its whole half, so a 16:9 resolution no longer leaves black bars. The camera keeps its vertical field of view; the sides are cropped."));
  ImGui::TextDisabled("%s", T("Off: the whole camera image fits the half, with black bars when the proportions differ."));

  ImGui::SeparatorText(T("Skybox"));
  if (ImGui::Checkbox(T("Skybox (6 images)"), &ed.sceneInfo.sky.enabled)) ed.settingsDirty = true;
  if (ed.sceneInfo.sky.enabled) {
    // Um slot por lado do cubo: uma imagem da pasta Assets (PNG, JPG, BMP, TGA). Slot vazio usa a cor solida.
    for (int i = 0; i < 6; ++i) {
      std::string before = ed.sceneInfo.sky.face[i];
      if (assetField(ed, T(editor::kSkyFaceNames[static_cast<size_t>(i)]), ed.sceneInfo.sky.face[i], sizeof ed.sceneInfo.sky.face[i], ed_::AssetKind::Image)) {
        ed.lib.invalidate(before); ed.lib.invalidate(ed.sceneInfo.sky.face[i]); ed.settingsDirty = true;
      }
    }
    if (ImGui::ColorEdit3(T("Solid color (sides without image)"), ed.sceneInfo.sky.solid)) ed.settingsDirty = true;
    ImGui::TextDisabled("%s", T("Each side shows its image in the direction you look at. Used by the Scene and Game views; 2D cameras have no skybox."));
  }
  ImGui::SeparatorText(T("Audio"));
  ImGui::Text("%s", F("Audio output: %s", ed.audioTried ? (ed.audioOk ? "ready" : "not available") : "starts on Play").c_str());
  if (c) ed.settingsDirty = true;
}

// --- Configuracoes > Aparencia -------------------------------------------------------------------------------
void drawAppearanceTab(Editor& ed) {
  editor::Theme& th = ed.theme;
  bool changed = false;
  ImGui::SeparatorText(T("Theme"));
  const auto& presets = editor::builtinThemes();
  const std::string curLabel = [&] {
    for (auto& p : presets) if (th.current == p.key) return std::string(T(p.key));
    return th.current == "Custom" ? std::string(T("Custom")) : th.current;
  }();
  if (ImGui::BeginCombo(T("UI theme"), curLabel.c_str())) {
    for (size_t i = 0; i < presets.size(); ++i)
      if (ImGui::Selectable(T(presets[i].key), th.current == presets[i].key)) { th.setPreset(i); changed = true; }
    if (!th.files.empty()) ImGui::SeparatorText(T("User themes"));
    for (auto& f : th.files)
      if (ImGui::Selectable(f.name.c_str(), th.current == f.name)) { th.load(f.path); changed = true; }
    ImGui::EndCombo();
  }

  ImGui::SeparatorText(T("Base colors"));
  ImGuiColorEditFlags fl = ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs;
  auto base = [&](const char* label, ImVec4& c, bool alphaBar = false) {
    if (ImGui::ColorEdit4(T(label), &c.x, alphaBar ? fl : (fl & ~ImGuiColorEditFlags_AlphaBar) | ImGuiColorEditFlags_NoAlpha)) { changed = true; th.current = "Custom"; th.apply(); }
  };
  base("Background", th.base.bg); base("Surface", th.base.surface); base("Text", th.base.text); base("Accent", th.base.accent); base("Border", th.base.border);
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)); ImGui::TextWrapped("%s", T("Changing a base color recalculates the whole palette; fine tweaks live in 'Advanced colors'.")); ImGui::PopStyleColor();

  ImGui::SeparatorText(T("Shape and size"));
  if (ImGui::SliderFloat(T("Rounding"), &th.base.rounding, 0.f, 12.f, "%.0f")) { changed = true; th.current = "Custom"; const ImVec4 keep[1] = {}; (void)keep; ImGuiStyle& s = ImGui::GetStyle(); s.WindowRounding = s.PopupRounding = s.FrameRounding = s.GrabRounding = s.TabRounding = s.ScrollbarRounding = s.ChildRounding = th.base.rounding; }
  if (ImGui::SliderFloat(T("UI scale"), &th.scale, 0.7f, 2.0f, "%.2f")) { changed = true; th.applyMetrics(); }
  ImGui::SameLine(); if (ImGui::SmallButton(T("100%"))) { th.scale = 1.f; changed = true; th.applyMetrics(); }
  ImGui::SameLine(); if (ImGui::SmallButton(T("125%"))) { th.scale = editor::Theme::kDefaultScale; changed = true; th.applyMetrics(); }

  if (ImGui::CollapsingHeader(T("Advanced colors (all)"))) {
    static char filter[48] = "";
    ImGui::InputText(T("filter"), filter, sizeof filter);
    ImGui::BeginChild("advcolors", ImVec2(0, 260), ImGuiChildFlags_Borders);
    for (int i = 0; i < ImGuiCol_COUNT; ++i) {
      const char* name = ImGui::GetStyleColorName(i);
      if (filter[0] && !ImGui::GetStyle().Colors[i].w && false) continue;
      std::string lower = name, f = filter; std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower); std::transform(f.begin(), f.end(), f.begin(), ::tolower);
      if (!f.empty() && lower.find(f) == std::string::npos) continue;
      ImGui::PushID(i);
      if (ImGui::ColorEdit4("##c", &ImGui::GetStyle().Colors[i].x, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs)) { changed = true; th.current = "Custom"; }
      ImGui::SameLine(); ImGui::TextUnformatted(name);
      ImGui::PopID();
    }
    ImGui::EndChild();
  }

  ImGui::SeparatorText(T("Save theme"));
  ImGui::InputText(T("theme name"), ed.themeName, sizeof ed.themeName);
  if (ImGui::Button(T("Save theme")) && ed.themeName[0]) {
    const std::string path = "assets/themes/" + std::string(ed.themeName) + ".json";
    ed.themeStatus = th.save(path, ed.themeName) ? F("saved to %s", path.c_str()) : std::string(T("failed to save"));
    th.current = ed.themeName; th.scan("assets/themes");
  }
  if (!ed.themeStatus.empty()) { ImGui::SameLine(); ImGui::TextDisabled("%s", ed.themeStatus.c_str()); }
  if (ImGui::Button(T("Reset to default theme"))) { th.scale = 1.f; th.setPreset(0); changed = true; }
  if (ImGui::Button(T("Reset window layout"))) ed.resetLayout = true;
  if (changed) ed.themeDirty = true;
}

// --- Configuracoes > Idioma -------------------------------------------------------------------------------------
void drawLanguageTab(Editor& ed) {
  auto& i18n = editor::I18n::get();
  ImGui::SeparatorText(T("Language packs"));
  std::string chosen;
  for (auto& p : i18n.packs()) {
    const std::string label = p.name + "  [" + p.code + "]";
    if (ImGui::RadioButton(label.c_str(), i18n.code() == p.code)) chosen = p.code;
  }
  if (!chosen.empty() && i18n.select(chosen)) ed.langDirty = true;
  ImGui::Spacing();
  if (i18n.code() != "en") ImGui::TextDisabled("%s", F("%zu of %zu phrases seen so far are translated in this language.", i18n.translatedCount(), i18n.phraseCount()).c_str());
  else ImGui::TextDisabled("%s", T("Base language (English): nothing to translate."));
  ImGui::SeparatorText(T("Create or edit a pack"));
  ImGui::TextWrapped("%s", F("Packs are JSON files in the '%s' folder. Untranslated phrases appear in English.", i18n.dir().c_str()).c_str());
  if (ImGui::Button(T("Reload packs"))) { const std::string cur = i18n.code(); i18n.scan(i18n.dir()); i18n.select(cur); }
  ImGui::SameLine();
  if (ImGui::Button(T("Export template"))) ed.langStatus = i18n.exportTemplate(i18n.dir() + "/modelo.json") ? F("template written to %s/modelo.json", i18n.dir().c_str()) : std::string(T("failed to save"));
  if (!ed.langStatus.empty()) ImGui::TextDisabled("%s", ed.langStatus.c_str());
}

// --- Configuracoes > Lighting (iluminacao e sombras da CENA + pos-processamento global) -------------------------
void drawLightingTab(Editor& ed) {
  auto& r = ed.sceneInfo.render; bool c = false;
  ImGui::TextDisabled("%s", F("These settings belong to the scene '%s' and are saved with it.", ed.sceneInfo.name.c_str()).c_str());
  ImGui::SeparatorText(T("Sun (Global Light)"));
  tool::Entity sun; ed.scene.world().each<ed_::GlobalLight>([&](tool::Entity e, ed_::GlobalLight&) { if (!ed.scene.world().alive(sun)) sun = e; });
  if (ed.scene.world().alive(sun)) {
    auto* g = ed.scene.world().get<ed_::GlobalLight>(sun);
    c |= ImGui::ColorEdit3(T("Light color"), g->color); c |= ImGui::DragFloat(T("Intensity"), &g->intensity, 0.05f, 0.f, 20.f);
    c |= ImGui::SliderFloat(T("Direction (azimuth)"), &g->azimuth, 0.f, 360.f, "%.0f deg"); c |= ImGui::SliderFloat(T("Height (elevation)"), &g->elevation, -10.f, 90.f, "%.0f deg");
    c |= ImGui::ColorEdit3(T("Ambient light"), g->ambient); c |= ImGui::Checkbox(T("Sun casts shadows"), &g->castShadow);
  } else ImGui::TextDisabled("%s", T("No Global Light in this scene (Hierarchy > + Create > Global Light)."));
  c |= ImGui::Checkbox(T("Frustum culling (skip objects outside the camera view)"), &r.frustumCull);
  c |= ImGui::DragFloat(T("Draw distance (0 = each camera's Far)"), &r.drawDistance, 1.f, 0.f, 20000.f, "%.0f");
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("Objects farther than this are not drawn in the Scene and Game views. 0 = use the Far value of each camera. Very large values make distant objects flicker (depth precision)."));
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("Objects outside the camera view are not drawn. Objects that cast shadows are always drawn."));
  ImGui::SeparatorText(T("Real-time shadows"));
  c |= ImGui::Checkbox(T("Enable shadows"), &r.shadow.enabled);
  ImGui::BeginDisabled(!r.shadow.enabled);
  const std::string q = std::string(T("Low (512)")) + '\0' + T("Medium (1024)") + '\0' + T("High (2048)") + '\0' + T("Ultra (4096)") + '\0';
  int qi = r.shadow.resolution <= 512 ? 0 : r.shadow.resolution <= 1024 ? 1 : r.shadow.resolution <= 2048 ? 2 : 3;
  if (ImGui::Combo(T("Shadow quality"), &qi, q.c_str())) { r.shadow.resolution = 512 << qi; c = true; }
  c |= ImGui::SliderFloat(T("Max distance (m)"), &r.shadow.distance, 5.f, 200.f, "%.0f");
  const std::string pcf = std::string(T("Hard edges")) + '\0' + T("Soft 3x3") + '\0' + T("Softer 5x5") + '\0';
  c |= ImGui::Combo(T("Edge softness"), &r.shadow.pcf, pcf.c_str());
  c |= ImGui::SliderFloat(T("Shadow strength"), &r.shadow.strength, 0.f, 1.f, "%.2f");
  c |= ImGui::SliderFloat(T("Bias"), &r.shadow.bias, 0.f, 0.02f, "%.4f"); c |= ImGui::SliderFloat(T("Normal bias (texels)"), &r.shadow.normalBias, 0.f, 4.f, "%.2f");
  ImGui::EndDisabled();
  ImGui::TextDisabled("%s", T("Each object chooses Cast shadow / Receive shadow in the Inspector. Only the sun casts shadows. Lower quality = more FPS."));
  ImGui::SeparatorText(T("Global post-processing"));
  c |= postFxUi(r.post, false);
  ImGui::TextDisabled("%s", T("Applies to every camera. A camera can add to it or replace it (Inspector > Camera)."));
  if (c) ed.settingsDirty = true;
}

// --- Configuracoes > Tags -----------------------------------------------------------------------------------------
void drawTagsTab(Editor& ed) {
  static std::string msg; static char alias[48] = ""; static char renameBuf[48] = ""; static int renaming = -1;
  ImGui::SeparatorText(T("Built-in tags (used by the engine)"));
  for (auto& b : ed_::kBuiltinTags) ImGui::BulletText("%s  -  %s", b.name, T(b.meaning));
  ImGui::TextDisabled("%s", T("Built-in tags cannot be renamed. To use your own name, link it below."));
  ImGui::SeparatorText(T("Links (your tag -> built-in tag)"));
  for (size_t i = 0; i < ed.projSettings.links.size(); ++i) {
    auto& l = ed.projSettings.links[i];
    ImGui::PushID(static_cast<int>(i));
    ImGui::Text("\"%s\"  ->  \"%s\"", l.alias.c_str(), l.target.c_str());
    ImGui::SameLine(); if (ImGui::SmallButton(T("Remove link"))) { ed.projSettings.unlink(l.alias); ed.projSettings.save(ed.project); ImGui::PopID(); break; }
    ImGui::PopID();
  }
  static int target = 1;
  ImGui::SetNextItemWidth(150); ImGui::InputTextWithHint("##alias", T("your tag (e.g. jogador)"), alias, sizeof alias);
  ImGui::SameLine(); ImGui::Text("->"); ImGui::SameLine(); ImGui::SetNextItemWidth(120);
  if (ImGui::BeginCombo("##target", ed_::kBuiltinTags[static_cast<size_t>(target)].name)) {
    for (int i = 0; i < static_cast<int>(ed_::kBuiltinTags.size()); ++i) if (ImGui::Selectable(ed_::kBuiltinTags[static_cast<size_t>(i)].name, i == target)) target = i;
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  if (ImGui::Button(T("Link")) && alias[0]) {
    if (ed.projSettings.link(alias, ed_::kBuiltinTags[static_cast<size_t>(target)].name)) { ed.scene.tag(alias); ed.projSettings.save(ed.project); msg.clear(); alias[0] = 0; }
    else msg = T("Could not link: the name is empty, too long, or is itself a built-in tag.");
  }
  ImGui::SeparatorText(T("All tags in this project"));
  for (tool::TagId i = 0; i < ed.scene.tags().size(); ++i) {
    const std::string name(ed.scene.tags().name(i)); const bool builtin = ed_::isBuiltinTag(name);
    size_t users = 0; ed.scene.world().eachEntity([&](tool::Entity e) { if (ed.scene.world().tagMask(e).test(i)) ++users; });
    ImGui::PushID(static_cast<int>(i));
    if (renaming == static_cast<int>(i)) {
      ImGui::SetNextItemWidth(140);
      if (ImGui::InputText("##r", renameBuf, sizeof renameBuf, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) { msg = ed.renameTag(i, renameBuf) ? "" : T("Could not rename: the name is empty, too long, already used, or a built-in tag."); renaming = -1; }
      ImGui::SameLine(); if (ImGui::SmallButton(T("cancel"))) renaming = -1;
    } else {
      ImGui::Text("%s%s   (%zu object%s)", name.c_str(), builtin ? "  [built-in]" : "", users, users == 1 ? "" : "s");
      if (!builtin) { ImGui::SameLine(); if (ImGui::SmallButton(T("Rename"))) { renaming = static_cast<int>(i); ed_::ser::copyStr(renameBuf, sizeof renameBuf, name); } }
    }
    ImGui::PopID();
  }
  if (!msg.empty()) ImGui::TextColored({1.f, .6f, .4f, 1.f}, "%s", msg.c_str());
  ImGui::TextDisabled("%s", T("Renaming a tag changes it in every object and in the other scene files."));
}

// --- Configuracoes > Project (links de tags, editor externo, nota sobre exportacao) ------------------------------
void drawProjectTab(Editor& ed) {
  bool c = false;
  ImGui::SeparatorText(T("External code editor"));
  c |= ImGui::InputTextWithHint(T("Editor command"), T("empty = system default (Notepad on Windows)"), ed.projSettings.externalEditor, sizeof ed.projSettings.externalEditor);
  ImGui::TextDisabled("%s", T("Examples: code   |   notepad++.exe   |   C:\\Program Files\\Notepad++\\notepad++.exe. Run directly, never through a shell."));
  ImGui::SeparatorText(T("Export"));
  ImGui::TextDisabled("%s", T("Fill in File > Editor info, then use File > Export game."));
  if (c) ed.projSettings.save(ed.project);
}

void drawSettings(Editor& ed) {
  ImGui::SetNextWindowSize({460, 480}, ImGuiCond_FirstUseEver);
  if (ed.focusSettings > 0 && --ed.focusSettings == 0) ImGui::SetNextWindowFocus();
  if (!ImGui::Begin(W("Settings", "settings").c_str(), &ed.showSettings)) { ImGui::End(); return; }
  editor::helpIcon("profiler");
  if (ImGui::BeginTabBar("settingsTabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
    const char* names[] = {"Appearance", "Language", "Graphics", "Game", "Lighting", "Tags", "Project", "Physics"};
    for (int i = 0; i < 8; ++i) {
      const std::string label = std::string(T(names[i])) + "###tab" + std::to_string(i);
      if (ImGui::BeginTabItem(label.c_str(), nullptr, ed.settingsTab == i ? ImGuiTabItemFlags_SetSelected : 0)) {
        ImGui::BeginChild("tabbody");
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
        if (i == 0) drawAppearanceTab(ed); else if (i == 1) drawLanguageTab(ed); else if (i == 2) drawGraphicsTab(ed); else if (i == 3) drawGameTab(ed); else if (i == 4) drawLightingTab(ed); else if (i == 5) drawTagsTab(ed); else if (i == 6) drawProjectTab(ed); else editor::drawPhysicsTab(ed);
        ImGui::PopItemWidth();
        ImGui::EndChild();
        ImGui::EndTabItem();
      }
    }
    ed.settingsTab = -1;
    ImGui::EndTabBar();
  }
  ImGui::End();
}

void openSettings(Editor& ed, int tab) { ed.showSettings = true; ed.settingsTab = tab; ed.focusSettings = 3; }

void drawMenu(Editor& ed, bool& quit) {
  if (!ImGui::BeginMainMenuBar()) return;
  if (ImGui::BeginMenu(T("File"))) {
    if (ImGui::MenuItem(T("New scene..."))) { ed.showScenes = true; editor::ui().openNewScene = true; }
    if (ImGui::MenuItem(T("Save scene"), "Ctrl+S")) ed.saveScene();
    if (ImGui::MenuItem(T("Import asset..."))) SDL_ShowOpenFileDialog([](void*, const char* const* files, int) { if (files) for (; *files; ++files) editor::queueImport(*files); }, nullptr, nullptr, nullptr, 0, nullptr, true);
    ImGui::Separator();
    if (ImGui::MenuItem(T("New narrative graph"))) ed.narrative.newGraph();
    if (ImGui::MenuItem(T("Save graph"))) ed.narrative.save();
    ImGui::Separator();
    if (ImGui::MenuItem(T("Settings..."))) openSettings(ed, 0);
    if (ImGui::MenuItem(T("Editor info..."))) ed.showEditorInfo = true;
    if (ImGui::MenuItem(T("Export game..."))) ed.showExport = true;
    if (ImGui::MenuItem(T("Quit"))) quit = true;
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu(T("Edit"))) {
    if (ImGui::MenuItem(T("Undo"), "Ctrl+Z", false, ed.undoStack.canUndo() && !ed.playing)) ed.undo();
    if (ImGui::MenuItem(T("Redo"), "Ctrl+Y", false, ed.undoStack.canRedo() && !ed.playing)) ed.redo();
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu(T("Window"))) {
    ImGui::MenuItem(T("Hierarchy"), nullptr, &ed.showHierarchy); ImGui::MenuItem(T("Inspector"), nullptr, &ed.showInspector); ImGui::MenuItem("Console", nullptr, &ed.showConsole);
    ImGui::MenuItem("Profiler", nullptr, &ed.showProfiler); ImGui::MenuItem(T("Scene"), nullptr, &ed.showScene); ImGui::MenuItem(T("Game"), nullptr, &ed.showGame);
    ImGui::MenuItem("Assets", nullptr, &ed.showAssets); ImGui::MenuItem(T("Scenes"), nullptr, &ed.showScenes);
    ImGui::MenuItem(T("Settings"), nullptr, &ed.showSettings);
    ImGui::MenuItem(T("Narrative Controller"), nullptr, &ed.showNarrative);
    ImGui::MenuItem(T("Input Map"), nullptr, &ed.showInputMap);
    ImGui::MenuItem(T("Help"), "F1", &editor::helpState().open); ImGui::Separator();
    if (ImGui::MenuItem(T("Reset layout"))) ed.resetLayout = true;
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu(T("Interface"))) {
    ImGui::MenuItem(T("Lock interface"), nullptr, &ed.lockInterface);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("Locks the layout: tabs and windows can't be dragged out of the docks, and the splitters don't move. Use Reset layout to change it again."));
    ImGui::MenuItem(T("Dock only while holding Shift"), nullptr, &ed.dockWithShift);
    ImGui::MenuItem(T("Log keyboard events (debug)"), nullptr, &ed.logKeys);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("Writes each key press, release and typed text to the Console. Use it to find the key that erases text."));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("Hold Shift to dock or undock a window. Stops windows from detaching by accident when you click."));
    ImGui::Separator();
    if (ImGui::MenuItem(T("Reset layout"))) ed.resetLayout = true;
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu(T("Options"))) {
    if (ImGui::MenuItem(T("Appearance and colors..."))) openSettings(ed, 0);
    if (ImGui::MenuItem(T("Language..."))) openSettings(ed, 1);
    if (ImGui::MenuItem(T("Graphics API..."))) openSettings(ed, 2);
    if (ImGui::MenuItem(T("Game view (grid, split)..."))) openSettings(ed, 3);
    if (ImGui::MenuItem(T("Lighting and shadows..."))) openSettings(ed, 4);
    if (ImGui::MenuItem(T("Tags..."))) openSettings(ed, 5);
    if (ImGui::MenuItem(T("Project and export preset..."))) openSettings(ed, 6);
    if (ImGui::MenuItem(T("Physics (world gravity)..."))) openSettings(ed, 7);
    ImGui::EndMenu();
  }
  ImGui::SameLine(ImGui::GetWindowWidth() * 0.5f - 30.f);
  if (ImGui::SmallButton(ed.playing ? T("Stop") : T("Play"))) { if (ed.playing) ed.stopPlay(); else ed.startPlay(); }
  if (ed.playing) { ImGui::SameLine(); if (ImGui::SmallButton(ed.paused ? T("Resume") : T("Pause"))) ed.paused = !ed.paused; }
  ImGui::EndMainMenuBar();
}

// Barra de status: API, FPS, cena, entidades, erros e idioma.
void drawStatusBar(Editor& ed) {
  ImGuiViewport* vp = ImGui::GetMainViewport();
  const ImGuiWindowFlags fl = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;
  if (ImGui::BeginViewportSideBar("##status", vp, ImGuiDir_Down, ImGui::GetFrameHeight(), fl)) {
    if (ImGui::BeginMenuBar()) {
      const float ms = static_cast<float>(tool::debug().lastFrameMs());
      ImGui::Text("%s", F("API: %s", ed.apiStatus.c_str()).c_str());
      ImGui::Separator();
      ImGui::Text("%.0f FPS", ms > 0 ? static_cast<double>(1000.f / ms) : 0.0);
      ImGui::Separator();
      ImGui::Text("%s", F("scene: %s", ed.sceneInfo.name.c_str()).c_str());
      ImGui::Separator();
      ImGui::Text("%s", F("entities: %zu", ed.scene.world().liveCount()).c_str());
      ImGui::Separator();
      ImGui::TextColored(tool::debug().count(tool::Severity::Error) ? ImVec4(1, .4f, .4f, 1) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), "%s",
                         F("errors: %zu  warnings: %zu", tool::debug().count(tool::Severity::Error), tool::debug().count(tool::Severity::Warning)).c_str());
      if (!ed.statusLine.empty()) { ImGui::Separator(); ImGui::TextDisabled("%s", ed.statusLine.c_str()); }
      ImGui::SameLine(ImGui::GetWindowWidth() - 150);
      ImGui::TextDisabled("%s", editor::I18n::get().code().c_str());
      ImGui::EndMenuBar();
    }
    ImGui::End();
  }
}

// Interface travada: tira de todos os nos do encaixe a permissao de desencaixar (abas/janelas viram fixas), de
// dividir o espaco e de redimensionar os divisores. Vale para os nos ja criados (percorre a arvore a cada quadro).
void applyLayoutLock(ImGuiDockNode* node, bool lock) {
  if (!node) return;
  const ImGuiDockNodeFlags f = ImGuiDockNodeFlags_NoUndocking | ImGuiDockNodeFlags_NoDockingSplit | ImGuiDockNodeFlags_NoResize;
  node->SharedFlags = lock ? (node->SharedFlags | f) : (node->SharedFlags & ~f);
  for (ImGuiDockNode* child : node->ChildNodes) applyLayoutLock(child, lock);
}

// Travar interface: cancela o arrasto de janela pela barra de titulo e encaixa de volta as janelas que estiverem
// flutuando (so as do editor: nomes com "###"). Roda a cada quadro enquanto a opcao estiver ligada.
void lockWindows(ImGuiID dock) {
  ImGuiContext& g = *ImGui::GetCurrentContext();
  if (g.MovingWindow) { ImGui::ClearActiveID(); g.MovingWindow = nullptr; }
  for (ImGuiWindow* win : g.Windows) {
    if (!win->WasActive || win->DockIsActive || (win->Flags & ImGuiWindowFlags_Popup) || !std::strstr(win->Name, "###")) continue;
    ImGui::DockBuilderDockWindow(win->Name, dock);
  }
}

// Layout padrao estilo Unity: hierarquia/cenas a esquerda, inspetor a direita, assets/console embaixo, Scene+Game no centro.
void buildDefaultLayout(ImGuiID dock, const ImVec2& size) {
  ImGui::DockBuilderRemoveNode(dock);
  ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dock, size);
  ImGuiID center = dock;
  const ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.18f, nullptr, &center);
  const ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.26f, nullptr, &center);
  const ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.30f, nullptr, &center);
  ImGuiID leftBottom = 0;
  const ImGuiID leftTop = ImGui::DockBuilderSplitNode(left, ImGuiDir_Up, 0.60f, nullptr, &leftBottom);
  ImGui::DockBuilderDockWindow("###hier", leftTop);
  ImGui::DockBuilderDockWindow("###scenes", leftBottom);
  ImGui::DockBuilderDockWindow("###insp", right);
  ImGui::DockBuilderDockWindow("###settings", right);
  ImGui::DockBuilderDockWindow("###narr", bottom);
  ImGui::DockBuilderDockWindow("###prof", bottom);
  ImGui::DockBuilderDockWindow("###cons", bottom);
  ImGui::DockBuilderDockWindow("###assets", bottom); // a ultima janela encaixada fica ativa (aba aberta)
  ImGui::DockBuilderDockWindow("###game", center);
  ImGui::DockBuilderDockWindow("###view", center);
  ImGui::DockBuilderFinish(dock);
}

// --- persistencia (editor_settings.json + editor_theme.json) ---------------------------------------------------
void saveSettings(Editor& ed) {
  tool::Json j = tool::Json::object();
  j.set("language", editor::I18n::get().code());
  j.set("showGridScene", ed.showGridScene); j.set("showGridGame", ed.showGridGame); j.set("showGizmos", ed.showGizmos);
  j.set("graphicsApi", ed.apiChoice);
  j.set("resMode", ed.resMode); j.set("customW", ed.customW); j.set("customH", ed.customH); j.set("pixelFilter", ed.pixelFilter); j.set("fpsMode", ed.fpsMode);
  j.set("upscaleMode", ed.upscaleMode); j.set("aaMode", ed.aaMode);
  j.set("lockInterface", ed.lockInterface); j.set("dockWithShift", ed.dockWithShift);
  j.set("renderScaleSet", ed.renderScaleSet); if (ed.renderScaleSet) j.set("renderScale", static_cast<double>(ed.renderScale));
  j.set("lastScene", ed.sceneInfo.name);
  { std::error_code ec; j.set("lastProject", std::filesystem::absolute(ed.project.root, ec).string()); }
  std::ofstream f("editor_settings.json", std::ios::binary | std::ios::trunc); f << j.dump();
  ed.theme.save("editor_theme.json", ed.theme.current);
}
// Retorna a API grafica salva (0 = CPU).
int loadSettings(Editor& ed, std::string& lastScene, std::string& lastProject) {
  std::ifstream f("editor_settings.json", std::ios::binary);
  int api = 0;
  if (f) {
    std::stringstream ss; ss << f.rdbuf();
    if (auto j = tool::Json::parse(ss.str())) {
      editor::I18n::get().select((*j)["language"].asString());
      ed.showGridScene = editor::ser::flag(*j, "showGridScene", true); ed.showGridGame = editor::ser::flag(*j, "showGridGame", false); ed.showGizmos = editor::ser::flag(*j, "showGizmos", true);
      ed.resMode = std::clamp(static_cast<int>(editor::ser::num(*j, "resMode", 0.f)), 0, 5);
      ed.customW = static_cast<int>(editor::ser::num(*j, "customW", 1280.f)); ed.customH = static_cast<int>(editor::ser::num(*j, "customH", 720.f));
      ed.pixelFilter = editor::ser::flag(*j, "pixelFilter", false); ed.fpsMode = std::clamp(static_cast<int>(editor::ser::num(*j, "fpsMode", 0.f)), 0, 4);
      ed.upscaleMode = std::clamp(static_cast<int>(editor::ser::num(*j, "upscaleMode", 2.f)), 0, 3); ed.aaMode = std::clamp(static_cast<int>(editor::ser::num(*j, "aaMode", 0.f)), 0, 1);
      ed.lockInterface = editor::ser::flag(*j, "lockInterface", false); ed.dockWithShift = editor::ser::flag(*j, "dockWithShift", false);
      if (editor::ser::flag(*j, "renderScaleSet", false)) { ed.renderScaleSet = true; ed.renderScale = std::clamp(editor::ser::num(*j, "renderScale", 1.f), 0.25f, 2.f); }
      api = static_cast<int>(editor::ser::num(*j, "graphicsApi", 0.f)); lastScene = (*j)["lastScene"].asString(); lastProject = (*j)["lastProject"].asString();
    }
  }
  ed.theme.load("editor_theme.json");
  return api;
}

} // namespace

// O editor le assets/ (shaders, idiomas, temas) por caminho relativo. Para funcionar com dois cliques no .exe
// (ou pelo Visual Studio), procuramos a raiz do projeto: pasta atual -> pastas acima do executavel -> pasta
// de origem gravada na compilacao (TOOL_SOURCE_DIR). Ao achar, ela vira a pasta de trabalho.
void enterProjectRoot() {
  namespace fs = std::filesystem;
  std::error_code ec;
  auto isRoot = [&](const fs::path& p) { return fs::exists(p / "assets" / "shaders" / "pbr.hlsl", ec); };
  if (isRoot(fs::current_path(ec))) return;
  if (const char* base = SDL_GetBasePath()) {
    fs::path p = fs::path(base);
    for (int i = 0; i < 8 && !p.empty(); ++i, p = p.parent_path()) {
      if (isRoot(p)) { fs::current_path(p, ec); return; }
      if (p == p.parent_path()) break;
    }
  }
#ifdef TOOL_SOURCE_DIR
  if (isRoot(fs::path(TOOL_SOURCE_DIR))) fs::current_path(fs::path(TOOL_SOURCE_DIR), ec);
#endif
}

// Fonte da UI: assets/fonts/ui.ttf (do usuario) ou uma fonte do sistema com acentos; senao a embutida do ImGui.
void loadUiFont(ImGuiIO& io) {
  static const char* candidates[] = {"assets/fonts/ui.ttf", "C:/Windows/Fonts/segoeui.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                                     "/usr/share/fonts/TTF/DejaVuSans.ttf", "/System/Library/Fonts/Helvetica.ttc"};
  ImFontGlyphRangesBuilder gb;
  gb.AddRanges(io.Fonts->GetGlyphRangesDefault());
  gb.AddText("\xc4\x80"); // so para garantir o bloco Latin Estendido-A abaixo
  static ImVector<ImWchar> ranges;
  static const ImWchar extra[] = {0x0100, 0x017F, 0x0370, 0x03FF, 0x0400, 0x04FF, 0};
  gb.AddRanges(extra);
  gb.BuildRanges(&ranges);
  std::error_code ec;
  for (const char* p : candidates)
    if (std::filesystem::exists(p, ec) && io.Fonts->AddFontFromFileTTF(p, 15.f, nullptr, ranges.Data)) return;
}

int main(int argc, char** argv) {
  int maxFrames = 0; int startApi = -1; bool startPlaying = false, selfTest = false, playerMode = false;
  long long ecsCount = 0;   // --ecs N: benchmark de N esferas (ver abaixo)
  long long voxelCount = 0; // --voxel N: benchmark de N chunks de terreno (16x16x16 blocos cada)
  for (int i = 1; i + 1 < argc; ++i) if (std::strcmp(argv[i], "--frames") == 0) maxFrames = std::atoi(argv[i + 1]);
  for (int i = 1; i + 1 < argc; ++i) if (std::strcmp(argv[i], "--ecs") == 0) ecsCount = std::atoll(argv[i + 1]);
  for (int i = 1; i + 1 < argc; ++i) if (std::strcmp(argv[i], "--voxel") == 0) voxelCount = std::atoll(argv[i + 1]);
  const bool headless = maxFrames > 0 || ecsCount > 0 || voxelCount > 0;   // sem dialogo de projeto, sem gravar cena/configuracoes
  std::string projectArg;   // --project <pasta>: abre esse projeto direto, sem a janela de inicio
  for (int i = 1; i + 1 < argc; ++i) if (std::strcmp(argv[i], "--project") == 0) projectArg = argv[i + 1];
  if ((ecsCount > 0 || voxelCount > 0) && projectArg.empty()) { std::fprintf(stderr, "--ecs/--voxel precisam de --project <pasta de teste> (nao grava o projeto)\n"); return 1; }
  for (int i = 1; i + 1 < argc; ++i) if (std::strcmp(argv[i], "--api") == 0) startApi = std::strcmp(argv[i + 1], "vulkan") == 0 ? 1 : std::strcmp(argv[i + 1], "opengl") == 0 ? 2 : std::strcmp(argv[i + 1], "d3d12") == 0 ? 3 : 0;
  for (int i = 1; i < argc; ++i) { if (std::strcmp(argv[i], "--play") == 0) startPlaying = true; if (std::strcmp(argv[i], "--selftest") == 0) selfTest = true; if (std::strcmp(argv[i], "--player") == 0) playerMode = true; }
  std::string startLang, startTheme, startScene, focusWin, selectName; int startTab = -1; bool showInputMapAtStart = false; std::string narrativeFile;
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::strcmp(argv[i], "--lang") == 0) startLang = argv[i + 1];
    if (std::strcmp(argv[i], "--theme") == 0) startTheme = argv[i + 1];
    if (std::strcmp(argv[i], "--tab") == 0) startTab = std::atoi(argv[i + 1]);
    if (std::strcmp(argv[i], "--scene") == 0) startScene = argv[i + 1];
    if (std::strcmp(argv[i], "--focus") == 0) focusWin = argv[i + 1];   // para capturas de tela: traz uma janela (hier, game, assets...) para frente
    if (std::strcmp(argv[i], "--select") == 0) selectName = argv[i + 1]; // seleciona um objeto pelo nome
    if (std::strcmp(argv[i], "--help-topic") == 0) editor::openHelp(argv[i + 1]); // capturas de tela: abre a janela Help neste topico
    if (std::strcmp(argv[i], "--show-inputmap") == 0) showInputMapAtStart = true;
    if (std::strcmp(argv[i], "--narrative") == 0) narrativeFile = argv[i + 1];   // abre um grafo no Narrative Controller (capturas de tela)
  }
  if (selfTest && !std::getenv("SDL_VIDEODRIVER")) SDL_setenv_unsafe("SDL_VIDEODRIVER", "dummy", 1);
  if (maxFrames > 0 && startApi != 2 && !std::getenv("SDL_VIDEODRIVER")) SDL_setenv_unsafe("SDL_VIDEODRIVER", "dummy", 1);

  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) { std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
  enterProjectRoot();
  if (selfTest) { const int rc = editor::runSelfTest("assets/samples"); SDL_Quit(); return rc; } // sem janela
  SDL_Window* window = SDL_CreateWindow("ToolEngine Editor", 1600, 900, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, maxFrames > 0 ? "software" : nullptr) : nullptr;
  if (!window || !renderer) { std::fprintf(stderr, "window/renderer: %s\n", SDL_GetError()); return 1; }
  SDL_SetRenderVSync(renderer, ecsCount > 0 ? 0 : 1);   // --ecs: sem vsync, para medir o custo real do quadro

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImNodes::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
  io.IniFilename = headless ? nullptr : "editor_layout.ini";
  ImGui::StyleColorsDark();
  loadUiFont(io);
  ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
  ImGui_ImplSDLRenderer3_Init(renderer);

  Editor ed;
  editor::I18n::get().scan("assets/lang");
  editor::I18n::get().select("en");
  ed.theme.scan("assets/themes");
  ed.theme.apply();
  std::string lastScene, lastProject; int savedApi = 0;
  if (!headless) savedApi = loadSettings(ed, lastScene, lastProject);
  if (!startLang.empty()) editor::I18n::get().select(startLang);
  if (startTab >= 0) openSettings(ed, startTab);
  if (!startTheme.empty()) {
    const auto& pr = editor::builtinThemes();
    for (size_t i = 0; i < pr.size(); ++i) if (startTheme == pr[i].key) ed.theme.setPreset(i);
    for (auto& f : ed.theme.files) if (startTheme == f.name) ed.theme.load(f.path);
  }
  tool::Logger::get().setSink([&](tool::LogLevel l, std::string_view m) {
    ed.console.emplace_back(l, std::string(m)); if (ed.console.size() > 1000) ed.console.pop_front();
    std::fprintf(l >= tool::LogLevel::Warn ? stderr : stdout, "[%s] %.*s\n", tool::toString(l), static_cast<int>(m.size()), m.data());
  });
  tool::logInfo("Editor started.");
  if (startApi < 0) startApi = savedApi;
  if (startApi > 0) ed.applyApi(startApi);
  tool::logInfo("Graphics API: " + ed.apiStatus);

  // projeto: cria Project/Assets e Project/Scenes; abre a ultima cena (ou cria "Main" na primeira vez)
  // Projeto: --project <pasta> abre direto; senao (uso normal) a janela de inicio pede nome, autor e pasta.
  if (playerMode) { // jogo exportado: o executavel fica na pasta do jogo, com Data/Project ao lado
    std::error_code pe;
    const std::filesystem::path base = editor::editorFolder();
    std::filesystem::current_path(base, pe);
    projectArg = (base / "Data" / "Project").string();
    startPlaying = true;
  }
  if (!projectArg.empty()) {
    std::string err;
    std::error_code mk;
    std::filesystem::create_directories(projectArg, mk);   // --project cria a pasta se ela nao existir (uso de linha de comando)
    const std::filesystem::path root = editor::openProjectFolder(projectArg, err);
    if (root.empty()) { std::fprintf(stderr, "--project: %s (%s)\n", err.c_str(), projectArg.c_str()); SDL_Quit(); return 1; }
    ed.project.root = root;
  } else if (!headless) {
    editor::ProjectDialog dlg;
    dlg.lastProject = lastProject;
    const std::string loc = editor::defaultProjectsFolder(lastProject);
    std::snprintf(dlg.location, sizeof dlg.location, "%s", loc.c_str());
    bool closed = false;
    while (!dlg.done && !closed) {
      SDL_Event ev;
      while (SDL_PollEvent(&ev)) {
        ImGui_ImplSDL3_ProcessEvent(&ev);
        if (ev.type == SDL_EVENT_QUIT || (ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && ev.window.windowID == SDL_GetWindowID(window))) closed = true;
      }
      ImGui_ImplSDLRenderer3_NewFrame(); ImGui_ImplSDL3_NewFrame(); ImGui::NewFrame();
      editor::drawProjectDialog(dlg, window);
      ImGui::Render();
      { const ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg); SDL_SetRenderDrawColor(renderer, static_cast<Uint8>(bg.x * 255), static_cast<Uint8>(bg.y * 255), static_cast<Uint8>(bg.z * 255), 255); }
      SDL_RenderClear(renderer);
      ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
      SDL_RenderPresent(renderer);
    }
    if (closed) { // fechou na janela de inicio: nada foi criado
      ImGui_ImplSDLRenderer3_Shutdown(); ImGui_ImplSDL3_Shutdown();
      ImNodes::DestroyContext(); ImGui::DestroyContext();
      SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit();
      return 0;
    }
    ed.project.root = dlg.result;
    saveSettings(ed); // lembra o projeto para o proximo "Abrir ultimo projeto"
  }
  ed.project.ensure();
  ed.projSettings.load(ed.project);
  if (!ed.projSettings.projectName.empty()) SDL_SetWindowTitle(window, ("ToolEngine Editor - " + ed.projSettings.projectName).c_str()); // Project/project.json (ligacoes de tags, preset de exportacao); ausente = padrao
  if (!ed.inputMap.load(ed.project)) ed.inputMap = editor::InputMap::defaults(); // Project/input_map.json; ausente = mapa padrao
  ed.applyInputMap();
  ed.refreshScenes();
  {
    const std::string want = !startScene.empty() ? startScene : lastScene;
    const bool exists = std::find(ed.sceneList.begin(), ed.sceneList.end(), want) != ed.sceneList.end();
    if (!want.empty() && exists && ed.openScene(want, false)) {}
    else if (!ed.sceneList.empty() && ed.openScene(ed.sceneList.front(), false)) {}
    else { ed.sceneInfo.name = "Main"; ed.createDefaultContent(); ed.saveScene(); ed.undoReset(); }
  }
  if (showInputMapAtStart) ed.showInputMap = true;
  if (!narrativeFile.empty()) ed.narrative.open(narrativeFile);
  if (startPlaying) ed.startPlay();
  // --ecs N: benchmark sem vsync. Cria N esferas (como "test ECS: N"), mede o tempo de cada quadro depois do nascimento
  // e imprime media, p50, p95 e maximo. Sai sozinho ao fim de 300 quadros medidos.
  if (ecsCount > 0) ed.startEcsTest(ecsCount);
  if (voxelCount > 0) {   // chunks em grade quadrada, todos com terreno
    const long long side = static_cast<long long>(std::ceil(std::sqrt(static_cast<double>(voxelCount))));
    for (long long i = 0; i < voxelCount; ++i) ed.makeVoxel("Chunk", static_cast<int>(i % side), static_cast<int>(i / side), true);
  }
  if (!selectName.empty()) ed.scene.world().eachEntity([&](tool::Entity e) { auto* n = ed.scene.world().get<editor::Name>(e); if (n && selectName == n->text) ed.selected = e; });

  bool quit = false;
  int frame = 0, layoutFocus = 0;
  const Uint64 t0 = SDL_GetTicks();
  const bool bench = ecsCount > 0 || voxelCount > 0;
  std::vector<double> benchMs; bool benchArmed = false;
  while (!quit) {
    const Uint64 frameStart = SDL_GetTicksNS();
    if (ed.fpsDirty && renderer) { SDL_SetRenderVSync(renderer, ed.fpsMode == 0 && !headless ? 1 : 0); ed.fpsDirty = false; }
    tool::debug().beginFrame();
    ed.prof.beginFrame();
    SDL_Event e;
    // Ocioso: espera ate 16 ms pelo proximo evento (em vez de girar o quadro sem parar). O evento volta para a fila.
    // A espera vem ANTES da zona de medicao: nao e trabalho e nao deve aparecer como "Events + input".
    if (ed.viewsIdle() && !headless) { SDL_Event waited; if (SDL_WaitEventTimeout(&waited, 16)) SDL_PushEvent(&waited); }
    std::unique_ptr<editor::ZoneTimer> zone = std::make_unique<editor::ZoneTimer>(ed.prof, editor::Zone::Events); // um cronometro de cada vez
    int nEvents = 0;
    while (SDL_PollEvent(&e)) {
      ++nEvents;
      ImGui_ImplSDL3_ProcessEvent(&e);
      if (ed.logKeys && (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP))
        tool::logInfo(std::string("[keys] ") + (e.type == SDL_EVENT_KEY_DOWN ? "down " : "up ") + SDL_GetKeyName(e.key.key) + (e.key.repeat ? " (repeat)" : "") + (ImGui::GetIO().WantTextInput ? " [text field]" : ""));
      if (ed.logKeys && e.type == SDL_EVENT_TEXT_INPUT) tool::logInfo(std::string("[keys] text '") + (e.text.text ? e.text.text : "") + "'");
      if (e.type == SDL_EVENT_MOUSE_MOTION && ed.freeFly) { ed.mouseDX += e.motion.xrel; ed.mouseDY += e.motion.yrel; } // navegacao livre: mouse capturado
      if (e.type == SDL_EVENT_QUIT || (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && e.window.windowID == SDL_GetWindowID(window))) quit = true;
      if (e.type == SDL_EVENT_DROP_FILE && e.drop.data) editor::queueImport(e.drop.data); // arrastar arquivos do sistema para a janela
    }
    ed.idleFrames = nEvents > 0 ? 0 : ed.idleFrames + 1;
    zone = std::make_unique<editor::ZoneTimer>(ed.prof, editor::Zone::Update);
    ed.time = static_cast<double>(SDL_GetTicks() - t0) / 1000.0;
    ed.scene.setClock(ed.time);
    ed.scene.collectPool(ed.time);
    ed.uiTyping = ImGui::GetIO().WantTextInput;
    ed.tick(static_cast<double>(tool::debug().lastFrameMs()) / 1000.0);
    if (!ed.paused) ed.updateParticles(static_cast<float>(tool::debug().lastFrameMs()) / 1000.f);
    editor::processImports(ed);
    { // navegacao livre (Alt): o mouse fica preso na janela (modo relativo) e a UI ignora o mouse ate sair
      if (ed.flyChanged) {
        ed.flyChanged = false; ed.mouseDX = ed.mouseDY = 0.f;
        SDL_SetWindowRelativeMouseMode(window, ed.freeFly);
        if (ed.freeFly) ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse; else ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
      }
    }
    zone.reset();
    const auto panelsStart = editor::FrameProfiler::Clock::now();

    ImGui_ImplSDLRenderer3_NewFrame(); ImGui_ImplSDL3_NewFrame(); ImGui::NewFrame();
    if (!playerMode && !ImGui::GetIO().WantTextInput && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)) ed.saveScene();
    if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) { // F1: ajuda do topico da janela em foco
      const char* topic = "start"; ImGuiWindow* nav = ImGui::GetCurrentContext()->NavWindow;
      if (nav) { const std::string nm = nav->Name; auto has = [&](const char* k) { return nm.find(k) != std::string::npos; };
        if (has("###view")) topic = "scene"; else if (has("###insp") || has("###hier")) topic = "objects"; else if (has("###prof") || has("###settings")) topic = "profiler"; else if (has("###narr")) topic = "narrative"; else if (has("###inmap")) topic = "inputmap"; }
      editor::openHelp(topic);
    }
    if (playerMode && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) quit = true; // jogo exportado: Esc fecha
    if (!ImGui::GetIO().WantTextInput) { // Desfazer / Refazer
      if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z)) ed.undo();
      if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y) || ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z)) ed.redo();
    }
    ed.undoTick(ImGui::IsAnyItemActive() || ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsKeyDown(ImGuiKey_Delete) || ed.freeFly); // fim de interacao = novo passo do historico
    if (playerMode) {
      drawGameView(ed, renderer, true);
    } else {
      drawMenu(ed, quit);
      drawStatusBar(ed);
      if (!focusWin.empty() && frame == 24) ImGui::SetWindowFocus(("###" + focusWin).c_str());
      const ImGuiID dock = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
      if (ed.resetLayout || ImGui::DockBuilderGetNode(dock) == nullptr || ImGui::DockBuilderGetNode(dock)->IsLeafNode()) {
        buildDefaultLayout(dock, ImGui::GetMainViewport()->WorkSize);
        ed.resetLayout = false; layoutFocus = 12;
      }
      ImGui::GetIO().ConfigDockingWithShift = ed.dockWithShift;          // encaixe so com Shift, se ligado
      applyLayoutLock(ImGui::DockBuilderGetNode(dock), ed.lockInterface); // travar layout, se ligado (cada quadro)
      if (ed.lockInterface) lockWindows(dock);
      const bool focusTabs = layoutFocus > 0 && --layoutFocus == 0; // seleciona as abas ativas do layout padrao (Inspector, Scene, Assets)
      if (ed.showHierarchy) drawHierarchy(ed);
      if (focusTabs) ImGui::SetNextWindowFocus();
      if (ed.showInspector) drawInspector(ed);
      if (focusTabs) ImGui::SetNextWindowFocus();
      if (ed.showScene) drawSceneView(ed, renderer);
      if (ed.showGame) drawGameView(ed, renderer);
      if (ed.showConsole) drawConsole(ed);
      if (ed.showProfiler) drawProfiler(ed);
      if (focusTabs) ImGui::SetNextWindowFocus();
      if (ed.showAssets) editor::drawAssets(ed);
      if (ed.showScenes) editor::drawScenes(ed);
      if (ed.showSettings) drawSettings(ed);
      if (ed.showNarrative) ed.narrative.draw(&ed.showNarrative);
      if (ed.showInputMap) editor::drawInputMap(ed, &ed.showInputMap);
      if (editor::helpState().open) editor::drawHelp(ed, &editor::helpState().open);
      if (ed.showEditorInfo) editor::drawEditorInfo(ed, &ed.showEditorInfo);
      if (ed.showExport) editor::drawExport(ed, &ed.showExport);
    }
    { // "Panels" = tempo dos paineis SEM as renderizacoes 3D (essas ja foram contadas nas suas proprias zonas)
      const float total = std::chrono::duration<float, std::milli>(editor::FrameProfiler::Clock::now() - panelsStart).count();
      const float inner = ed.prof.cur[static_cast<int>(editor::Zone::SceneRender)] + ed.prof.cur[static_cast<int>(editor::Zone::GameRender)] + ed.prof.cur[static_cast<int>(editor::Zone::PostFx)] + ed.prof.cur[static_cast<int>(editor::Zone::Sky)];
      ed.prof.add(editor::Zone::Hierarchy, std::max(0.f, total - inner));
    }
    zone = std::make_unique<editor::ZoneTimer>(ed.prof, editor::Zone::Ui);
    ImGui::Render();
    if (!headless && !playerMode && (ed.themeDirty || ed.langDirty || ed.settingsDirty) && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) { saveSettings(ed); ed.themeDirty = ed.langDirty = ed.settingsDirty = false; }

    { const ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg); SDL_SetRenderDrawColor(renderer, static_cast<Uint8>(bg.x * 255), static_cast<Uint8>(bg.y * 255), static_cast<Uint8>(bg.z * 255), 255); }
    SDL_RenderClear(renderer);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
    zone = std::make_unique<editor::ZoneTimer>(ed.prof, editor::Zone::Present);
    SDL_RenderPresent(renderer);
    tool::debug().endFrame();
    if (ed.fpsMode >= 1 && ed.fpsMode <= 3 && !headless) { // limite manual de FPS (30/60/120): espera o que sobrar do quadro
      static const Uint64 target[4] = {0, 1000000000ull / 30, 1000000000ull / 60, 1000000000ull / 120};
      const Uint64 spent = SDL_GetTicksNS() - frameStart;
      if (spent < target[ed.fpsMode]) SDL_DelayNS(target[ed.fpsMode] - spent);
    }
    zone.reset();
    ++frame; if (maxFrames > 0 && frame >= maxFrames) quit = true;
    if (bench) {
      bool armed = false;
      ed.scene.world().each<editor::SpawnerComp>([&](tool::Entity, editor::SpawnerComp& s) { if (s.armed) armed = true; });
      if (armed) benchArmed = true;
      if (voxelCount > 0 && frame == 10) benchArmed = true;   // chunks nao tem nascimento: mede a partir do 10o quadro
      if (benchArmed && !armed) {   // nascimento terminou: mede os proximos quadros
        benchMs.push_back(static_cast<double>(SDL_GetTicksNS() - frameStart) / 1e6);
        if (benchMs.size() >= 300) {
          std::vector<double> s = benchMs; std::sort(s.begin(), s.end());
          double sum = 0; for (double v : s) sum += v;
          std::printf("ECS bench: %s, %zu bodies, %zu frames\n", ecsCount > 0 ? (std::to_string(ecsCount) + " spheres").c_str() : (std::to_string(voxelCount) + " voxel chunks").c_str(), ed.play.ents.size(), s.size());
          std::printf("ECS bench: avg %.2f ms (%.1f fps), p50 %.2f ms, p95 %.2f ms, max %.2f ms\n", sum / s.size(), 1000.0 * s.size() / sum,
                      s[s.size() / 2], s[s.size() * 95 / 100], s.back());
          for (int z = 0; z < static_cast<int>(editor::Zone::Count); ++z) std::printf("ECS bench:   %-34s %7.2f ms\n", editor::zoneName(static_cast<editor::Zone>(z)), static_cast<double>(ed.prof.zoneAvg(static_cast<editor::Zone>(z))));
          std::printf("ECS bench:   last frame: %llu draws, %llu triangles, %llu culled\n", static_cast<unsigned long long>(ed.prof.last.draws), static_cast<unsigned long long>(ed.prof.last.tris), static_cast<unsigned long long>(ed.prof.last.culled));
          quit = true;
        }
      }
    }
  }
  if (!headless && !playerMode) { ed.saveScene(); saveSettings(ed); } // ao sair, grava a cena e as configuracoes (nunca no jogo exportado)
  ed.stopPlay();
  tool::Logger::get().setSink(nullptr);
  ed.sceneTex.destroy(); ed.gameTex[0].destroy(); ed.gameTex[1].destroy();
  ed.shutdownGpu();
  ImGui_ImplSDLRenderer3_Shutdown(); ImGui_ImplSDL3_Shutdown();
  ImNodes::DestroyContext(); ImGui::DestroyContext();
  SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit();
  std::printf("editor exited after %d frames\n", frame);
  return 0;
}
