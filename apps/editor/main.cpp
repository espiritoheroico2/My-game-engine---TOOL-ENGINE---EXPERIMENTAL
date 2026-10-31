// ToolEngine Editor - janelas ancoraveis estilo Unity (ImGui docking) + SDL3.
// Janelas: Hierarchy, Inspector, Scene, Game, Assets, Scenes, Console, Profiler, Narrative Controller, Settings.
// O estado esta em editor_state.hpp; Assets/Scenes em assets_panel.hpp; componentes em components.hpp.
// Uso: tool_editor [--frames N] [--api vulkan|opengl|d3d12] [--lang pt-BR] [--tab N] [--play] [--screenshot-tab ...]
//   (N > 0 roda sem interacao e sai; usado nos testes automaticos)
#include "assets_panel.hpp"
#include "editor_state.hpp"
#include "extra_ui.hpp"
#include "fx.hpp"
#include "i18n.hpp"
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
std::vector<std::string> assetsOfKind(const Editor& ed, ed_::AssetKind kind) {
  std::vector<std::string> out; std::error_code ec;
  if (!std::filesystem::exists(ed.project.assetsDir(), ec)) return out;
  for (auto& e : std::filesystem::recursive_directory_iterator(ed.project.assetsDir(), std::filesystem::directory_options::skip_permission_denied, ec)) {
    if (out.size() >= 500) break;
    if (e.is_regular_file(ec) && ed_::kindOf(e.path(), false) == kind) out.push_back(ed.project.relative(e.path()));
  }
  std::sort(out.begin(), out.end());
  return out;
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
void scriptUi(Editor& ed) {
  tool::World& w = ed.scene.world();
  auto* sc = w.get<ed_::ScriptComp>(ed.selected);
  ImGui::SeparatorText("ToolObject Script");
  if (!sc) { if (ImGui::Button(T("Add script"))) ed.attachScript(ed.selected); return; }
  ImGui::Checkbox(T("Script enabled"), &sc->enabled);
  ImGui::TextDisabled("%s", sc->path[0] ? sc->path : T("(no file)"));
  if (sc->path[0]) { ImGui::SameLine(); if (ImGui::SmallButton(T("Edit external"))) editor::openInExternalEditor(ed, sc->path); if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("Opens the file in your code editor (Settings > Project).")); }
  static tool::Entity editing; static std::string text; static bool loaded = false, dirty = false; static std::string msg;
  if (editing != ed.selected) { editing = ed.selected; loaded = false; dirty = false; msg.clear(); }
  if (!loaded && ImGui::Button(T("Edit script"))) {
    if (!sc->path[0]) { const std::string rel = ed_::createScriptFile(ed.project, w.get<ed_::Name>(ed.selected)->text); if (!rel.empty()) ed_::ser::copyStr(sc->path, sizeof sc->path, rel); }
    loaded = ed_::readScript(ed.project, sc->path, text); msg = loaded ? "" : T("Could not read the script file."); dirty = false;
  }
  if (loaded) {
    if (ImGui::InputTextMultiline("##script", &text[0], text.capacity() + 1, ImVec2(-1, 220), ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_AllowTabInput,
        [](ImGuiInputTextCallbackData* d) { if (d->EventFlag == ImGuiInputTextFlags_CallbackResize) { auto* t = static_cast<std::string*>(d->UserData); t->resize(static_cast<size_t>(d->BufTextLen)); d->Buf = &(*t)[0]; } return 0; }, &text)) dirty = true;
    if (ImGui::Button(T("Save script"))) { msg = ed_::writeScript(ed.project, sc->path, text) ? T("Script saved.") : T("Could not save the script."); dirty = false; }
    ImGui::SameLine(); if (ImGui::Button(T("Close"))) loaded = false;
    if (dirty) { ImGui::SameLine(); ImGui::TextDisabled("%s", T("(unsaved)")); }
  }
  if (!msg.empty()) ImGui::TextDisabled("%s", msg.c_str());
  ImGui::TextDisabled("%s", T("Scripts are saved with the project. Running them needs the script runtime (planned)."));
}

// --- Hierarquia ------------------------------------------------------------------------------------------------
void drawHierarchy(Editor& ed) {
  if (!ImGui::Begin(W("Hierarchy", "hier").c_str(), &ed.showHierarchy)) { ImGui::End(); return; }
  editor::helpIcon("objects");
  tool::World& w = ed.scene.world();
  if (ImGui::Button(T("+ Create"))) ImGui::OpenPopup("createmenu");
  if (ImGui::BeginPopup("createmenu")) {
    if (ImGui::MenuItem(T("Empty object"))) ed.selected = ed.make("Empty");
    if (ImGui::MenuItem(T("Cube"))) ed.selected = ed.makeMesh("Cube", 0, 0, 0.5f, 0);
    if (ImGui::MenuItem(T("Sphere"))) ed.selected = ed.makeMesh("Sphere", 1, 0, 0.5f, 0);
    if (ImGui::MenuItem(T("Plane"))) ed.selected = ed.makeMesh("Plane", 2, 0, 0, 0);
    ImGui::Separator();
    if (ImGui::MenuItem(T("Camera"))) ed.selected = ed.makeCamera(false);
    if (ImGui::MenuItem(T("Main Camera (Principal)"))) ed.selected = ed.makeCamera(true);
    if (ImGui::MenuItem(T("Global Light"))) ed.selected = ed.makeGlobalLight();
    if (ImGui::MenuItem(T("Point Light"))) ed.selected = ed.makePointLight();
    if (ImGui::MenuItem(T("Audio Source"))) ed.selected = ed.makeAudio("");
    ImGui::Separator();
    if (ImGui::MenuItem(T("Particle System"))) { ed.selected = ed.make("Particles"); w.add<ed_::ParticleComp>(ed.selected); w.get<ed_::Transform>(ed.selected)->pos[1] = 1.f; }
    if (ImGui::MenuItem(T("Game UI (canvas)"))) { ed.selected = ed.make("UI"); ed_::UiCanvas u; ed_::UiElem& el = u.items[u.count++]; ed_::ser::copyStr(el.text, sizeof el.text, "Score: {score}"); w.add<ed_::UiCanvas>(ed.selected, u); }
    ImGui::EndPopup();
  }
  ImGui::SameLine();
  const bool has = w.alive(ed.selected);
  if (ImGui::Button(T("Duplicate")) && has) ed.selected = ed.duplicate(ed.selected);
  ImGui::SameLine();
  if (ImGui::Button(T("Delete")) && has) ed.destroy(ed.selected);
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
  std::function<void(tool::Entity, int)> row = [&](tool::Entity e, int depth) {
    auto* n = w.get<ed_::Name>(e);
    const char* prefix = w.has<ed_::CameraComp>(e) ? (ed.isPrincipal(e) ? "[cam*] " : "[cam] ") : w.has<ed_::GlobalLight>(e) ? "[sun] " : w.has<ed_::LightComp>(e) ? "[light] " : w.has<ed_::AudioSource>(e) ? "[snd] " : w.has<ed_::ParticleComp>(e) ? "[fx] " : w.has<ed_::UiCanvas>(e) ? "[ui] " : "";
    char label[96]; std::snprintf(label, sizeof label, "%s%s##%u", prefix, n ? n->text : T("(unnamed)"), e.index);
    auto it = kids.find(key(e)); const bool leaf = it == kids.end();
    ImGuiTreeNodeFlags fl = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen | (leaf ? ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen : 0) | (ed.selected == e ? ImGuiTreeNodeFlags_Selected : 0);
    const bool open = ImGui::TreeNodeEx(label, fl);
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
      if (ImGui::MenuItem(T("Duplicate"))) ed.selected = ed.duplicate(e);
      if (ed.parentOf(e).index != tool::Entity{}.index && w.alive(ed.parentOf(e)) && ImGui::MenuItem(T("Detach from parent"))) { dragChild = e; doUnparent = true; }
      if (ImGui::MenuItem(T("Delete"))) toDelete = e;
      ImGui::EndPopup();
    }
    if (!leaf && open) { if (depth < 32) for (tool::Entity c : it->second) row(c, depth + 1); ImGui::TreePop(); }
  };
  for (tool::Entity r : roots) row(r, 0);
  // soltar no espaco vazio da janela = remove o pai
  ImGui::Dummy({ImGui::GetContentRegionAvail().x, std::max(24.f, ImGui::GetContentRegionAvail().y)});
  if (ImGui::BeginDragDropTarget()) {
    if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("HIER_ENTITY"); pl && pl->DataSize == sizeof(tool::Entity)) { dragChild = *static_cast<const tool::Entity*>(pl->Data); doUnparent = true; }
    ImGui::EndDragDropTarget();
  }
  if (doReparent && w.alive(dragChild) && w.alive(dragParent)) { ed.setParent(dragChild, dragParent, true); }
  if (doUnparent && w.alive(dragChild)) { ed.setParent(dragChild, tool::Entity{}, true); }
  if (w.alive(toDelete)) ed.destroy(toDelete);
  if (has && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Delete)) ed.destroy(ed.selected);
  ImGui::End();
}

// --- Inspetor --------------------------------------------------------------------------------------------------
void drawInspector(Editor& ed) {
  if (!ImGui::Begin(W("Inspector", "insp").c_str(), &ed.showInspector)) { ImGui::End(); return; }
  editor::helpIcon("objects");
  tool::World& w = ed.scene.world();
  if (!w.alive(ed.selected)) { ImGui::TextDisabled("%s", T("no object selected")); ImGui::End(); return; }
  ImGui::Text("%s", F("Entity %u (generation %u)", ed.selected.index, ed.selected.generation).c_str());
  if (auto* n = w.get<ed_::Name>(ed.selected)) ImGui::InputText(T("Name"), n->text, sizeof n->text);
  if (auto* t = w.get<ed_::Transform>(ed.selected)) {
    ImGui::DragFloat3(T("Position"), t->pos, 0.05f); ImGui::DragFloat3(T("Rotation"), t->rot, 0.5f); ImGui::DragFloat3(T("Scale"), t->scale, 0.01f);
  }
  if (auto* mr = w.get<ed_::MeshRenderer>(ed.selected)) {
    ImGui::SeparatorText("Mesh Renderer");
    const std::string shapes = std::string(T("Cube")) + '\0' + T("Sphere") + '\0' + T("Plane") + '\0' + T("Model") + '\0';
    ImGui::Combo(T("Shape"), &mr->shape, shapes.c_str());
    if (mr->shape == 3 && assetField(ed, T("Model file"), mr->model, sizeof mr->model, ed_::AssetKind::Model)) ed.lib.invalidate(mr->model);
    if (mr->shape == 3 && !mr->model[0]) ImGui::TextDisabled("%s", T("Import a .glb/.gltf/.fbx in Assets and pick it here."));
    ImGui::ColorEdit3(T("Color"), mr->color);
    ImGui::SliderFloat(T("Roughness"), &mr->roughness, 0.05f, 1.f); ImGui::SliderFloat(T("Metallic"), &mr->metallic, 0.f, 1.f);
    ImGui::Checkbox(T("Cast shadow"), &mr->castShadow); ImGui::SameLine(); ImGui::Checkbox(T("Receive shadow"), &mr->receiveShadow);
  }
  if (auto* lc = w.get<ed_::LightComp>(ed.selected)) {
    ImGui::SeparatorText(T("Light source"));
    const std::string types = std::string(T("Directional")) + '\0' + T("Point") + '\0' + "Spot" + '\0';
    int ty = static_cast<int>(lc->light.type); if (ImGui::Combo(T("Type"), &ty, types.c_str())) lc->light.type = static_cast<tool::LightType>(ty);
    ImGui::ColorEdit3(T("Light color"), &lc->light.color.x); ImGui::DragFloat(T("Intensity"), &lc->light.intensity, 0.1f, 0.f, 200.f); ImGui::DragFloat(T("Range"), &lc->light.range, 0.1f, 0.1f, 200.f);
    ImGui::DragFloat3(T("Direction"), &lc->light.direction.x, 0.02f);
  }
  if (auto* g = w.get<ed_::GlobalLight>(ed.selected)) {
    ImGui::SeparatorText(T("Global Light (sun)"));
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
  if (auto* c = w.get<ed_::CameraComp>(ed.selected)) {
    ImGui::SeparatorText(T("Camera"));
    ImGui::SliderFloat(T("Field of view"), &c->fovDeg, 10.f, 120.f, "%.0f deg");
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
  if (auto* a = w.get<ed_::AudioSource>(ed.selected)) {
    ImGui::SeparatorText(T("Audio Source"));
    if (assetField(ed, T("Sound file"), a->clip, sizeof a->clip, ed_::AssetKind::Audio)) ed.lib.invalidate(a->clip);
    ImGui::SliderFloat(T("Volume"), &a->volume, 0.f, 2.f);
    ImGui::Checkbox(T("Loop"), &a->loop); ImGui::SameLine(); ImGui::Checkbox(T("Play on start"), &a->playOnStart);
    ImGui::Checkbox(T("3D sound (needs a Sound Ear)"), &a->spatial);
    if (a->spatial) { ImGui::DragFloat(T("Min distance"), &a->minDistance, 0.1f, 0.1f, 100.f); ImGui::DragFloat(T("Max distance"), &a->maxDistance, 0.5f, 1.f, 1000.f); }
    if (ImGui::Button(T("Preview"))) { if (a->clip[0]) ed.previewSound(a->clip); }
    ImGui::SameLine(); if (ImGui::Button(T("Stop"))) ed.mixer.stop(ed.previewVoice);
  }
  editor::physicsUi(ed);
  editor::toolsUi(ed);
  editor::uiCanvasUi(ed);
  editor::particleUi(ed);
  editor::addResourceUi(ed);
  scriptUi(ed);
  tagsUi(ed);
  ImGui::End();
}

void drawConsole(Editor& ed) {
  if (!ImGui::Begin(W("Console", "cons").c_str(), &ed.showConsole)) { ImGui::End(); return; }
  editor::helpIcon("start");
  if (ImGui::Button(T("Clear"))) ed.console.clear();
  ImGui::SameLine(); ImGui::Text("%s", F("errors: %zu  warnings: %zu", tool::debug().count(tool::Severity::Error), tool::debug().count(tool::Severity::Warning)).c_str());
  ImGui::Separator(); ImGui::BeginChild("log");
  for (auto& [lvl, msg] : ed.console) {
    const ImVec4 c = lvl == tool::LogLevel::Error ? ImVec4(1, .4f, .4f, 1) : lvl == tool::LogLevel::Warn ? ImVec4(1, .85f, .3f, 1) : ImGui::GetStyleColorVec4(ImGuiCol_Text);
    ImGui::TextColored(c, "%s", msg.c_str());
  }
  if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.f);
  ImGui::EndChild(); ImGui::End();
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
    ImGui::Text("%s", F("triangles: %llu   shadow casters: %u   shadow triangles (CPU): %llu   shadow map reused: %u", c.tris, c.shadowCasters, c.shadowTris, c.shadowReused).c_str());
    ImGui::Text("%s", F("shadows: %s   map %d px   post-processing: %s", ed.sceneInfo.render.shadow.enabled ? "on" : "off", ed.sceneInfo.render.shadow.resolution, ed.sceneInfo.render.post.active() ? "on" : "off").c_str());
    if (ed.gpuDev) { const auto g = ed.gpuDev->frameStats(); ImGui::Text("%s", F("GPU: %u draws, %u triangles, %u uploads, %u pipeline switches", g.draws, g.triangles, g.bufferUploads, g.pipelineSwitches).c_str()); }
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
  constexpr int N = 24;
  for (int i = -N; i <= N; ++i) {
    const bool major = i % 5 == 0;
    const float fade = std::clamp(1.f - std::abs(static_cast<float>(i)) / static_cast<float>(N + 4), 0.15f, 1.f);
    const ImU32 col = IM_COL32(200, 200, 215, static_cast<int>((major ? 70 : 34) * fade));
    if (i != 0) { line3(dl, v, {static_cast<float>(i), 0, -N}, {static_cast<float>(i), 0, N}, col); line3(dl, v, {-N, 0, static_cast<float>(i)}, {N, 0, static_cast<float>(i)}, col); }
  }
  line3(dl, v, {-N, 0, 0}, {N, 0, 0}, IM_COL32(230, 70, 70, 190), 1.6f);
  line3(dl, v, {0, 0, -N}, {0, 0, N}, IM_COL32(70, 110, 240, 190), 1.6f);
  line3(dl, v, {0, 0, 0}, {0, 6, 0}, IM_COL32(80, 220, 90, 220), 1.6f);
  auto label = [&](tool::Vec3 p, const char* t, ImU32 c) { ImVec2 s; if (v.point(p, s)) dl->AddText(s, c, t); };
  label({6.4f, 0, 0}, "X", IM_COL32(230, 90, 90, 255)); label({0, 6.4f, 0}, "Y", IM_COL32(100, 230, 110, 255)); label({0, 0, 6.4f}, "Z", IM_COL32(100, 140, 255, 255));
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
  if (ed.playing) {
    ImGui::SameLine(); if (ImGui::Button(ed.paused ? T("Resume") : T("Pause"))) ed.paused = !ed.paused;
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
  {
    ImGui::SameLine(); ImGui::Checkbox(T("Post-fx"), &ed.showPostInScene);
    ImGui::SameLine(); ImGui::RadioButton(T("Move (W)"), &ed.gizmoMode, 0);
    ImGui::SameLine(); ImGui::RadioButton(T("Rotate (E)"), &ed.gizmoMode, 1);
    ImGui::SameLine(); ImGui::RadioButton(T("Scale (R)"), &ed.gizmoMode, 2);
    ImGui::SameLine(); ImGui::TextDisabled("%s", ed.freeFly ? T("FREE NAVIGATION: WASD move, Q/E down/up, Shift fast, wheel speed, Alt/Esc exit")
                                                          : T("right: orbit | middle: pan | wheel: zoom | Alt: free navigation | double-click in Hierarchy: focus"));
  }
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  const int w = std::clamp(static_cast<int>(avail.x), 64, 1920), h = std::clamp(static_cast<int>(avail.y), 64, 1080);
  const int rw = std::max(64, static_cast<int>(static_cast<float>(w) * ed.renderScale)), rh = std::max(36, static_cast<int>(static_cast<float>(h) * ed.renderScale));
  const tool::Camera cam = ed.sceneCamera();
  const tool::u8* pixels = ed.renderView(cam, rw, rh, true, editor::Zone::SceneRender, ed.showPostInScene ? &ed.sceneInfo.render.post : nullptr);
  ed.sceneTex.ensure(sdl, rw, rh);
  if (ed.sceneTex.tex) {
    SDL_SetTextureScaleMode(ed.sceneTex.tex, ed.pixelFilter ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR);
    SDL_UpdateTexture(ed.sceneTex.tex, nullptr, pixels, rw * 4);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Image(reinterpret_cast<ImTextureID>(ed.sceneTex.tex), ImVec2(static_cast<float>(w), static_cast<float>(h)));
    const ViewXf xf = ViewXf::make(cam, origin, {static_cast<float>(w), static_cast<float>(h)});
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(origin, {origin.x + static_cast<float>(w), origin.y + static_cast<float>(h)}, true);
    if (ed.showGridScene) drawGrid(dl, xf);
    drawParticles(dl, xf, ed);
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
        if (!consumed && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { // selecao por raio; sem acerto -> centro mais proximo (luzes/camera/vazios)
          tool::Entity hit = editor::pickByRay(ed, editor::rayFromScreen(cam, origin, {static_cast<float>(w), static_cast<float>(h)}, io.MousePos));
          if (!ed.scene.world().alive(hit)) {
            float best = 28.f;
            ed.scene.world().eachEntity([&](tool::Entity e) {
              auto* t = ed.scene.world().get<ed_::Transform>(e); if (!t) return;
              auto* m = ed.scene.world().get<ed_::MeshRenderer>(e);
              if (m && m->shape == 2) return; // o chao nao disputa o clique
              ImVec2 sp; if (!xf.point({t->pos[0], t->pos[1], t->pos[2]}, sp)) return;
              const float d = std::hypot(sp.x - io.MousePos.x, sp.y - io.MousePos.y); if (d < best) { best = d; hit = e; }
            });
          }
          if (ed.scene.world().alive(hit)) ed.selected = hit;
        }
      }
    }
  }
  ImGui::End();
}

void drawGameView(Editor& ed, SDL_Renderer* sdl) {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  if (!ImGui::Begin(W("Game", "game").c_str(), &ed.showGame)) { ImGui::End(); ImGui::PopStyleVar(); return; }
  editor::helpIcon("start");
  ImGui::PopStyleVar();
  ImGui::SetCursorPos({ImGui::GetCursorPosX() + 6.f, ImGui::GetCursorPosY() + 4.f});
  playButton(ed);
  const auto cams = ed.principalCameras();
  ImGui::SameLine(); ImGui::TextDisabled("%s", F("Principal cameras: %zu%s", cams.size(), cams.size() >= 2 ? (ed.sceneInfo.splitVertical ? " (vertical split)" : " (horizontal split)") : "").c_str());
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
  const bool fixedRes = ed.fixedRes(rw, rh); // resolucao fixa: a imagem mantem a proporcao e ganha barras pretas
  for (int i = 0; i < n; ++i) {
    const tool::Entity e = cams[static_cast<size_t>(i)];
    auto* t = ed.scene.world().get<ed_::Transform>(e); auto* c = ed.scene.world().get<ed_::CameraComp>(e);
    if (!t || !c) continue;
    const tool::Camera cam = ed.cameraFor(e);
    const tool::PostFx fx = tool::combinePostFx(ed.sceneInfo.render.post, c->post); // global da cena + local da camera
    const tool::u8* pixels = ed.renderView(cam, rw, rh, false, editor::Zone::GameRender, &fx);
    ed.gameTex[i].ensure(sdl, rw, rh);
    if (!ed.gameTex[i].tex) continue;
    SDL_UpdateTexture(ed.gameTex[i].tex, nullptr, pixels, rw * 4);
    if (i == 1) { if (vertical) ImGui::SameLine(0, gap); else ImGui::Dummy({0, gap}); }
    SDL_SetTextureScaleMode(ed.gameTex[i].tex, ed.pixelFilter ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR);
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
    { const ViewXf pxf = ViewXf::make(cam, origin, size); ImDrawList* pdl = ImGui::GetWindowDrawList(); pdl->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true); drawParticles(pdl, pxf, ed); pdl->PopClipRect(); }
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
  if (ImGui::SliderFloat(T("UI scale"), &th.scale, 0.7f, 2.0f, "%.2f")) { changed = true; ImGui::GetIO().FontGlobalScale = th.scale; }

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

// --- Configuracoes > Project (preset de exportacao: guardado, mas DESLIGADO) ----------------------------------
void drawProjectTab(Editor& ed) {
  auto& ex = ed.projSettings.exportPreset; bool c = false;
  ImGui::SeparatorText(T("External code editor"));
  c |= ImGui::InputTextWithHint(T("Editor command"), T("empty = system default (Notepad on Windows)"), ed.projSettings.externalEditor, sizeof ed.projSettings.externalEditor);
  ImGui::TextDisabled("%s", T("Examples: code   |   notepad++.exe   |   C:\\Program Files\\Notepad++\\notepad++.exe. Run directly, never through a shell."));
  ImGui::SeparatorText(T("Export preset"));
  ImGui::TextColored({1.f, .8f, .3f, 1.f}, "%s", T("Export is not active yet. The preset is only stored for the future."));
  ImGui::BeginDisabled(!ed_::kExportEnabled);
  std::string plats; for (auto* p : ed_::kExportPlatforms) { plats += p; plats += '\0'; }
  c |= ImGui::Combo(T("Target platform"), &ex.platform, plats.c_str());
  c |= ImGui::InputText(T("Game name"), ex.appName, sizeof ex.appName);
  c |= ImGui::Checkbox(T("Compress assets"), &ex.compressAssets); c |= ImGui::Checkbox(T("Leave editor-only data out"), &ex.stripEditorData);
  ImGui::Button(T("Export...")); 
  ImGui::EndDisabled();
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
  j.set("renderScaleSet", ed.renderScaleSet); if (ed.renderScaleSet) j.set("renderScale", static_cast<double>(ed.renderScale));
  j.set("lastScene", ed.sceneInfo.name);
  std::ofstream f("editor_settings.json", std::ios::binary | std::ios::trunc); f << j.dump();
  ed.theme.save("editor_theme.json", ed.theme.current);
}
// Retorna a API grafica salva (0 = CPU).
int loadSettings(Editor& ed, std::string& lastScene) {
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
      if (editor::ser::flag(*j, "renderScaleSet", false)) { ed.renderScaleSet = true; ed.renderScale = std::clamp(editor::ser::num(*j, "renderScale", 1.f), 0.25f, 2.f); }
      api = static_cast<int>(editor::ser::num(*j, "graphicsApi", 0.f)); lastScene = (*j)["lastScene"].asString();
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
  int maxFrames = 0; int startApi = -1; bool startPlaying = false, selfTest = false;
  for (int i = 1; i + 1 < argc; ++i) if (std::strcmp(argv[i], "--frames") == 0) maxFrames = std::atoi(argv[i + 1]);
  for (int i = 1; i + 1 < argc; ++i) if (std::strcmp(argv[i], "--api") == 0) startApi = std::strcmp(argv[i + 1], "vulkan") == 0 ? 1 : std::strcmp(argv[i + 1], "opengl") == 0 ? 2 : std::strcmp(argv[i + 1], "d3d12") == 0 ? 3 : 0;
  for (int i = 1; i < argc; ++i) { if (std::strcmp(argv[i], "--play") == 0) startPlaying = true; if (std::strcmp(argv[i], "--selftest") == 0) selfTest = true; }
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
  SDL_SetRenderVSync(renderer, 1);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImNodes::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
  io.IniFilename = maxFrames > 0 ? nullptr : "editor_layout.ini";
  ImGui::StyleColorsDark();
  loadUiFont(io);
  ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
  ImGui_ImplSDLRenderer3_Init(renderer);

  Editor ed;
  editor::I18n::get().scan("assets/lang");
  editor::I18n::get().select("en");
  ed.theme.scan("assets/themes");
  ed.theme.apply();
  std::string lastScene; int savedApi = 0;
  if (maxFrames == 0) savedApi = loadSettings(ed, lastScene);
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
  ed.project.ensure();
  ed.projSettings.load(ed.project); // Project/project.json (ligacoes de tags, preset de exportacao); ausente = padrao
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
  if (!selectName.empty()) ed.scene.world().eachEntity([&](tool::Entity e) { auto* n = ed.scene.world().get<editor::Name>(e); if (n && selectName == n->text) ed.selected = e; });

  bool quit = false;
  int frame = 0, layoutFocus = 0;
  const Uint64 t0 = SDL_GetTicks();
  while (!quit) {
    const Uint64 frameStart = SDL_GetTicksNS();
    if (ed.fpsDirty && renderer) { SDL_SetRenderVSync(renderer, ed.fpsMode == 0 && maxFrames == 0 ? 1 : 0); ed.fpsDirty = false; }
    tool::debug().beginFrame();
    ed.prof.beginFrame();
    SDL_Event e;
    std::unique_ptr<editor::ZoneTimer> zone = std::make_unique<editor::ZoneTimer>(ed.prof, editor::Zone::Events); // um cronometro de cada vez
    while (SDL_PollEvent(&e)) {
      ImGui_ImplSDL3_ProcessEvent(&e);
      if (e.type == SDL_EVENT_MOUSE_MOTION && ed.freeFly) { ed.mouseDX += e.motion.xrel; ed.mouseDY += e.motion.yrel; } // navegacao livre: mouse capturado
      if (e.type == SDL_EVENT_QUIT || (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && e.window.windowID == SDL_GetWindowID(window))) quit = true;
      if (e.type == SDL_EVENT_DROP_FILE && e.drop.data) editor::queueImport(e.drop.data); // arrastar arquivos do sistema para a janela
    }
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
    if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)) ed.saveScene();
    if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) { // F1: ajuda do topico da janela em foco
      const char* topic = "start"; ImGuiWindow* nav = ImGui::GetCurrentContext()->NavWindow;
      if (nav) { const std::string nm = nav->Name; auto has = [&](const char* k) { return nm.find(k) != std::string::npos; };
        if (has("###view")) topic = "scene"; else if (has("###insp") || has("###hier")) topic = "objects"; else if (has("###prof") || has("###settings")) topic = "profiler"; else if (has("###narr")) topic = "narrative"; else if (has("###inmap")) topic = "inputmap"; }
      editor::openHelp(topic);
    }
    if (!ImGui::GetIO().WantTextInput) { // Desfazer / Refazer
      if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z)) ed.undo();
      if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y) || ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z)) ed.redo();
    }
    ed.undoTick(ImGui::IsAnyItemActive() || ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsKeyDown(ImGuiKey_Delete) || ed.freeFly); // fim de interacao = novo passo do historico
    drawMenu(ed, quit);
    drawStatusBar(ed);
    if (!focusWin.empty() && frame == 24) ImGui::SetWindowFocus(("###" + focusWin).c_str());
    const ImGuiID dock = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
    if (ed.resetLayout || ImGui::DockBuilderGetNode(dock) == nullptr || ImGui::DockBuilderGetNode(dock)->IsLeafNode()) {
      buildDefaultLayout(dock, ImGui::GetMainViewport()->WorkSize);
      ed.resetLayout = false; layoutFocus = 12;
    }
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
    { // "Panels" = tempo dos paineis SEM as renderizacoes 3D (essas ja foram contadas nas suas proprias zonas)
      const float total = std::chrono::duration<float, std::milli>(editor::FrameProfiler::Clock::now() - panelsStart).count();
      const float inner = ed.prof.cur[static_cast<int>(editor::Zone::SceneRender)] + ed.prof.cur[static_cast<int>(editor::Zone::GameRender)] + ed.prof.cur[static_cast<int>(editor::Zone::PostFx)];
      ed.prof.add(editor::Zone::Hierarchy, std::max(0.f, total - inner));
    }
    zone = std::make_unique<editor::ZoneTimer>(ed.prof, editor::Zone::Ui);
    ImGui::Render();
    if (maxFrames == 0 && (ed.themeDirty || ed.langDirty || ed.settingsDirty) && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) { saveSettings(ed); ed.themeDirty = ed.langDirty = ed.settingsDirty = false; }

    { const ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg); SDL_SetRenderDrawColor(renderer, static_cast<Uint8>(bg.x * 255), static_cast<Uint8>(bg.y * 255), static_cast<Uint8>(bg.z * 255), 255); }
    SDL_RenderClear(renderer);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
    zone = std::make_unique<editor::ZoneTimer>(ed.prof, editor::Zone::Present);
    SDL_RenderPresent(renderer);
    tool::debug().endFrame();
    if (ed.fpsMode >= 1 && ed.fpsMode <= 3 && maxFrames == 0) { // limite manual de FPS (30/60/120): espera o que sobrar do quadro
      static const Uint64 target[4] = {0, 1000000000ull / 30, 1000000000ull / 60, 1000000000ull / 120};
      const Uint64 spent = SDL_GetTicksNS() - frameStart;
      if (spent < target[ed.fpsMode]) SDL_DelayNS(target[ed.fpsMode] - spent);
    }
    zone.reset();
    ++frame; if (maxFrames > 0 && frame >= maxFrames) quit = true;
  }
  if (maxFrames == 0) { ed.saveScene(); saveSettings(ed); } // ao sair, grava a cena e as configuracoes
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
