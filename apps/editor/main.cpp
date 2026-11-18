// UI-CONTRATO: este arquivo usa Dear ImGui. Catalogo, padroes e regras em ui_contract.hpp (secoes 2, 3 e 4).
// Elementos: barra de menus e menus (File, Interface, Help), docking (DockSpaceOverViewport, DockBuilder, travamento),
//   Hierarquia (TreeNodeEx, arrastar "HIER_ENTITY", popups, lista achatada com ImGuiListClipper acima de 300 objetos),
//   Inspetor (Transform com DragFloat, Mesh e cores), Console (InputTextWithHint com historico, BeginChild),
//   Settings (BeginTabBar, Combo, ColorEdit, SliderFloat), Profiler (PlotLines, ProgressBar, tabela),
//   viewport (ImGui::Image do SDL_Texture, InvisibleButton, grade e gizmos com ImDrawList), janela Game e modo --player,
//   barra de status (BeginViewportSideBar) e atalhos (IsKeyChordPressed Ctrl+S/Z/Y).
// Dados: estado do Editor (ed.selected, showX, lockInterface, dockWithShift), buffers "static char" (cmdBuf, renBuf).
// Troca SpriteUI: o loop de quadro (NewFrame/Render/RenderDrawData), os backends SDL3 e SDLRenderer3, o docking e
//   a hierarquia. A cena 3D e a imagem (ViewTex) continuam como estao; so o desenho sobre elas muda.
// ToolEngine Editor - janelas ancoraveis estilo Unity (ImGui docking) + SDL3.
// Janelas: Hierarchy, Inspector, Scene, Game, Assets, Scenes, Console, Profiler, Narrative Controller, Settings.
// O estado esta em editor_state.hpp; Assets/Scenes em assets_panel.hpp; componentes em components.hpp.
// Uso: tool_editor [--frames N] [--api vulkan|opengl|d3d12|d3d11] [--bench-frames N] [--lang pt-BR] [--tab N] [--play] [--screenshot-tab ...]
//   (N > 0 roda sem interacao e sai; usado nos testes automaticos)
#include "assets_panel.hpp"
#include <cctype>
#include "editor_state.hpp"
#include "export_ui.hpp"
#include "extra_ui.hpp"
#include "fx.hpp"
#include "i18n.hpp"
#include "profiler_model.hpp"
#include "project_dialog.hpp"
#include "selftest.hpp"
#include "viewport_tools.hpp"
#include "script_window.hpp"
#include "mainmenu_model.hpp"
#ifdef TOOL_WITH_SPRITEUI
#include "mainmenu_panel.hpp"
#include "spriteui_diagnostics.hpp"
#include "start_screen_spriteui.hpp"
#include "profiler_panel.hpp"
#include "statusbar_panel.hpp"
#include "help_panel.hpp"
#include "test_map_panel.hpp"
#include "console_model.hpp"
#include "console_panel.hpp"
#include "hierarchy_panel.hpp"
#include "inspector_panel.hpp"
#include "settings_changes.hpp"
#include "settings_panel.hpp"
#ifdef TOOL_WITH_SPRITEUI
#include "scene_view_panel.hpp"
#include "game_view_panel.hpp"
#include "view_overlays.hpp"
#endif
#include "settings_model.hpp"
#endif

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
  // A edicao fica numa janela propria (autocomplete, snippets e verificador): este botao so a abre.
  if (ImGui::Button(T("Edit script"))) ed_::openScriptWindow(ed);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("Opens the script in its own window (autocomplete, snippets and checks)."));
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

#ifdef TOOL_WITH_SPRITEUI
// Janela Hierarchy em SpriteUI (hierarchy_panel.hpp). O ImGui so cria a moldura da janela (titulo, X e posicao);
// a lista, a selecao e o arraste sao do painel. Os pedidos do painel sao aplicados aqui, na cena.
bool gHierarchySpriteOn = false;   // o painel SpriteUI existe
bool gHierarchyFrame = false;      // a janela ImGui esta aberta neste quadro
editor::sui::Box gHierarchyRect;   // area do conteudo, em pixels da janela

// Id do objeto para o modelo: geracao e indice juntos (um objeto apagado e recriado tem outro id).
editor::hierarchy_model::Id hierarchyKey(tool::Entity e) {
  return (static_cast<editor::hierarchy_model::Id>(e.generation) << 32) | e.index;
}
tool::Entity hierarchyEntity(editor::hierarchy_model::Id id) {
  tool::Entity e{};
  e.index = static_cast<decltype(e.index)>(id & 0xFFFFFFFFu);
  e.generation = static_cast<decltype(e.generation)>(id >> 32);
  return e;
}

editor::hierarchy_model::Kind hierarchyKindOf(tool::World& w, tool::Entity e) {
  using K = editor::hierarchy_model::Kind;
  if (w.has<ed_::CameraComp>(e)) return K::Camera;
  if (w.has<ed_::GlobalLight>(e) || w.has<ed_::LightComp>(e)) return K::Light;
  if (w.has<ed_::AudioSource>(e)) return K::Audio;
  if (w.has<ed_::ParticleComp>(e)) return K::Particles;
  if (w.has<ed_::UiCanvas>(e)) return K::Ui;
  return K::Object;
}

// Lista de objetos do quadro (pai, nome, tipo, ocultar e travar) e a selecao do Editor.
editor::hierarchy_panel::Source hierarchySource(Editor& ed) {
  tool::World& w = ed.scene.world();
  editor::hierarchy_panel::Source src;
  w.eachEntity([&](tool::Entity e) {
    editor::hierarchy_model::Item item;
    item.id = hierarchyKey(e);
    const tool::Entity par = ed.parentOf(e);
    item.parent = w.alive(par) ? hierarchyKey(par) : editor::hierarchy_model::NoId;
    const auto* nm = w.get<ed_::Name>(e);
    item.label = nm ? std::string(nm->text) : std::string(T("(unnamed)"));
    item.kind = hierarchyKindOf(w, e);
    item.hidden = w.has<ed_::Inactive>(e);
    item.locked = w.has<ed_::Locked>(e);
    src.items.push_back(std::move(item));
  });
  src.selected = w.alive(ed.selected) ? hierarchyKey(ed.selected) : editor::hierarchy_model::NoId;
  return src;
}

// Menu "+ Create": mesmos objetos do menu do ImGui, na mesma ordem de editor::hierarchy_panel::kCreateLabels.
void createFromHierarchyMenu(Editor& ed, int kind) {
  switch (kind) {
    case 0: ed.selected = ed.make("Empty"); break;
    case 1: ed.selected = ed.makeMesh("Cube", 0, 0, 0.5f, 0); break;
    case 2: ed.selected = ed.makeMesh("Sphere", 1, 0, 0.5f, 0); break;
    case 3: ed.selected = ed.makeMesh("Plane", 2, 0, 0, 0); break;
    case 4: ed.selected = ed.makeCamera(false); break;
    case 5: ed.selected = ed.makeCamera(true); break;
    case 6: ed.selected = ed.makeGlobalLight(); break;
    case 7: ed.selected = ed.makePointLight(); break;
    case 8: ed.selected = ed.makeAudio(""); break;
    default: break;
  }
}

void applyHierarchyRequests(Editor& ed, const editor::hierarchy_panel::Requests& reqs) {
  using A = editor::hierarchy_panel::Act;
  tool::World& w = ed.scene.world();
  for (const editor::hierarchy_panel::Action& a : reqs) {
    const tool::Entity e = hierarchyEntity(a.id);
    const bool alive = w.alive(e);
    const bool locked = alive && w.has<ed_::Locked>(e);
    switch (a.act) {
      case A::Select: ed.selected = alive ? e : tool::Entity{}; break;
      case A::Activate: if (alive) ed.focusOn(e); break;
      case A::Reparent: {
        const tool::Entity parent = hierarchyEntity(a.parent);
        if (alive && w.alive(parent) && !locked) ed.setParent(e, parent, true);
        break;
      }
      case A::Detach: if (alive && !locked) ed.setParent(e, tool::Entity{}, true); break;
      case A::ToggleHidden:
        if (!alive) break;
        if (w.has<ed_::Inactive>(e)) w.remove<ed_::Inactive>(e);
        else w.add<ed_::Inactive>(e, ed_::Inactive{});
        break;
      case A::ToggleLocked:
        if (!alive) break;
        if (w.has<ed_::Locked>(e)) w.remove<ed_::Locked>(e);
        else w.add<ed_::Locked>(e, ed_::Locked{});
        break;
      case A::Rename:
        if (alive && !a.text.empty()) if (auto* nm = w.get<ed_::Name>(e)) ed_::ser::copyStr(nm->text, sizeof nm->text, a.text.c_str());
        break;
      case A::Duplicate: if (alive) ed.selected = ed.duplicate(e); break;
      case A::Delete:
        if (!alive) break;
        if (locked) ed.statusLine = T("Object is locked: unlock it (padlock) before deleting.");
        else ed.destroy(e);
        break;
      case A::Create: createFromHierarchyMenu(ed, a.kind); break;
    }
  }
}

// Moldura ImGui da Hierarchy: cria a janela e mede a area do conteudo (abaixo da barra de titulo).
void drawHierarchyFrame(Editor& ed) {
  gHierarchyFrame = ImGui::Begin(W("Hierarchy", "hier").c_str(), &ed.showHierarchy);
  if (gHierarchyFrame) {
    const ImVec2 pos = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    const float titleH = ImGui::GetFrameHeight();
    gHierarchyRect = {pos.x, pos.y + titleH, size.x, size.y - titleH};
  }
  ImGui::End();
}
#endif

void drawHierarchy(Editor& ed) {
#ifdef TOOL_WITH_SPRITEUI
  if (gHierarchySpriteOn) { drawHierarchyFrame(ed); return; }
#endif
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
    if (ImGui::MenuItem(T("Test map of chunks..."))) ed_::openTestMap();
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
  if (ImGui::CollapsingHeader(T("Pool demo (Recycle)"))) {
    if (ImGui::Button(T("+ Enemy"))) ed.spawnEnemy();
    ImGui::SameLine();
    if (ImGui::Button("Recycle") && has) { tool::ToolObject(&ed.scene, ed.selected).recycle(); ed.selected = {}; }
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
// Componentes do objeto (nome, Transform, Mesh Renderer, Light, Global Light, Camera, Audio Source) em SpriteUI
// (inspector_panel.hpp). Logo abaixo, no mesmo ImGui, seguem as secoes que ainda sao ImGui (fisica, voxel, UI,
// particulas, recursos, script, tags) e o rodape. O bloco SpriteUI reserva a sua altura com um Dummy do ImGui.
#ifdef TOOL_WITH_SPRITEUI
editor::inspector_panel::InspectorPanel* gInspectorPanel = nullptr;   // o painel existe (dono: main)
bool gInspectorSpriteOn = false;     // o painel SpriteUI existe
bool gInspectorFrame = false;        // a janela ImGui do Inspector esta aberta neste quadro
editor::sui::Box gInspectorOrigin;   // canto do bloco SpriteUI na tela (preenchido no quadro)
editor::sui::Box gInspectorVisible;  // parte do bloco que cabe na janela ImGui (a janela rola e corta)
bool gProfilerFrame = false;         // a janela ImGui do Profiler esta visivel neste quadro
editor::sui::Box gProfilerRect;       // area de conteudo da janela dockavel Profiler
bool gConsoleFrame = false;          // a janela ImGui do Console esta visivel neste quadro
editor::sui::Box gConsoleRect;        // area de conteudo da janela dockavel Console

// Dados do objeto selecionado para o painel. Os ponteiros valem neste quadro.
editor::inspector_panel::Context inspectorContext(Editor& ed) {
  editor::inspector_panel::Context c;
  tool::World& w = ed.scene.world();
  c.hasObject = w.alive(ed.selected);
  if (!c.hasObject) return c;
  const tool::Entity e = ed.selected;
  c.entityIndex = e.index;
  c.entityGeneration = e.generation;
  c.active = !w.has<ed_::Inactive>(e);
  c.locked = w.has<ed_::Locked>(e);
  c.name = w.get<ed_::Name>(e);
  c.transform = w.get<ed_::Transform>(e);
  c.mesh = w.get<ed_::MeshRenderer>(e);
  c.overrides = w.get<ed_::PartOverrides>(e);
  if (c.mesh && c.mesh->shape == 3 && c.mesh->node >= 0) {   // no de modelo: nomes das partes
    if (const auto* lm = ed.lib.model(c.mesh->model); lm && static_cast<size_t>(c.mesh->node) < lm->data.nodes.size()) {
      for (int pi : lm->data.nodes[static_cast<size_t>(c.mesh->node)].parts) {
        c.parts.emplace_back(pi, lm->data.parts[static_cast<size_t>(pi)].name);
      }
    }
  }
  c.light = w.get<ed_::LightComp>(e);
  c.global = w.get<ed_::GlobalLight>(e);
  c.camera = w.get<ed_::CameraComp>(e);
  c.principal = ed.isPrincipal(e);
  c.principalCount = ed.principalCameras().size();
  c.splitVertical = ed.sceneInfo.splitVertical;
  c.audio = w.get<ed_::AudioSource>(e);
  return c;
}

// Acoes com efeito fora do objeto. Cada uma faz o mesmo que o ImGui fazia antes da troca.
editor::inspector_panel::Hooks inspectorHooks(Editor& ed) {
  editor::inspector_panel::Hooks h;
  h.assets = [&ed](ed_::AssetKind k) { return assetsOfKind(ed, k); };
  h.assetChosen = [&ed](ed_::AssetKind, const std::string& rel) {   // mesma regra do assetField do ImGui
    if (!rel.empty()) ed.assignAsset(rel);
    ed.lib.invalidate(rel);
  };
  h.previewSound = [&ed](const std::string& rel) { ed.previewSound(rel); };
  h.stopSound = [&ed] { ed.mixer.stop(ed.previewVoice); };
  h.hasNamed = [&ed](const std::string& name) { tool::Entity found = ed.selected; return ed.findNamed(name, found); };
  h.setActive = [&ed](bool on) {
    tool::World& w = ed.scene.world();
    if (on) { if (w.has<ed_::Inactive>(ed.selected)) w.remove<ed_::Inactive>(ed.selected); }
    else if (!w.has<ed_::Inactive>(ed.selected)) w.tryAdd<ed_::Inactive>(ed.selected, ed_::Inactive{});
  };
  h.setLocked = [&ed](bool on) {
    tool::World& w = ed.scene.world();
    if (on) w.tryAdd<ed_::Locked>(ed.selected, ed_::Locked{});
    else w.remove<ed_::Locked>(ed.selected);
  };
  h.setPrincipal = [&ed](bool on) { ed.setPrincipal(ed.selected, on); };
  h.setSplitVertical = [&ed](bool vertical) { ed.sceneInfo.splitVertical = vertical; };
  h.setPartOverride = [&ed](int pi, bool on) {   // material editado por parte de um modelo importado
    tool::World& w = ed.scene.world();
    const auto* mr = w.get<ed_::MeshRenderer>(ed.selected);
    if (!mr) return;
    const auto* lm = ed.lib.model(mr->model);
    if (!lm || pi < 0 || static_cast<size_t>(pi) >= lm->data.parts.size()) return;
    const auto& part = lm->data.parts[static_cast<size_t>(pi)];
    auto* po = w.get<ed_::PartOverrides>(ed.selected);
    if (on) {
      if (!po) { po = w.tryAdd<ed_::PartOverrides>(ed.selected, ed_::PartOverrides{}); if (!po) return; }
      for (const auto& it : po->items) if (it.part == pi) return;
      ed_::PartOverride ov;
      ov.part = pi;
      ov.color[0] = part.material.albedo.x; ov.color[1] = part.material.albedo.y; ov.color[2] = part.material.albedo.z;
      ov.roughness = part.material.roughness; ov.metallic = part.material.metallic;
      po->items.push_back(ov);
    } else if (po) {
      po->items.erase(std::remove_if(po->items.begin(), po->items.end(), [&](const ed_::PartOverride& it) { return it.part == pi; }), po->items.end());
    }
  };
  return h;
}

// Bloco SpriteUI: prepara os controles, reserva a altura no ImGui e guarda a parte visivel dentro da janela.
void drawInspectorBlock(Editor& ed) {
  const float avail = ImGui::GetContentRegionAvail().x;
  const float h = gInspectorPanel->prepare(inspectorContext(ed), inspectorHooks(ed), avail);
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImGui::Dummy({avail, h});
  const ImVec2 wpos = ImGui::GetWindowPos();
  const ImVec2 wsize = ImGui::GetWindowSize();
  const float top = std::max(origin.y, wpos.y + ImGui::GetFrameHeight());   // abaixo da barra de titulo
  const float bottom = std::min(origin.y + h, wpos.y + wsize.y);
  gInspectorOrigin = {origin.x, origin.y, avail, h};
  gInspectorVisible = {origin.x, top, avail, bottom - top};
}

void drawProfilerFrame(Editor& ed) {
  gProfilerFrame = false;
  if (!ed.showProfiler) return;
  ImGui::SetNextWindowSize({editor::profiler_panel::Theme::kWidth, 360.f}, ImGuiCond_FirstUseEver);
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
  if (!ImGui::Begin(W("Profiler", "prof").c_str(), &ed.showProfiler, flags)) {
    ImGui::End();
    return;
  }
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  gProfilerRect = {origin.x, origin.y, avail.x, avail.y};
  ImGui::Dummy(avail);
  gProfilerFrame = true;
  ImGui::End();
}

void drawConsoleFrame(Editor& ed) {
  gConsoleFrame = false;
  if (!ed.showConsole) return;
  ImGui::SetNextWindowSize({720.f, 280.f}, ImGuiCond_FirstUseEver);
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
  if (!ImGui::Begin(W("Console", "cons").c_str(), &ed.showConsole, flags)) {
    ImGui::End();
    return;
  }
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  gConsoleRect = {origin.x, origin.y, avail.x, avail.y};
  ImGui::Dummy(avail);
  gConsoleFrame = true;
  ImGui::End();
}
#endif

void drawInspector(Editor& ed) {
#ifdef TOOL_WITH_SPRITEUI
  gInspectorFrame = false;
#endif
  if (!ImGui::Begin(W("Inspector", "insp").c_str(), &ed.showInspector)) { ImGui::End(); return; }
  editor::helpIcon("objects");
  tool::World& w = ed.scene.world();
#ifdef TOOL_WITH_SPRITEUI
  gInspectorFrame = true;
  if (gInspectorSpriteOn) drawInspectorBlock(ed);
  const bool textInImGui = !gInspectorSpriteOn;   // sem SpriteUI, a janela mostra o aviso sozinha
#else
  const bool textInImGui = true;
#endif
  if (!w.alive(ed.selected)) {
    if (textInImGui) ImGui::TextDisabled("%s", T("no object selected"));
    ImGui::End();
    return;
  }
  // Travado: as secoes de ImGui abaixo ficam so de leitura (o bloco SpriteUI cuida do proprio estado).
  const bool lockedNow = w.has<ed_::Locked>(ed.selected);
  ImGui::BeginDisabled(lockedNow);
  editor::physicsUi(ed);
  editor::toolsUi(ed);
  editor::voxelUi(ed);
  editor::uiCanvasUi(ed);
  editor::particleUi(ed);
  editor::addResourceUi(ed);
  scriptUi(ed);
  tagsUi(ed);
  // Rodape: adicionar componente (mesmo menu do "Add New"), abrir/fechar todas as secoes de componente.
  ImGui::Separator();
  if (ImGui::Button(T("Add Component"))) ImGui::OpenPopup("addres");
#ifdef TOOL_WITH_SPRITEUI
  if (gInspectorPanel) {
    ImGui::SameLine(); if (ImGui::Button(T("Collapse All"))) gInspectorPanel->setAllSections(false);
    ImGui::SameLine(); if (ImGui::Button(T("Open All"))) gInspectorPanel->setAllSections(true);
  }
#endif
  ImGui::EndDisabled();
  ImGui::End();
}

// Clicar numa linha "[Nome] ..." seleciona o objeto Nome na cena (nome vazio: nada).
void selectObjectNamed(Editor& ed, const std::string& name) {
  if (name.empty()) return;
  const tool::Entity e = ed.findByName(name);
  if (ed.scene.world().alive(e)) ed.selected = e;
}

// Janela de recurso ainda nao pronto: mostra o aviso e nada mais (Edit Mesh e Paint Mesh).
void drawFeatureStub(Editor& ed, const char* title, const char* text, bool* open) {
  (void)ed;
  if (!ImGui::Begin(title, open)) { ImGui::End(); return; }
  ImGui::TextWrapped("%s", text);
  ImGui::Separator();
  ImGui::TextDisabled("%s", T("Feature in development: not available yet."));
  ImGui::End();
}

#ifdef TOOL_WITH_SPRITEUI
editor::statusbar::Info statusBarInfo(Editor& ed) {
  editor::statusbar::Info info;
  info.api = ed.apiStatus;
  info.frameMs = tool::debug().lastFrameMs();
  info.scene = ed.sceneInfo.name;
  info.entities = ed.scene.world().liveCount();
  info.errors = tool::debug().count(tool::Severity::Error);
  info.warnings = tool::debug().count(tool::Severity::Warning);
  info.statusLine = ed.statusLine;
  info.language = editor::I18n::get().code();
  return info;
}
#endif

#ifdef TOOL_WITH_SPRITEUI
// Pedidos do painel SpriteUI do Console (o painel nao conhece o Editor). Aplicados depois do desenho.
void applyConsoleRequests(Editor& ed, const editor::console_panel::Requests& req) {
  if (req.clear) {
    ed.console.clear();
    ++ed.consoleSerial;
  }
  if (req.checkScripts) ed.checkAllScripts();
  if (!req.command.empty()) editor::runConsoleCommand(ed, req.command);
  selectObjectNamed(ed, req.select);
}
#endif

// Dados que o Profiler nao conhece (API, luz, GPU, cena): entram no modelo do painel SpriteUI a cada refresh.
editor::profiler_model::Extras fillProfilerExtras(Editor& ed) {
  editor::profiler_model::Extras x;
  x.api = ed.apiStatus;
  x.adapter = ed.gpuDev ? ed.gpuDev->deviceName() : std::string("(software renderer)");
  x.hasGpu = ed.gpuDev != nullptr;
  if (ed.gpuDev) {
    const auto g = ed.gpuDev->frameStats();
    x.gpuDraws = static_cast<unsigned>(g.draws);
    x.gpuTriangles = static_cast<unsigned>(g.triangles);
    x.gpuUploads = static_cast<unsigned>(g.bufferUploads);
    x.gpuPipelineSwitches = static_cast<unsigned>(g.pipelineSwitches);
  }
  const auto q = tool::makeQuality(ed.sceneInfo.render.lighting);
  const auto& lf = ed.lightRig.frame();
  x.lightShadows = lf.shadows();
  x.lightCascades = static_cast<unsigned>(q.csmCascades);
  x.lightShadowRes = static_cast<unsigned>(q.shadowMapResolution);
  x.lightDropped = static_cast<unsigned>(lf.droppedLights);
  x.postOn = ed.sceneInfo.render.post.active();
  const auto s = ed.scene.stats();
  x.entities = ed.scene.world().liveCount();
  x.dormant = ed.scene.dormantCount();
  x.reused = s.reused;
  x.freed = s.freed;
  x.meshes = ed.lib.meshCount();
  x.sounds = ed.lib.soundCount();
  x.tags = ed.scene.tags().size();
  x.errors = static_cast<unsigned>(tool::debug().count(tool::Severity::Error));
  x.warnings = static_cast<unsigned>(tool::debug().count(tool::Severity::Warning));
  return x;
}

// --- Janelas de visualizacao (Scene e Game) -------------------------------------------------------------------
#ifndef TOOL_WITH_SPRITEUI   // desenho ImGui da Scene e do Jogo (a build SpriteUI usa view_overlays.hpp)
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
    if (lc.light.type != tool::LightType::Point) line3(dl, v, p, p + ed.lightDir(e, lc) * 1.5f, col, 1.5f);
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
#endif  // !TOOL_WITH_SPRITEUI

#ifndef TOOL_WITH_SPRITEUI
// Build sem SpriteUI: Scene e Jogo so com ImGui. Na build SpriteUI, as janelas sao scene_view_panel / game_view_panel.
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
      // O arrasto da camera so vale se o botao foi pressionado DENTRO desta vista. Antes, um clique que comecava em outro
      // painel e passava por cima da Scene girava a camera sozinha (o ImGui considera "arrastando" qualquer botao preso).
      static bool navRight = false, navMid = false;
      if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) navRight = true;
      if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) navMid = true;
      if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) navRight = false;
      if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle)) navMid = false;
      if (hovered && !gizmo.dragging()) {
        if (navRight && ImGui::IsMouseDragging(ImGuiMouseButton_Right)) { ed.yaw -= io.MouseDelta.x * 0.008f; ed.pitch = std::clamp(ed.pitch + io.MouseDelta.y * 0.008f, -1.5f, 1.5f); }
        if (navMid && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
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
#endif  // !TOOL_WITH_SPRITEUI


#ifdef TOOL_WITH_SPRITEUI
// Janelas Scene e Jogo em SpriteUI (scene_view_panel.hpp, game_view_panel.hpp). O ImGui cria a janela (titulo, foco,
// docking, o "?") e mede a area; o painel desenha imagem, barra e sobreposicoes depois do ImGui. Sem a fonte, a
// janela nao aparece (como as outras janelas SpriteUI).
//
// Exemplo: a Scene calcula a selecao e a grade no quadro, o painel desenha e devolve o que o usuario pediu
// (`Requests`), e applySceneRequests aplica no Editor. Assim a regra de edicao fica em um lugar so.
namespace scene_panel = editor::scene_view_panel;
namespace game_panel = editor::game_view_panel;
namespace vm = editor::scene_view_model;
namespace gm = editor::game_view_model;
namespace vo = editor::view_overlays;
using ImageFit = ToolEngine::SpriteUi::Elements::ImageFit;

editor::scene_view_panel::ScenePanel* gScenePanel = nullptr;   // dono: main (unique_ptr)
editor::game_view_panel::GamePanel* gGamePanel = nullptr;
bool gSceneFrame = false;   // a janela Scene foi desenhada neste quadro
bool gGameFrame = false;
editor::sui::Box gSceneRect;   // area da janela (pixels da janela)
editor::sui::Box gGameRect;
scene_panel::Values gSceneValues;   // montado no quadro, desenhado depois do ImGui
game_panel::Values gGameValues;

void beginViewsFrame() { gSceneFrame = gGameFrame = false; }

// Entidade <-> id de 64 bits (o painel so guarda o id).
std::uint64_t packEntity(tool::Entity e) { return (static_cast<std::uint64_t>(e.index) << 32) | e.generation; }
tool::Entity unpackEntity(std::uint64_t id) {
  return tool::Entity{static_cast<std::uint32_t>(id >> 32), static_cast<std::uint32_t>(id & 0xffffffffu)};
}

scene_panel::Selection sceneSelection(Editor& ed) {
  scene_panel::Selection s;
  auto& w = ed.scene.world();
  if (!w.alive(ed.selected)) return s;
  const auto* t = w.get<ed_::Transform>(ed.selected);
  if (!t) return s;
  s.present = true;
  s.locked = w.has<ed_::Locked>(ed.selected);
  for (std::size_t i = 0; i < 3; ++i) {
    s.pos[i] = t->pos[i];
    s.rot[i] = t->rot[i];
    s.scale[i] = t->scale[i];
  }
  s.worldPos = ed.worldPos(ed.selected);
  s.world = ed.worldMat(ed.selected);
  const tool::Entity par = ed.parentOf(ed.selected);
  s.hasParent = w.alive(par);
  s.parentInv = s.hasParent ? editor::inverseAffine(ed.worldMat(par)) : tool::Mat4{};
  return s;
}

// Objeto sob o mouse: raio; sem acerto, o centro mais proximo (luzes, cameras, vazios). Mesma regra de antes.
tool::Entity pickSceneAt(Editor& ed, const tool::Camera& cam, editor::sui::Box img, vm::Pixel mp) {
  const ImVec2 origin(img.x, img.y), size(img.w, img.h), at(mp.x, mp.y);
  tool::Entity hit = editor::pickByRay(ed, editor::rayFromScreen(cam, origin, size, at));
  if (ed.scene.world().alive(hit)) return hit;
  const vm::Projection proj = vm::Projection::make(cam, {img.x, img.y, img.w, img.h});
  float best = 28.f;
  ed.scene.world().eachEntity([&](tool::Entity e) {
    const auto* t = ed.scene.world().get<ed_::Transform>(e);
    if (!t) return;
    const auto* m = ed.scene.world().get<ed_::MeshRenderer>(e);
    if (m && m->shape == 2) return;   // o chao nao disputa o clique
    vm::Pixel sp{};
    if (!proj.point({t->pos[0], t->pos[1], t->pos[2]}, sp)) return;
    const float d = std::hypot(sp.x - mp.x, sp.y - mp.y);
    if (d < best) { best = d; hit = e; }
  });
  return hit;
}

// O que tem sob o mouse na Scene (botao direito): id, nome e marcas do objeto.
scene_panel::Probe sceneProbe(Editor& ed, const tool::Camera& cam, editor::sui::Box img, vm::Pixel p) {
  scene_panel::Probe r;
  const tool::Entity e = pickSceneAt(ed, cam, img, p);
  auto& w = ed.scene.world();
  if (!w.alive(e)) return r;
  r.found = true;
  r.id = packEntity(e);
  if (const auto* nm = w.get<ed_::Name>(e)) r.name = nm->text;
  r.locked = w.has<ed_::Locked>(e);
  r.hidden = w.has<ed_::Inactive>(e);
  return r;
}

// Monta os dados da Scene do quadro: flags, selecao, particulas (abaixo), grade, cortados e icones (acima).
void buildSceneValues(Editor& ed, const tool::Camera& cam, editor::sui::Box img, SDL_Texture* tex, int tw, int th) {
  scene_panel::Values v;
  v.shown = true;
  v.cam = cam;
  v.texture = tex;
  v.texW = static_cast<float>(tw);
  v.texH = static_cast<float>(th);
  v.playing = ed.playing;
  v.paused = ed.paused;
  v.freeFly = ed.freeFly;
  v.ctrlDown = ImGui::GetIO().KeyCtrl;
  v.showGrid = ed.showGridScene;
  v.showGizmos = ed.showGizmos;
  v.showCulled = ed.showCulled;
  v.showPostFx = ed.showPostInScene;
  v.gizmoMode = ed.gizmoMode;
  v.localSpace = ed.gizmoLocal;
  v.snapOn = ed.snapOn;
  v.snapStep = ed.snapStep;
  v.sel = sceneSelection(ed);
  v.probe = [&ed, cam, img](vm::Pixel p) { return sceneProbe(ed, cam, img, p); };
  const vm::Projection proj = vm::Projection::make(cam, {img.x, img.y, img.w, img.h});
  vo::buildParticles(ed, cam, img, ed.selected, v.below);
  if (ed.showGridScene) vo::buildGrid(cam, proj, v.grid);
  if (ed.showCulled) vo::buildCulled(ed, proj, img, v.above);
  if (ed.showGizmos) {
    vo::buildSceneIcons(ed, proj, v.above);
    vo::buildPhysics(ed, proj, v.above);
  }
  gSceneValues = std::move(v);
}

// Item do menu de contexto da Scene (botao direito sobre um objeto).
void applySceneContext(Editor& ed, scene_panel::CtxAct act, tool::Entity e) {
  auto& w = ed.scene.world();
  if (!w.alive(e)) return;
  const bool locked = w.has<ed_::Locked>(e), hidden = w.has<ed_::Inactive>(e);
  switch (act) {
    case scene_panel::CtxAct::Inspector: ed.showInspector = true; break;
    case scene_panel::CtxAct::Focus: ed.focusOn(e); break;
    case scene_panel::CtxAct::Duplicate: ed.selected = ed.duplicate(e); break;
    case scene_panel::CtxAct::Delete:
      if (locked) ed.statusLine = T("Object is locked: unlock it (padlock) before deleting.");
      else ed.destroy(e);
      break;
    case scene_panel::CtxAct::SnapToContact:
      if (!locked && !snapToContact(ed, e)) ed.statusLine = T("Nothing to snap to here");
      break;
    case scene_panel::CtxAct::ToggleHidden:
      if (hidden) w.remove<ed_::Inactive>(e); else w.add<ed_::Inactive>(e, ed_::Inactive{});
      break;
    case scene_panel::CtxAct::ToggleLocked:
      if (locked) w.remove<ed_::Locked>(e); else w.add<ed_::Locked>(e, ed_::Locked{});
      break;
  }
}

// Pedidos da Scene: aplicados no Editor com as mesmas regras de antes (botoes, flags, arrasto, menu de contexto).
void applySceneRequests(Editor& ed, const scene_panel::Requests& r) {
  auto& w = ed.scene.world();
  if (r.playToggle) { if (ed.playing) ed.stopPlay(); else ed.startPlay(); }
  if (r.pauseToggle) ed.paused = !ed.paused;
  if (r.stepOnce) ed.stepOnce = true;
  if (r.grid) ed.showGridScene = *r.grid;
  if (r.gizmos) ed.showGizmos = *r.gizmos;
  if (r.culled) ed.showCulled = *r.culled;
  if (r.postFx) ed.showPostInScene = *r.postFx;
  if (r.gizmoMode) ed.gizmoMode = *r.gizmoMode;
  if (r.localSpace) ed.gizmoLocal = *r.localSpace;
  if (r.snapOn) ed.snapOn = *r.snapOn;
  if (r.snapStep) ed.snapStep = *r.snapStep;
  if (r.select && w.alive(unpackEntity(*r.select))) ed.selected = unpackEntity(*r.select);
  if (w.alive(ed.selected)) {
    if (auto* t = w.get<ed_::Transform>(ed.selected)) {
      for (std::size_t i = 0; i < 3; ++i) {
        if (r.pos) t->pos[i] = (*r.pos)[i];
        if (r.rot) t->rot[i] = (*r.rot)[i];
        if (r.scale) t->scale[i] = (*r.scale)[i];
      }
    }
  }
  if (r.snapToContact && !snapToContact(ed, ed.selected)) ed.statusLine = T("Nothing to snap to here");
  if (r.ctx) applySceneContext(ed, r.ctx->first, unpackEntity(r.ctx->second));
}

void applyGameRequests(Editor& ed, const game_panel::Requests& r) {
  if (r.playToggle) { if (ed.playing) ed.stopPlay(); else ed.startPlay(); }
  if (r.pauseToggle) ed.paused = !ed.paused;
  if (r.stepOnce) ed.stepOnce = true;
  for (const std::string& a : r.uiActions) ed.uiEvents.push_back(a);
}

// Teclas do menu da Scene: W, E, R trocam o modo do gizmo; Q alterna mundo e local. A regra fica em scene_transform_menu.
void sceneMenuKeys(Editor& ed) {
  namespace stm = editor::scene_transform_menu;
  const std::pair<ImGuiKey, stm::Action> keys[] = {
      {ImGuiKey_W, stm::Action::Move}, {ImGuiKey_E, stm::Action::Rotate}, {ImGuiKey_R, stm::Action::Scale}, {ImGuiKey_Q, stm::Action::ToggleSpace}};
  for (const auto& [key, action] : keys) {
    if (ImGui::IsKeyPressed(key, false)) stm::applyAction(action, ed.gizmoMode, ed.gizmoLocal);
  }
}

// Teclado e navegacao da Scene. Alt = navegacao livre (WASD); fora dela, W/E/R/Q mudam o menu do gizmo e o botao
// direito/meio movem a camera. Quem consome o clique no gizmo e o painel, entao aqui so vale fora do arrasto.
void sceneKeysAndNavigation(Editor& ed, bool hovered) {
  ImGuiIO& io = ImGui::GetIO();
  const bool typing = io.WantTextInput;
  const bool focused = hovered || ImGui::IsWindowFocused();
  if (!typing && focused && ImGui::IsKeyPressed(ImGuiKey_LeftAlt, false)) { ed.toggleFreeFly(); ed.flyChanged = true; }
  if (ed.freeFly) {
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) { ed.toggleFreeFly(); ed.flyChanged = true; return; }
    const float dt = std::min(0.1f, io.DeltaTime);
    ed.flyLook(ed.mouseDX, ed.mouseDY);
    ed.mouseDX = ed.mouseDY = 0.f;
    auto key = [](ImGuiKey k) { return ImGui::IsKeyDown(k) ? 1.f : 0.f; };
    ed.flyMove(dt, key(ImGuiKey_W) - key(ImGuiKey_S), key(ImGuiKey_D) - key(ImGuiKey_A), key(ImGuiKey_E) - key(ImGuiKey_Q), io.KeyShift);
    if (io.MouseWheel != 0.f) ed.flySpeed = std::clamp(ed.flySpeed * (1.f + io.MouseWheel * 0.15f), 0.5f, 80.f);
    return;
  }
  if (!typing && focused) sceneMenuKeys(ed);
  // O arrasto da camera so vale se o botao foi pressionado DENTRO desta vista (um clique que vem de outro painel
  // nao gira a camera sozinho).
  static bool navRight = false, navMid = false;
  if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) navRight = true;
  if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) navMid = true;
  if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) navRight = false;
  if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle)) navMid = false;
  if (!hovered || gScenePanel->dragging()) return;
  if (navRight && ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
    ed.yaw -= io.MouseDelta.x * 0.008f;
    ed.pitch = std::clamp(ed.pitch + io.MouseDelta.y * 0.008f, -1.5f, 1.5f);
  }
  if (navMid && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
    const tool::Camera cam = ed.sceneCamera();
    const tool::Vec3 f = tool::normalize(cam.target - cam.eye), r = tool::normalize(tool::cross(f, {0, 1, 0})), u = tool::cross(r, f);
    const float k = ed.dist * 0.0016f;
    ed.target = ed.target - r * (io.MouseDelta.x * k) + u * (io.MouseDelta.y * k);
  }
  ed.dist = std::clamp(ed.dist - io.MouseWheel * ed.dist * 0.1f, 1.5f, 200.f);
}

// Textura da Scene: renderiza (ou reusa, com a cena ociosa) no tamanho da imagem.
SDL_Texture* updateSceneTexture(Editor& ed, SDL_Renderer* sdl, const tool::Camera& cam, int w, int h, int& tw, int& th) {
  const int rw = std::max(64, static_cast<int>(static_cast<float>(w) * ed.renderScale));
  const int rh = std::max(36, static_cast<int>(static_cast<float>(h) * ed.renderScale));
  const bool soft = ed.softScaling();   // upscaling/AA por software: a textura tem o tamanho da janela
  tw = soft ? w : rw;
  th = soft ? h : rh;
  const bool reuse = ed.viewsIdle() && ed.sceneTex.tex && ed.sceneTex.w == tw && ed.sceneTex.h == th;
  if (!reuse) {
    const tool::u8* raw = ed.renderView(cam, rw, rh, true, editor::Zone::SceneRender, ed.showPostInScene ? &ed.sceneInfo.render.post : nullptr);
    const tool::u8* pixels = soft ? ed.finishView(ed.sceneUp, raw, rw, rh, w, h) : raw;
    ed.sceneTex.ensure(sdl, tw, th);
    if (ed.sceneTex.tex) SDL_UpdateTexture(ed.sceneTex.tex, nullptr, pixels, tw * 4);
  }
  if (ed.sceneTex.tex) SDL_SetTextureScaleMode(ed.sceneTex.tex, soft ? SDL_SCALEMODE_NEAREST : (ed.pixelFilter ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR));
  return ed.sceneTex.tex;
}

void drawSceneSui(Editor& ed, SDL_Renderer* sdl) {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  const bool open = ImGui::Begin(W("Scene", "view").c_str(), &ed.showScene);
  ImGui::PopStyleVar();
  if (!open) { ImGui::End(); return; }
  editor::helpIcon("scene");
  const ImVec2 origin = ImGui::GetCursorScreenPos(), avail = ImGui::GetContentRegionAvail();
  gSceneFrame = true;
  gSceneRect = {origin.x, origin.y, avail.x, avail.y};
  const editor::sui::Box img = scene_panel::imageArea(gSceneRect);
  const tool::Camera cam = ed.sceneCamera();
  sceneKeysAndNavigation(ed, gScenePanel->hovered());
  int tw = 0, th = 0;
  SDL_Texture* tex = updateSceneTexture(ed, sdl, cam, std::clamp(static_cast<int>(img.w), 64, 1920), std::clamp(static_cast<int>(img.h), 64, 1080), tw, th);
  // soltar um modelo/som do painel Assets cria o objeto na cena: a area da imagem e um alvo invisivel do ImGui
  ImGui::SetCursorScreenPos(ImVec2(img.x, img.y));
  ImGui::InvisibleButton("##scenedrop", ImVec2(img.w, img.h));
  if (ImGui::BeginDragDropTarget()) {
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_PATH")) ed_::instantiateAsset(ed, static_cast<const char*>(p->Data));
    ImGui::EndDragDropTarget();
  }
  buildSceneValues(ed, cam, img, tex, tw, th);
  ImGui::End();
}

// Textura de uma camera do jogo: renderiza (ou reusa) e sobe para o SDL_Texture da celula.
SDL_Texture* updateGameTexture(Editor& ed, SDL_Renderer* sdl, std::size_t i, const tool::Camera& cam, const tool::PostFx& fx,
                               int w, int h, int rw, int rh, bool soft, int& tw, int& th) {
  tw = soft ? w : rw;
  th = soft ? h : rh;
  const bool reuse = ed.viewsIdle() && ed.gameTex[i].tex && ed.gameTex[i].w == tw && ed.gameTex[i].h == th;
  if (!reuse) {
    const tool::u8* raw = ed.renderView(cam, rw, rh, false, editor::Zone::GameRender, &fx);
    const tool::u8* pixels = soft ? ed.finishView(ed.gameUp[i], raw, rw, rh, w, h) : raw;
    ed.gameTex[i].ensure(sdl, tw, th);
    if (ed.gameTex[i].tex) SDL_UpdateTexture(ed.gameTex[i].tex, nullptr, pixels, tw * 4);
  }
  if (ed.gameTex[i].tex) SDL_SetTextureScaleMode(ed.gameTex[i].tex, soft ? SDL_SCALEMODE_NEAREST : (ed.pixelFilter ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR));
  return ed.gameTex[i].tex;
}

// Uma celula (uma camera principal): imagem, grade e sobreposicoes. Com resolucao fixa, a imagem ocupa so o
// retangulo de letreiro dentro da celula (sem barras pretas no desenho, o painel faz o Contain).
void buildGameCell(Editor& ed, SDL_Renderer* sdl, std::size_t i, tool::Entity e, const gm::Rect& cell, bool fill, game_panel::Cell& out) {
  auto* t = ed.scene.world().get<ed_::Transform>(e);
  auto* c = ed.scene.world().get<ed_::CameraComp>(e);
  if (!t || !c) return;
  const int w = std::clamp(static_cast<int>(cell.w), 32, 1920), h = std::clamp(static_cast<int>(cell.h), 32, 1080);
  int rw = std::max(32, static_cast<int>(static_cast<float>(w) * ed.renderScale));
  int rh = std::max(18, static_cast<int>(static_cast<float>(h) * ed.renderScale));
  const bool fixedRes = !fill && ed.fixedRes(rw, rh);   // resolucao fixa: a imagem mantem a proporcao (Contain)
  const tool::Camera cam = ed.cameraFor(e);
  const tool::PostFx fx = tool::combinePostFx(ed.sceneInfo.render.post, c->post);
  int tw = 0, th = 0;
  out.texture = updateGameTexture(ed, sdl, i, cam, fx, w, h, rw, rh, ed.softScaling() && !fixedRes, tw, th);
  out.box = cell;
  out.texW = static_cast<float>(fixedRes ? rw : tw);
  out.texH = static_cast<float>(fixedRes ? rh : th);
  out.fit = fixedRes ? ImageFit::Contain : ImageFit::Stretch;
  const gm::Rect img = fixedRes ? gm::letterbox(cell, static_cast<float>(rw), static_cast<float>(rh)) : cell;
  const editor::sui::Box imgBox{img.x, img.y, img.w, img.h};
  if (ed.showGridGame) vo::buildGrid(cam, vm::Projection::make(cam, {img.x, img.y, img.w, img.h}), out.overlay);
  vo::buildParticles(ed, cam, imgBox, tool::Entity{}, out.overlay);
  vo::buildUi(ed, e, imgBox, ed.playing, gGamePanel->hoverHit(i), gGamePanel->pressedHit(i), out);
}

void drawGameSui(Editor& ed, SDL_Renderer* sdl, bool player) {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  bool open = false;
  if (player) {   // jogo exportado (--player): janela cheia, sem barra
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    open = ImGui::Begin("##player", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                             ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoDocking);
  } else {
    open = ImGui::Begin(W("Game", "game").c_str(), &ed.showGame);
    if (open) editor::helpIcon("start");
  }
  ImGui::PopStyleVar();
  if (!open) { ImGui::End(); return; }
  const ImVec2 origin = ImGui::GetCursorScreenPos(), avail = ImGui::GetContentRegionAvail();
  gGameFrame = true;
  gGameRect = {origin.x, origin.y, avail.x, avail.y};
  const editor::sui::Box area = player ? gGameRect
      : editor::sui::Box{origin.x, origin.y + game_panel::kToolbarH, avail.x, std::max(0.f, avail.y - game_panel::kToolbarH)};
  const auto cams = ed.principalCameras();
  game_panel::Values v;
  v.shown = true;
  v.player = player;
  v.playing = ed.playing;
  v.paused = ed.paused;
  if (cams.empty()) {
    v.message = T("No camera has the 'Principal' tag. Create a camera (Hierarchy > + Create > Main Camera) or tick 'Principal' on a camera in the Inspector.");
  } else {
    const std::size_t n = std::min<std::size_t>(2, cams.size());
    const bool vertical = ed.sceneInfo.splitVertical;
    v.cameraLabel = F("Principal cameras: %zu%s", cams.size(), cams.size() >= 2 ? (vertical ? " (vertical split)" : " (horizontal split)") : "");
    const std::vector<gm::Rect> cells = gm::splitCells({area.x, area.y, area.w, area.h}, n, vertical, 2.f);
    const bool fill = n == 2 && ed.sceneInfo.splitFill;   // split "preencher": cada metade usa a propria area
    for (std::size_t i = 0; i < n; ++i) {
      game_panel::Cell out;
      buildGameCell(ed, sdl, i, cams[i], cells[i], fill, out);
      v.cells.push_back(std::move(out));
    }
  }
  gGameValues = std::move(v);
  ImGui::End();
}

// Na build SpriteUI, a janela so existe com o painel (a fonte carregou). Sem fonte, nada e desenhado.
void drawSceneWindow(Editor& ed, SDL_Renderer* sdl) {
  if (gScenePanel) drawSceneSui(ed, sdl);
}
void drawGameWindow(Editor& ed, SDL_Renderer* sdl, bool player) {
  if (gGamePanel) drawGameSui(ed, sdl, player);
}

// Depois do RenderDrawData: os paineis desenham por cima e devolvem os pedidos do quadro. Sem quadro (janela
// fechada), o painel recebe Values vazio e so solta o estado.
void drawViewPanels(Editor& ed, SDL_Renderer* sdl) {
  const scene_panel::Values noScene;
  const game_panel::Values noGame;
  if (gScenePanel) {
    scene_panel::Requests req;
    if (!gScenePanel->draw(sdl, gSceneRect, gSceneFrame ? gSceneValues : noScene, req)) tool::logError(std::string("Scene SpriteUI render failed: ") + SDL_GetError());
    applySceneRequests(ed, req);
  }
  if (gGamePanel) {
    game_panel::Requests req;
    if (!gGamePanel->draw(sdl, gGameRect, gGameFrame ? gGameValues : noGame, req)) tool::logError(std::string("Game SpriteUI render failed: ") + SDL_GetError());
    applyGameRequests(ed, req);
  }
}

// Eventos: os paineis veem tudo, sem consumir (o ImGui continua recebendo o mouse e o teclado).
void forwardViewEvent(const SDL_Event& e) {
  if (gScenePanel) gScenePanel->handleEvent(e);
  if (gGamePanel) gGamePanel->handleEvent(e);
}
#else
void beginViewsFrame() {}
void drawSceneWindow(Editor& ed, SDL_Renderer* sdl) { drawSceneView(ed, sdl); }
void drawGameWindow(Editor& ed, SDL_Renderer* sdl, bool player) { drawGameView(ed, sdl, player); }
void drawViewPanels(Editor&, SDL_Renderer*) {}
void forwardViewEvent(const SDL_Event&) {}
#endif  // TOOL_WITH_SPRITEUI

void drawGraphicsTab(Editor& ed) {
  ImGui::Text("%s", F("Active API: %s", ed.apiStatus.c_str()).c_str());
  ImGui::SeparatorText(T("Graphics API"));
  int pick = ed.apiRequested;
  ImGui::RadioButton("Software (CPU)", &pick, 0);
#ifdef TOOL_EDITOR_GPU
  bool d3d11Available = false;
  for (auto b : tool::rhi::compiledBackends()) {
    if (b == tool::rhi::Backend::Vulkan) ImGui::RadioButton("Vulkan", &pick, 1);
    else if (b == tool::rhi::Backend::GLES) ImGui::RadioButton("OpenGL", &pick, 2);
    else if (b == tool::rhi::Backend::D3D12) ImGui::RadioButton("Direct3D 12", &pick, 3); // so existe se compilado com TOOL_WITH_D3D12
    else if (b == tool::rhi::Backend::D3D11) { ImGui::RadioButton("Direct3D 11", &pick, 4); d3d11Available = true; }
  }
  if (!d3d11Available) {
    ImGui::BeginDisabled();
    ImGui::RadioButton("Direct3D 11 (not built)", &pick, 4);
    ImGui::EndDisabled();
    ImGui::TextDisabled("%s", T("Build with the 'editor-gpu-dx11' preset to enable Direct3D 11."));
  }
#else
  ImGui::TextDisabled("%s", T("GPU backends are not available in this build (use the 'beta' preset)."));
  ImGui::BeginDisabled();
  ImGui::RadioButton("Direct3D 11 (not built)", &pick, 4);
  ImGui::EndDisabled();
  ImGui::TextDisabled("%s", T("Build with the 'editor-gpu-dx11' preset to enable Direct3D 11."));
#endif
  if (pick != ed.apiRequested) { ed.applyApi(pick); ed.settingsDirty = true; }   // so quando o usuario muda o radio (nao a cada quadro)
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
    auto& L = ed.sceneInfo.render.lighting; bool sc = false;
    sc |= ImGui::Checkbox(T("Enable shadows"), &L.shadows);
    const std::string q = std::string(T("Low (512)")) + '\0' + T("Medium (1024)") + '\0' + T("High (2048)") + '\0' + T("Ultra (4096)") + '\0';
    int qi = ::editor::settings_model::ShadowQualityIndex(L.shadowMapResolution);
    if (ImGui::Combo(T("Shadow quality"), &qi, q.c_str())) { L.shadowMapResolution = ::editor::settings_model::ShadowResolution(qi); L.custom = true; sc = true; } // trocar a resolucao sai do preset
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
  int sv = ::editor::settings_model::SplitIndex(ed.sceneInfo.splitVertical);
  if (ImGui::Combo(T("Split screen"), &sv, modes.c_str())) ed.sceneInfo.splitVertical = ::editor::settings_model::SplitIsVertical(sv);
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
  auto& r = ed.sceneInfo.render; auto& L = r.lighting; bool c = false;
  // Inteiro de 0..n com slider (os campos da Light Prism sao u32).
  auto sliderU = [&](const char* label, tool::u32& v, tool::u32 lo, tool::u32 hi) -> bool {
    int t = static_cast<int>(v);
    if (ImGui::SliderInt(label, &t, static_cast<int>(lo), static_cast<int>(hi))) { v = static_cast<tool::u32>(t); return true; }
    return false;
  };
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
  c |= ImGui::Checkbox(T("Occlusion culling (skip objects hidden behind big objects)"), &r.occlusionCull);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("Objects completely behind a large object from the camera are not drawn. Works with perspective cameras. Objects that cast shadows are always drawn."));
  c |= ImGui::DragFloat(T("Draw distance (0 = each camera's Far)"), &r.drawDistance, 1.f, 0.f, 20000.f, "%.0f");
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("Objects farther than this are not drawn in the Scene and Game views. 0 = use the Far value of each camera. Very large values make distant objects flicker (depth precision)."));
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("Objects outside the camera view are not drawn. Objects that cast shadows are always drawn."));

  // ---- Light Prism: qualidade (preset ou custom) ----
  ImGui::SeparatorText(T("Light Prism: quality"));
  ImGui::TextDisabled("%s", T("The lighting of the engine (lights, clusters, shadows and ambient) is computed by the Light Prism library, on the CPU and on the GPU."));
  const std::string tiers = std::string(T("Mobile")) + '\0' + T("Low") + '\0' + T("Medium") + '\0' + T("High") + '\0';
  c |= ImGui::Combo(T("Quality preset"), &L.tier, tiers.c_str());
  c |= ImGui::Checkbox(T("Custom values (override the preset)"), &L.custom);
  if (!L.custom) { // mostra o que o preset escolhido significa
    const auto q = tool::makeQuality(L);
    ImGui::TextDisabled("%s", F("Preset: %u shadow cascades, %u px per cascade, %u PCF taps, %u lights per cluster",
                                static_cast<unsigned>(q.csmCascades), static_cast<unsigned>(q.shadowMapResolution), static_cast<unsigned>(q.pcfTaps), static_cast<unsigned>(q.maxLightsPerCluster)).c_str());
  }
  ImGui::BeginDisabled(!L.custom);
  c |= sliderU(T("Shadow cascades"), L.csmCascades, 1, 4);
  {
    const std::string res = std::string(T("Low (512)")) + '\0' + T("Medium (1024)") + '\0' + T("High (2048)") + '\0' + T("Ultra (4096)") + '\0';
    int qi = L.shadowMapResolution <= 512 ? 0 : L.shadowMapResolution <= 1024 ? 1 : L.shadowMapResolution <= 2048 ? 2 : 3;
    if (ImGui::Combo(T("Shadow map resolution (per cascade)"), &qi, res.c_str())) { L.shadowMapResolution = 512u << qi; c = true; }
  }
  {
    const std::string pcf = std::string(T("Hard edges (1 tap)")) + '\0' + T("Soft 2x2 (4 taps)") + '\0' + T("Softer 3x3 (9 taps)") + '\0';
    int pi = L.pcfTaps <= 1 ? 0 : (L.pcfTaps <= 4 ? 1 : 2);
    if (ImGui::Combo(T("Edge softness (PCF)"), &pi, pcf.c_str())) { L.pcfTaps = pi == 0 ? 1u : (pi == 1 ? 4u : 9u); c = true; }
  }
  c |= sliderU(T("Max lights per cluster"), L.maxLightsPerCluster, 1, 256);
  c |= sliderU(T("Light indices per cluster"), L.indicesPerCluster, 1, 64);
  c |= ImGui::SliderFloat(T("Auto radius cutoff"), &L.autoRadiusCutoff, 0.f, 0.2f, "%.3f");
  ImGui::EndDisabled();

  // ---- Orcamento de luzes (sempre editavel) ----
  ImGui::SeparatorText(T("Light budget"));
  c |= sliderU(T("Max point lights"), L.maxPointLights, 1, 16384);
  c |= sliderU(T("Max spot lights"), L.maxSpotLights, 1, 4096);
  ImGui::TextDisabled("%s", T("Lights beyond the budget are dropped (the scene reports how many in Profiler > Rendering)."));

  // ---- Sombras do sol (cascatas) ----
  ImGui::SeparatorText(T("Shadows (sun cascades)"));
  c |= ImGui::Checkbox(T("Enable shadows"), &L.shadows);
  ImGui::BeginDisabled(!L.shadows);
  c |= ImGui::SliderFloat(T("Max shadow distance (m, 0 = camera far)"), &L.shadowDistance, 0.f, 500.f, "%.0f");
  c |= ImGui::SliderFloat(T("Cascade split (0 uniform - 1 logarithmic)"), &L.splitLambda, 0.f, 1.f, "%.2f");
  c |= ImGui::SliderFloat(T("Cascade blend"), &L.blendFraction, 0.f, 0.5f, "%.2f");
  c |= ImGui::SliderFloat(T("Caster extension (m)"), &L.casterExtension, 0.f, 200.f, "%.0f");
  c |= ImGui::SliderFloat(T("Depth bias (m)"), &L.depthBias, 0.f, 0.5f, "%.3f");
  c |= ImGui::SliderFloat(T("Normal bias (texels)"), &L.normalBias, 0.f, 4.f, "%.2f");
  c |= ImGui::Checkbox(T("Snap to texels (stable shadows, no shimmer)"), &L.snapToTexels);
  ImGui::EndDisabled();
  // Sombras de luzes pontuais/spot: feitas pela engine (a Light Prism so tem CSM do sol). Independentes do interruptor do sol.
  c |= sliderU(T("Point and spot shadow lights"), L.pointShadowLights, 0, 4);
  {
    const std::string pres = std::string("128") + '\0' + "256" + '\0' + "512" + '\0' + "1024" + '\0';
    int pi = L.pointShadowResolution <= 128 ? 0 : L.pointShadowResolution <= 256 ? 1 : L.pointShadowResolution <= 512 ? 2 : 3;
    if (ImGui::Combo(T("Point/spot shadow resolution (per face)"), &pi, pres.c_str())) { L.pointShadowResolution = 128u << pi; c = true; }
  }
  ImGui::TextDisabled("%s", T("Point and spot shadows: CPU and GPU (Vulkan, D3D12). OpenGL uses the CPU renderer."));
  // SSAO (Light Prism Post/Ssao): escurece a luz ambiente nas quinas e na base dos objetos.
  c |= ImGui::Checkbox(T("Ambient occlusion (SSAO)"), &L.ssao);
  ImGui::BeginDisabled(!L.ssao);
  c |= sliderU(T("AO kernel samples"), L.ssaoKernel, 1, 32);
  c |= ImGui::SliderFloat(T("AO radius (m)"), &L.ssaoRadius, 0.05f, 3.f, "%.2f");
  c |= ImGui::SliderFloat(T("AO bias (m)"), &L.ssaoBias, 0.f, 0.2f, "%.3f");
  c |= ImGui::SliderFloat(T("AO power (contrast)"), &L.ssaoPower, 0.1f, 4.f, "%.2f");
  ImGui::EndDisabled();
  // Ray tracing (CPU): a oclusao ambiente passa a ser por raios reais e substitui o SSAO.
  c |= ImGui::Checkbox(T("Ray-traced ambient occlusion (CPU)"), &L.rtAo);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("Casts real rays from each visible surface to find occluders. Replaces the SSAO above. Software renderer only (the GPU keeps the SSAO); sun shadows and voxels are not ray traced yet."));
  ImGui::BeginDisabled(!L.rtAo);
  c |= sliderU(T("RT AO rays per pixel"), L.rtAoRays, 1, 64);
  c |= ImGui::SliderFloat(T("RT AO radius (m)"), &L.rtAoRadius, 0.05f, 5.f, "%.2f");
  ImGui::EndDisabled();
  // Bloom: o brilho acima do limiar (objetos emissivos e superficies muito claras) vira um halo.
  c |= ImGui::Checkbox(T("Bloom (glow of emissive objects)"), &L.bloom);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("Only objects with Enable emission glow (Inspector > Emission). Other bright surfaces do not bloom. GPU and software renderer."));
  ImGui::BeginDisabled(!L.bloom);
  c |= ImGui::SliderFloat(T("Bloom strength"), &L.bloomStrength, 0.f, 4.f, "%.2f");
  ImGui::EndDisabled();

  // ---- Ambiente e exposicao ----
  ImGui::SeparatorText(T("Ambient and exposure"));
  c |= ImGui::SliderFloat(T("Ambient intensity"), &L.ambientIntensity, 0.f, 4.f, "%.2f");
  c |= ImGui::SliderFloat(T("Pre-exposure"), &L.preExposure, 0.1f, 4.f, "%.2f");
  ImGui::TextDisabled("%s", T("Each object chooses Cast shadow / Receive shadow in the Inspector. The sun and the nearest point/spot lights that have Cast shadow on cast shadows. Lower quality = more FPS."));

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

#ifdef TOOL_WITH_SPRITEUI
// Janela Configuracoes em SpriteUI (settings_panel.hpp): a faixa de abas e a aba Graficos. As outras abas seguem
// no ImGui, abaixo da faixa. Os pedidos do painel sao gravados no Editor aqui (mesmas regras da aba no ImGui).
bool gSettingsSpriteOn = false;   // o painel SpriteUI existe
bool gSettingsFrame = false;      // a janela ImGui esta aberta neste quadro
editor::sui::Box gSettingsRect;   // area da janela (faixa e conteudo), em pixels

// Quais APIs de render estao compiladas (o radio so mostra essas).
std::array<bool, editor::settings_bridge::kApiCount> settingsApiBuilt() {
  std::array<bool, editor::settings_bridge::kApiCount> built{};
  built[0] = true;   // Software (CPU) existe sempre
#ifdef TOOL_EDITOR_GPU
  for (auto b : tool::rhi::compiledBackends()) {
    if (b == tool::rhi::Backend::Vulkan) built[1] = true;
    else if (b == tool::rhi::Backend::GLES) built[2] = true;
    else if (b == tool::rhi::Backend::D3D12) built[3] = true;
    else if (b == tool::rhi::Backend::D3D11) built[4] = true;
  }
#endif
  return built;
}

editor::settings_panel::Values settingsValues(Editor& ed) {
  editor::settings_panel::Values v;
  auto& L = ed.sceneInfo.render.lighting;
  v.tab = ed.settingsTab < 0 ? 0 : ed.settingsTab;
  v.api = ed.apiRequested;
  v.apiBuilt = settingsApiBuilt();
  v.apiStatus = F("Active API: %s", ed.apiStatus.c_str());
  v.resMode = ed.resMode;
  v.customW = ed.customW;
  v.customH = ed.customH;
  int fw = 0, fh = 0;
  v.fixedSize = ed.fixedRes(fw, fh);
  v.fixedW = fw;
  v.fixedH = fh;
  v.renderScale = ed.renderScale;
  v.upscale = ed.upscaleMode;
  v.aa = ed.aaMode;
  v.pixelFilter = ed.pixelFilter;
  v.fps = ed.fpsMode;
  v.shadows = L.shadows;
  v.shadowQuality = ::editor::settings_model::ShadowQualityIndex(L.shadowMapResolution);
  v.adapter = F("Adapter: %s", ed.gpuDev ? ed.gpuDev->deviceName().c_str() : "(software renderer)");
  v.pixelsLine = F("Pixels per frame: %.2f M (all views)", static_cast<double>(ed.prof.last.pixels) / 1e6);
  v.gpuErrorLine = F("Consecutive GPU error frames: %d / 5 (then the editor falls back to the CPU renderer)", ed.gpuBadFrames);
  if (ed.gpuDev) {
    const auto s = ed.gpuDev->frameStats();
    v.gpuStatsLine = F("GPU: %u draws, %u triangles, %u uploads/frame", s.draws, s.triangles, s.bufferUploads);
  }
  return v;
}

// Alvo dos pedidos da janela Configuracoes (settings_changes.hpp): cada metodo grava um campo no Editor com a mesma
// regra que a aba tinha no ImGui (marca o modo de janela como sujo, troca de API pelo applyApi etc.).
struct SettingsEditorTarget {
  Editor& ed;
  int apiRequested() const { return ed.apiRequested; }
  void setTab(int i) { ed.settingsTab = i; }
  void applyApi(int api) { ed.applyApi(api); ed.settingsDirty = true; }
  void setResMode(int i) { ed.resMode = i; ed.settingsDirty = true; }
  void setCustomW(int w) { ed.customW = w; ed.settingsDirty = true; }
  void setCustomH(int h) { ed.customH = h; ed.settingsDirty = true; }
  void setRenderScale(float s) { ed.renderScale = s; ed.renderScaleSet = true; ed.settingsDirty = true; }
  void setUpscale(int i) { ed.upscaleMode = i; ed.settingsDirty = true; }
  void setAa(int i) { ed.aaMode = i; ed.settingsDirty = true; }
  void setPixelFilter(bool on) { ed.pixelFilter = on; ed.settingsDirty = true; }
  void setFps(int i) { ed.fpsMode = i; ed.fpsDirty = true; ed.settingsDirty = true; }
  void setShadows(bool on) { ed.sceneInfo.render.lighting.shadows = on; ed.settingsDirty = true; }
  void setShadowQuality(int i) {
    auto& L = ed.sceneInfo.render.lighting;
    L.shadowMapResolution = ::editor::settings_model::ShadowResolution(i);
    L.custom = true;
    ed.settingsDirty = true;
  }
};

#endif

// Conteudo de uma aba do ImGui (as abas em SpriteUI passam por aqui so se o ImGui precisar desenhar).
void drawSettingsTabBody(Editor& ed, int i) {
  if (i == 0) drawAppearanceTab(ed);
  else if (i == 1) drawLanguageTab(ed);
  else if (i == 2) drawGraphicsTab(ed);
  else if (i == 3) drawGameTab(ed);
  else if (i == 4) drawLightingTab(ed);
  else if (i == 5) drawTagsTab(ed);
  else if (i == 6) drawProjectTab(ed);
  else editor::drawPhysicsTab(ed);
}

void drawSettings(Editor& ed) {
#ifdef TOOL_WITH_SPRITEUI
  gSettingsFrame = false;
#endif
  ImGui::SetNextWindowSize({460, 480}, ImGuiCond_FirstUseEver);
  if (ed.focusSettings > 0 && --ed.focusSettings == 0) ImGui::SetNextWindowFocus();
  if (!ImGui::Begin(W("Settings", "settings").c_str(), &ed.showSettings)) { ImGui::End(); return; }
  editor::helpIcon("profiler");
#ifdef TOOL_WITH_SPRITEUI
  if (gSettingsSpriteOn) {   // faixa de abas e aba Graficos em SpriteUI; as outras abas seguem aqui, abaixo da faixa
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    gSettingsFrame = true;
    gSettingsRect = {origin.x, origin.y, avail.x, avail.y};
    ImGui::Dummy({avail.x, editor::settings_panel::Metrics::kStripH});
    const int tab = ed.settingsTab < 0 ? 0 : ed.settingsTab;
    if (tab != editor::settings_panel::kGraphicsTab) {
      ImGui::BeginChild("tabbody");
      ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
      drawSettingsTabBody(ed, tab);
      ImGui::PopItemWidth();
      ImGui::EndChild();
    }
    ImGui::End();
    return;
  }
#endif
  if (ImGui::BeginTabBar("settingsTabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
    for (int i = 0; i < ::editor::settings_model::kTabCount; ++i) {
      const std::string label = std::string(T(::editor::settings_model::kTabNames[i])) + "###tab" + std::to_string(i);
      if (ImGui::BeginTabItem(label.c_str(), nullptr, ed.settingsTab == i ? ImGuiTabItemFlags_SetSelected : 0)) {
        ImGui::BeginChild("tabbody");
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
        drawSettingsTabBody(ed, i);
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

// Estado da barra de menus principal SpriteUI. Cada marca le um flag do Editor.
editor::mainmenu::MenuState menuStateOf(Editor& ed) {
  editor::mainmenu::MenuState s;
  s.playing = ed.playing;
  s.paused = ed.paused;
  s.canUndo = ed.undoStack.canUndo();
  s.canRedo = ed.undoStack.canRedo();
  s.hierarchy = ed.showHierarchy;
  s.inspector = ed.showInspector;
  s.console = ed.showConsole;
  s.profiler = ed.showProfiler;
  s.editMesh = ed.showEditMesh;
  s.paintMesh = ed.showPaintMesh;
  s.scene = ed.showScene;
  s.game = ed.showGame;
  s.assets = ed.showAssets;
  s.scenes = ed.showScenes;
  s.settings = ed.showSettings;
  s.narrative = ed.showNarrative;
  s.inputMap = ed.showInputMap;
  s.spriteUiDiagnostics = ed.showSpriteUiDiagnostics;
  s.help = editor::helpState().open;
  s.lockInterface = ed.lockInterface;
  s.dockWithShift = ed.dockWithShift;
  s.logKeys = ed.logKeys;
  s.language = editor::I18n::get().code();
  return s;
}

// O DockSpace ainda e do ImGui; reserva-se a faixa para o menu renderizado pelo SpriteUI.
void reserveMenuBarStrip() {
  ImGuiViewport* vp = ImGui::GetMainViewport();
  const ImGuiWindowFlags fl = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;
  if (ImGui::BeginViewportSideBar("##menubar", vp, ImGuiDir_Up, ImGui::GetFrameHeight(), fl)) ImGui::End();
}

// Janelas e opcoes que a barra liga e desliga. false se o pedido nao for uma delas.
bool toggleFromMenu(Editor& ed, editor::mainmenu::Command c) {
  using C = editor::mainmenu::Command;
  bool* flag = nullptr;
  switch (c) {
    case C::ToggleHierarchy: flag = &ed.showHierarchy; break;
    case C::ToggleInspector: flag = &ed.showInspector; break;
    case C::ToggleConsole: flag = &ed.showConsole; break;
    case C::ToggleProfiler: flag = &ed.showProfiler; break;
    case C::ToggleEditMesh: flag = &ed.showEditMesh; break;
    case C::TogglePaintMesh: flag = &ed.showPaintMesh; break;
    case C::ToggleScene: flag = &ed.showScene; break;
    case C::ToggleGame: flag = &ed.showGame; break;
    case C::ToggleAssets: flag = &ed.showAssets; break;
    case C::ToggleScenes: flag = &ed.showScenes; break;
    case C::ToggleSettings: flag = &ed.showSettings; break;
    case C::ToggleNarrative: flag = &ed.showNarrative; break;
    case C::ToggleInputMap: flag = &ed.showInputMap; break;
    case C::ToggleSpriteUiDiagnostics: flag = &ed.showSpriteUiDiagnostics; break;
    case C::ToggleHelp: flag = &editor::helpState().open; break;
    case C::ToggleLockInterface: flag = &ed.lockInterface; break;
    case C::ToggleDockWithShift: flag = &ed.dockWithShift; break;
    case C::ToggleLogKeys: flag = &ed.logKeys; break;
    default: return false;
  }
  *flag = !*flag;
  return true;
}

// Executa o pedido de um item ou botao da barra de menus.
void applyMenuRequest(Editor& ed, bool& quit, const editor::mainmenu::Request& req) {
  using C = editor::mainmenu::Command;
  if (toggleFromMenu(ed, req.command)) return;
  switch (req.command) {
    case C::NewScene: ed.showScenes = true; editor::ui().openNewScene = true; break;
    case C::SaveScene: ed.saveScene(); break;
    case C::ImportAsset: SDL_ShowOpenFileDialog([](void*, const char* const* files, int) { if (files) for (; *files; ++files) editor::queueImport(*files); }, nullptr, nullptr, nullptr, 0, nullptr, true); break;
    case C::NewNarrative: ed.narrative.newGraph(); break;
    case C::SaveNarrative: ed.narrative.save(); break;
    case C::Settings: openSettings(ed, req.tab); break;
    case C::EditorInfo: ed.showEditorInfo = true; break;
    case C::ExportGame: ed.showExport = true; break;
    case C::Quit: quit = true; break;
    case C::Undo: ed.undo(); break;
    case C::Redo: ed.redo(); break;
    case C::ResetLayout: ed.resetLayout = true; break;
    case C::Play: ed.startPlay(); break;
    case C::Stop: ed.stopPlay(); break;
    case C::Pause: ed.paused = !ed.paused; break;
    default: break;
  }
}

// Barra de status: o texto e desenhado pelo painel SpriteUI (statusbar_panel.hpp), depois do ImGui.
// O ImGui so reserva a faixa de baixo para o encaixe das janelas nao passar por ela.
void reserveStatusBarStrip() {
  ImGuiViewport* vp = ImGui::GetMainViewport();
  const ImGuiWindowFlags fl = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;
  if (ImGui::BeginViewportSideBar("##status", vp, ImGuiDir_Down, ImGui::GetFrameHeight(), fl)) ImGui::End();
}

// Interface travada: tira de todos os nos do encaixe a permissao de desencaixar (abas/janelas viram fixas), de
// dividir o espaco e de redimensionar os divisores. Vale para os nos ja criados (percorre a arvore a cada quadro).
void applyLayoutLock(ImGuiDockNode* node, bool lock) {
  if (!node) return;
  const ImGuiDockNodeFlags f = ImGuiDockNodeFlags_NoUndocking | ImGuiDockNodeFlags_NoDockingSplit | ImGuiDockNodeFlags_NoResize;
  node->SharedFlags = lock ? (node->SharedFlags | f) : (node->SharedFlags & ~f);
  for (ImGuiDockNode* child : node->ChildNodes) applyLayoutLock(child, lock);
}

// Janela solta (fora do encaixe) que saiu da area do editor volta para dentro: pelo menos 80 px continuam visiveis
// (sem isso, ao reduzir a janela do editor ou arrastar para fora, a janela some e nao volta).
void keepFloatingWindowsOnScreen(const ImGuiViewport* vp) {
  ImGuiContext* ctx = ImGui::GetCurrentContext();
  constexpr float keep = 80.f;
  const float x0 = vp->WorkPos.x, y0 = vp->WorkPos.y, x1 = x0 + vp->WorkSize.x, y1 = y0 + vp->WorkSize.y;
  constexpr ImGuiWindowFlags skip = ImGuiWindowFlags_Popup | ImGuiWindowFlags_Tooltip | ImGuiWindowFlags_ChildWindow | ImGuiWindowFlags_ChildMenu;
  for (int i = 0; i < ctx->Windows.Size; ++i) {
    ImGuiWindow* w = ctx->Windows[i];
    if (!w->WasActive || w->Hidden || (w->Flags & skip) || w->DockNode || w->DockIsActive) continue;
    ImVec2 p = w->Pos;
    p.x = std::clamp(p.x, x0 - w->Size.x + keep, x1 - keep);
    p.y = std::clamp(p.y, y0, y1 - keep);
    if (p.x != w->Pos.x || p.y != w->Pos.y) ImGui::SetWindowPos(w->Name, p, ImGuiCond_Always);
  }
}

// Travar interface: cancela o arrasto de janela (barra de titulo ou aba) enquanto ele acontece, para que a janela
// nao saia do encaixe. Nao reencaixa nada: redocar todas as janelas no no raiz a cada quadro juntava todas as abas
// numa unica barra e desfazia o layout. Roda a cada quadro enquanto a opcao estiver ligada.
void lockWindows() {
  ImGuiContext& g = *ImGui::GetCurrentContext();
  if (g.MovingWindow) { ImGui::ClearActiveID(); g.MovingWindow = nullptr; }
}

// Layout padrao estilo Unity: hierarquia/cenas a esquerda, inspetor a direita, assets/console embaixo, Scene+Game no centro.
void buildDefaultLayout(ImGuiID dock, const ImVec2& size) {
  ImGui::DockBuilderRemoveNode(dock);
  ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dock, size);
  ImGuiID center = dock;
  const ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.18f, nullptr, &center);
  const ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.26f, nullptr, &center);
  ImGuiID rightBottom = 0;
  const ImGuiID rightTop = ImGui::DockBuilderSplitNode(right, ImGuiDir_Up, 0.58f, nullptr, &rightBottom);
  const ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.30f, nullptr, &center);
  ImGuiID leftBottom = 0;
  const ImGuiID leftTop = ImGui::DockBuilderSplitNode(left, ImGuiDir_Up, 0.60f, nullptr, &leftBottom);
  ImGui::DockBuilderDockWindow("###hier", leftTop);
  ImGui::DockBuilderDockWindow("###scenes", leftBottom);
  ImGui::DockBuilderDockWindow("###insp", rightTop);
  ImGui::DockBuilderDockWindow("###settings", rightBottom);
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
  j.set("graphicsApi", ed.apiRequested);
  j.set("resMode", ed.resMode); j.set("customW", ed.customW); j.set("customH", ed.customH); j.set("pixelFilter", ed.pixelFilter); j.set("fpsMode", ed.fpsMode);
  j.set("upscaleMode", ed.upscaleMode); j.set("aaMode", ed.aaMode);
  j.set("lockInterface", ed.lockInterface); j.set("dockWithShift", ed.dockWithShift);
  j.set("dockLockV2", true); j.set("dockLockV3", true);
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
      editor::I18n::get().selectOrDefault((*j)["language"].asString());   // sem idioma salvo: ingles
      ed.showGridScene = editor::ser::flag(*j, "showGridScene", true); ed.showGridGame = editor::ser::flag(*j, "showGridGame", false); ed.showGizmos = editor::ser::flag(*j, "showGizmos", true);
      ed.resMode = std::clamp(static_cast<int>(editor::ser::num(*j, "resMode", 0.f)), 0, 5);
      ed.customW = static_cast<int>(editor::ser::num(*j, "customW", 1280.f)); ed.customH = static_cast<int>(editor::ser::num(*j, "customH", 720.f));
      ed.pixelFilter = editor::ser::flag(*j, "pixelFilter", false); ed.fpsMode = std::clamp(static_cast<int>(editor::ser::num(*j, "fpsMode", 4.f)), 0, 4);
      ed.upscaleMode = std::clamp(static_cast<int>(editor::ser::num(*j, "upscaleMode", 2.f)), 0, 3); ed.aaMode = std::clamp(static_cast<int>(editor::ser::num(*j, "aaMode", 0.f)), 0, 1);
      // Arquivos anteriores podiam gravar a trava automaticamente; abre destravado uma vez para permitir resize/docking.
      ed.lockInterface = editor::ser::flag(*j, "dockLockV3", false)
          ? editor::ser::flag(*j, "lockInterface", false)
          : false;
      ed.dockWithShift = editor::ser::flag(*j, "dockWithShift", false);
      if (editor::ser::flag(*j, "renderScaleSet", false)) { ed.renderScaleSet = true; ed.renderScale = std::clamp(editor::ser::num(*j, "renderScale", 1.f), 0.25f, 2.f); }
      api = static_cast<int>(editor::ser::num(*j, "graphicsApi", 0.f)); lastScene = (*j)["lastScene"].asString(); lastProject = (*j)["lastProject"].asString();
    }
  }
  ed.theme.load("editor_theme.json");
  return api;
}

// Renderer SDL da interface (SpriteUI). E independente da API da Cena e do Jogo (Diligent): a interface usa o driver de
// GPU mais adequado (OpenGL primeiro, depois Direct3D, Metal e GLES) e so cai para software se nenhum abrir.
// Vulkan fica de fora porque a Cena ja o usa. Driver escolhido vai para o log.
SDL_Renderer* createEditorRenderer(SDL_Window* window, bool forceSoftware) {
  if (!window) return nullptr;
  if (forceSoftware) return SDL_CreateRenderer(window, "software");
  static constexpr const char* kPreferred[] = {"opengl", "direct3d11", "metal", "opengles2", "direct3d12"};
  auto accept = [&](const char* driver) -> SDL_Renderer* {
    SDL_Renderer* renderer = SDL_CreateRenderer(window, driver);
    if (renderer) tool::logInfo(std::string("Interface SpriteUI: driver de render ") + SDL_GetRendererName(renderer));
    return renderer;
  };
  for (const char* driver : kPreferred) {
    if (SDL_Renderer* renderer = accept(driver)) return renderer;
  }
  for (int i = 0; i < SDL_GetNumRenderDrivers(); ++i) {
    const char* driver = SDL_GetRenderDriver(i);
    if (!driver || std::strcmp(driver, "vulkan") == 0 || std::strcmp(driver, "software") == 0) continue;
    if (SDL_Renderer* renderer = accept(driver)) return renderer;
  }
  tool::logWarn("Interface SpriteUI: nenhum driver de GPU abriu; usando software (CPU)");
  return SDL_CreateRenderer(window, "software");
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
  ImFontConfig fontConfig;
  fontConfig.SizePixels = 18.f;
  fontConfig.OversampleH = 3;
  fontConfig.OversampleV = 2;
  fontConfig.PixelSnapH = false;
  std::error_code ec;
  for (const char* p : candidates)
    if (std::filesystem::exists(p, ec) && io.Fonts->AddFontFromFileTTF(p, fontConfig.SizePixels, &fontConfig, ranges.Data)) return;
  io.Fonts->AddFontDefault(&fontConfig);
}

int main(int argc, char** argv) {
  int maxFrames = 0; int startApi = -1; bool startPlaying = false, selfTest = false, playerMode = false;
  long long ecsCount = 0;   // --ecs N: benchmark de N esferas (ver abaixo)
  long long voxelCount = 0; // --voxel N: benchmark de N chunks de terreno (16x16x16 blocos cada)
  int benchFrames = 300;    // --bench-frames N: quadros medidos apos aquecimento
  int mapSide = 0;          // --map N: mapa de N x N chunks de terreno, centrado na origem (ver Criar > Test map of chunks)
  for (int i = 1; i + 1 < argc; ++i) if (std::strcmp(argv[i], "--map") == 0) mapSide = std::atoi(argv[i + 1]);
  for (int i = 1; i + 1 < argc; ++i) if (std::strcmp(argv[i], "--frames") == 0) maxFrames = std::atoi(argv[i + 1]);
  for (int i = 1; i + 1 < argc; ++i) if (std::strcmp(argv[i], "--ecs") == 0) ecsCount = std::atoll(argv[i + 1]);
  for (int i = 1; i + 1 < argc; ++i) if (std::strcmp(argv[i], "--voxel") == 0) voxelCount = std::atoll(argv[i + 1]);
  for (int i = 1; i + 1 < argc; ++i) if (std::strcmp(argv[i], "--bench-frames") == 0) benchFrames = std::clamp(std::atoi(argv[i + 1]), 1, 10000);
  const bool headless = maxFrames > 0 || ecsCount > 0 || voxelCount > 0;   // sem dialogo de projeto, sem gravar cena/configuracoes
  std::string projectArg;   // --project <pasta>: abre esse projeto direto, sem a janela de inicio
  for (int i = 1; i + 1 < argc; ++i) if (std::strcmp(argv[i], "--project") == 0) projectArg = argv[i + 1];
  if ((ecsCount > 0 || voxelCount > 0 || mapSide > 0) && projectArg.empty()) { std::fprintf(stderr, "--ecs/--voxel precisam de --project <pasta de teste> (nao grava o projeto)\n"); return 1; }
  for (int i = 1; i + 1 < argc; ++i) if (std::strcmp(argv[i], "--api") == 0) startApi = std::strcmp(argv[i + 1], "vulkan") == 0 ? 1 : std::strcmp(argv[i + 1], "opengl") == 0 ? 2 : std::strcmp(argv[i + 1], "d3d12") == 0 ? 3 : std::strcmp(argv[i + 1], "d3d11") == 0 ? 4 : 0;
  bool useImGuiStart = false;   // --imgui-start: dialogo de inicio antigo (ImGui); padrao: tela SpriteUI
  for (int i = 1; i < argc; ++i) { if (std::strcmp(argv[i], "--imgui-start") == 0) useImGuiStart = true; if (std::strcmp(argv[i], "--test-map") == 0) ed_::openTestMap(); }   // --test-map: captura de tela
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
  SDL_Renderer* renderer = createEditorRenderer(window, maxFrames > 0);
  if (!window || !renderer) { std::fprintf(stderr, "window/renderer: %s\n", SDL_GetError()); return 1; }
  SDL_SetRenderVSync(renderer, 0); // o limite so e ativado se o usuario selecionar VSync ou um FPS fixo

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImNodes::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
  // Clique (botao para baixo e para cima) processado no mesmo quadro: sem isso o ImGui espalha os dois por dois quadros e
  // o botao parece demorar para responder.
  io.ConfigInputTrickleEventQueue = false;
  io.IniFilename = headless ? nullptr : "editor_layout.ini";
  ImGui::StyleColorsDark();
  loadUiFont(io);
  ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
  ImGui_ImplSDLRenderer3_Init(renderer);

  Editor ed;
  editor::I18n::get().scan("assets/lang");
  editor::I18n::get().select(editor::kDefaultLanguage);   // ingles sem configuracao salva
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
    ed.console.emplace_back(l, std::string(m));
    if (ed.console.size() > editor::console_model::kMaxEntries) ed.console.pop_front();
    ++ed.consoleSerial;
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
    std::filesystem::path startRoot;
    bool startDone = false;
#ifdef TOOL_WITH_SPRITEUI
    if (!useImGuiStart) {
      ToolEngine::SpriteUi::Text::TruetypeAtlas atlas;
      if (editor::start::LoadTextAtlas("assets/fonts/Xmind-Regular.ttf", atlas, SDL_GetWindowDisplayScale(window))) {
        editor::start::Model model(lastProject, editor::defaultProjectsFolder(lastProject));
        startDone = editor::start::RunStartScreen(window, renderer, model, atlas,
                                                  editor::defaultProjectsFolder(lastProject), closed);
        startRoot = model.result();
      } else {
        std::fprintf(stderr, "Fonte da tela de inicio nao encontrada (assets/fonts/Xmind-Regular.ttf); usando o ImGui.\n");
      }
    }
#endif
    while (!startDone && !dlg.done && !closed) {
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
    ed.project.root = startDone ? startRoot : dlg.result;
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
  // e imprime media, p50, p95 e maximo. Sai sozinho ao fim de benchFrames quadros medidos.
  if (ecsCount > 0) ed.startEcsTest(ecsCount);
  if (mapSide > 0) ed.generateChunkMap(mapSide);
  if (voxelCount > 0) {   // chunks em grade quadrada, todos com terreno
    const long long side = static_cast<long long>(std::ceil(std::sqrt(static_cast<double>(voxelCount))));
    for (long long i = 0; i < voxelCount; ++i) ed.makeVoxel("Chunk", static_cast<int>(i % side), static_cast<int>(i / side), true);
  }
  if (!selectName.empty()) ed.scene.world().eachEntity([&](tool::Entity e) { auto* n = ed.scene.world().get<editor::Name>(e); if (n && selectName == n->text) ed.selected = e; });

  bool quit = false;
  int frame = 0, layoutFocus = 0;
  const Uint64 t0 = SDL_GetTicks();
  const bool bench = ecsCount > 0 || voxelCount > 0;
  std::vector<double> benchMs;
  std::array<double, static_cast<size_t>(editor::Zone::Count)> benchZoneMs{};
  // TOOL_BENCH_CSV=<arquivo>: grava cada quadro medido (ms total e de cada zona) para achar a origem dos picos.
  std::FILE* benchCsv = nullptr;
  if (const char* csvPath = std::getenv("TOOL_BENCH_CSV")) {
    benchCsv = std::fopen(csvPath, "w");
    if (benchCsv) { std::fprintf(benchCsv, "frame,total_ms"); for (int z = 0; z < static_cast<int>(editor::Zone::Count); ++z) std::fprintf(benchCsv, ",z%d", z); std::fprintf(benchCsv, "\n"); }
  }
  bool benchArmed = false;
#ifdef TOOL_WITH_SPRITEUI
  editor::SpriteUiDiagnostics spriteUiDiagnostics;
  // Fonte e renderizador unicos das janelas SpriteUI. Sem a fonte, as janelas ficam na versao ImGui.
  ToolEngine::SpriteUi::Text::TruetypeAtlas uiAtlas;
  editor::sui::SpriteUiSdlRenderer uiRenderer;
  // O modo --player tambem carrega a fonte: a janela do jogo desenha o texto da UI com ela (mesma fonte do editor).
  const bool fontReady = !headless &&
      editor::sui::LoadTextAtlas("assets/fonts/Xmind-Regular.ttf", uiAtlas, SDL_GetWindowDisplayScale(window)) &&
      uiRenderer.uploadAtlas(renderer, uiAtlas);
  const bool uiReady = fontReady && !playerMode;
  // Janelas do editor em SpriteUI. Sem a fonte, elas nao aparecem (nao ha caminho ImGui de reserva).
  std::unique_ptr<editor::profiler_panel::ProfilerPanel> profPanel;
  if (uiReady) profPanel = std::make_unique<editor::profiler_panel::ProfilerPanel>(uiAtlas, uiRenderer);
  std::unique_ptr<editor::statusbar::StatusBarPanel> statusPanel;
  if (uiReady) statusPanel = std::make_unique<editor::statusbar::StatusBarPanel>(uiAtlas, uiRenderer);
  const bool statusSpriteOn = statusPanel != nullptr;
  std::unique_ptr<editor::mainmenu::MainMenuPanel> menuPanel;
  if (uiReady) menuPanel = std::make_unique<editor::mainmenu::MainMenuPanel>(uiAtlas, uiRenderer);
  const bool menuSpriteOn = menuPanel != nullptr;
  std::unique_ptr<editor::help::HelpPanel> helpPanel;
  if (uiReady) helpPanel = std::make_unique<editor::help::HelpPanel>(uiAtlas, uiRenderer);
  std::unique_ptr<editor::testmap::TestMapPanel> testMapPanel;
  if (uiReady) testMapPanel = std::make_unique<editor::testmap::TestMapPanel>(uiAtlas, uiRenderer);
  bool testMapGenerate = false;   // o botao "Generate map" so marca; o mapa e gerado depois do quadro
  std::unique_ptr<editor::console_panel::ConsolePanel> consolePanel;
  if (uiReady) consolePanel = std::make_unique<editor::console_panel::ConsolePanel>(uiAtlas, uiRenderer);
  SDL_Cursor* spriteTextCursor = uiReady ? SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_TEXT) : nullptr;
  bool spriteTextCursorActive = false;
  bool cursorWarningLogged = false;
  if (uiReady && !spriteTextCursor) tool::logWarn(std::string("Could not create text cursor: ") + SDL_GetError());
  std::unique_ptr<editor::hierarchy_panel::HierarchyPanel> hierPanel;
  if (uiReady) hierPanel = std::make_unique<editor::hierarchy_panel::HierarchyPanel>(uiAtlas, uiRenderer);
  gHierarchySpriteOn = hierPanel != nullptr;
  std::unique_ptr<editor::settings_panel::SettingsPanel> settingsPanel;
  if (uiReady) settingsPanel = std::make_unique<editor::settings_panel::SettingsPanel>(uiAtlas, uiRenderer);
  gSettingsSpriteOn = settingsPanel != nullptr;
  std::unique_ptr<editor::inspector_panel::InspectorPanel> inspPanel;
  if (uiReady) inspPanel = std::make_unique<editor::inspector_panel::InspectorPanel>(uiAtlas, uiRenderer);
  gInspectorPanel = inspPanel.get();
#ifdef TOOL_WITH_SPRITEUI
  std::unique_ptr<editor::scene_view_panel::ScenePanel> scenePanel;
  std::unique_ptr<editor::game_view_panel::GamePanel> gamePanel;
  if (uiReady) scenePanel = std::make_unique<editor::scene_view_panel::ScenePanel>(uiAtlas, uiRenderer);
  if (fontReady) gamePanel = std::make_unique<editor::game_view_panel::GamePanel>(uiAtlas, uiRenderer);
  gScenePanel = scenePanel.get();
  gGamePanel = gamePanel.get();
#endif
  gInspectorSpriteOn = inspPanel != nullptr;
#else
  const bool statusSpriteOn = false;
  const bool menuSpriteOn = false;
#endif
  while (!quit) {
    const Uint64 frameStart = SDL_GetTicksNS();
    if (ed.fpsDirty && renderer) { SDL_SetRenderVSync(renderer, ed.fpsMode == 0 && !headless ? 1 : 0); ed.fpsDirty = false; }
    tool::debug().beginFrame();
    ed.prof.beginFrame();
    SDL_Event e;
    // Ocioso: espera ate 16 ms pelo proximo evento (em vez de girar o quadro sem parar). O evento volta para a fila.
    // A espera vem ANTES da zona de medicao: nao e trabalho e nao deve aparecer como "Events + input".
    if (ed.viewsIdle() && !headless) {
      const auto waitStart = editor::FrameProfiler::Clock::now();
      SDL_Event waited;
      if (SDL_WaitEventTimeout(&waited, 16)) SDL_PushEvent(&waited);
      ed.prof.addWait(std::chrono::duration<float, std::milli>(editor::FrameProfiler::Clock::now() - waitStart).count());
    }
    editor::ZoneTimer zone(ed.prof, editor::Zone::Events);
    int nEvents = 0;
#ifdef TOOL_WITH_SPRITEUI
    spriteUiDiagnostics.beginFrame();
#endif
    while (SDL_PollEvent(&e)) {
      // Movimento do mouse sem botao (so passar por cima) nao conta como atividade: senao a cena nunca fica ociosa
      // e cada pixel de mouse refaz as duas vistas por 3 quadros. A interface continua redesenhando a cada quadro.
      if (!(e.type == SDL_EVENT_MOUSE_MOTION && e.motion.state == 0 && !ed.freeFly)) ++nEvents;
#ifdef TOOL_WITH_SPRITEUI
      const bool spriteUiHandled =
          spriteUiDiagnostics.handleEvent(e, ed.showSpriteUiDiagnostics);
      const bool profHandled = profPanel && profPanel->handleEvent(e, ed.showProfiler, gProfilerFrame);
      const bool helpHandled = helpPanel && helpPanel->handleEvent(e, window, editor::helpState().open);   // janela Help SpriteUI
      const editor::testmap::Action testMapAction = testMapPanel ? testMapPanel->handleEvent(e, editor::testMapState().open, editor::testMapState().side) : editor::testmap::Action::None;
      if (testMapAction == editor::testmap::Action::Generate) testMapGenerate = true;
      const bool menuHandled = menuPanel && menuPanel->handleEvent(e);
      const bool consoleHandled = consolePanel && consolePanel->handleEvent(e, ed.showConsole && gConsoleFrame);
      const bool hierHandled = hierPanel && hierPanel->handleEvent(e);
      const bool setHandled = settingsPanel && settingsPanel->handleEvent(e);
      const bool inspHandled = inspPanel && inspPanel->handleEvent(e);
      forwardViewEvent(e);   // paineis Scene e Jogo: veem o evento, sem consumir
      if (!spriteUiHandled && !profHandled && !helpHandled && !consoleHandled && !hierHandled && !setHandled && !inspHandled && !menuHandled && testMapAction == editor::testmap::Action::None) ImGui_ImplSDL3_ProcessEvent(&e);
#else
      ImGui_ImplSDL3_ProcessEvent(&e);
#endif
      if (ed.logKeys && (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP))
        tool::logInfo(std::string("[keys] ") + (e.type == SDL_EVENT_KEY_DOWN ? "down " : "up ") + SDL_GetKeyName(e.key.key) + (e.key.repeat ? " (repeat)" : "") + (ImGui::GetIO().WantTextInput ? " [text field]" : ""));
      if (ed.logKeys && e.type == SDL_EVENT_TEXT_INPUT) tool::logInfo(std::string("[keys] text '") + (e.text.text ? e.text.text : "") + "'");
      if (e.type == SDL_EVENT_MOUSE_MOTION && ed.freeFly) { ed.mouseDX += e.motion.xrel; ed.mouseDY += e.motion.yrel; } // navegacao livre: mouse capturado
      if (e.type == SDL_EVENT_QUIT || (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && e.window.windowID == SDL_GetWindowID(window))) quit = true;
      if (e.type == SDL_EVENT_DROP_FILE && e.drop.data) editor::queueImport(e.drop.data); // arrastar arquivos do sistema para a janela
    }
    ed.idleFrames = nEvents > 0 ? 0 : ed.idleFrames + 1;
    // Botao que o sistema ja soltou, mas o ImGui ainda considera preso (a soltura se perdeu, p.ex. fora da janela):
    // corrige aqui, senao qualquer movimento do mouse continua arrastando a camera ou a interface.
    if (!headless) {
      const Uint32 mb = SDL_GetMouseState(nullptr, nullptr);
      ImGuiIO& inp = ImGui::GetIO();
      if (inp.MouseDown[0] && !(mb & SDL_BUTTON_LMASK)) inp.AddMouseButtonEvent(0, false);
      if (inp.MouseDown[1] && !(mb & SDL_BUTTON_RMASK)) inp.AddMouseButtonEvent(1, false);
      if (inp.MouseDown[2] && !(mb & SDL_BUTTON_MMASK)) inp.AddMouseButtonEvent(2, false);
    }
    zone.restart(editor::Zone::Update);
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
    zone.stop();
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
    beginViewsFrame();
    if (playerMode) {
      drawGameWindow(ed, renderer, true);
    } else {
      if (menuSpriteOn) reserveMenuBarStrip();
      if (statusSpriteOn) reserveStatusBarStrip();
      if (!focusWin.empty() && frame == 24) ImGui::SetWindowFocus(("###" + focusWin).c_str());
      const ImGuiID dock = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
      if (ed.resetLayout || ImGui::DockBuilderGetNode(dock) == nullptr || ImGui::DockBuilderGetNode(dock)->IsLeafNode()) {
        buildDefaultLayout(dock, ImGui::GetMainViewport()->WorkSize);
        ed.resetLayout = false; layoutFocus = 12;
      }
      ImGui::GetIO().ConfigDockingWithShift = ed.dockWithShift;          // encaixe so com Shift, se ligado
      applyLayoutLock(ImGui::DockBuilderGetNode(dock), ed.lockInterface); // travar layout, se ligado (cada quadro)
      if (ed.lockInterface) lockWindows();
      keepFloatingWindowsOnScreen(ImGui::GetMainViewport());
      const bool focusTabs = layoutFocus > 0 && --layoutFocus == 0; // seleciona as abas ativas do layout padrao (Inspector, Scene, Assets)
      if (ed.showHierarchy) drawHierarchy(ed);
      if (focusTabs) ImGui::SetNextWindowFocus();
      if (ed.showInspector) drawInspector(ed);
      if (profPanel) drawProfilerFrame(ed);
      if (consolePanel) drawConsoleFrame(ed);
      ed_::scriptWindowUi(ed); // janela de edicao de script (fora do Inspector)
      if (focusTabs) ImGui::SetNextWindowFocus();
      if (ed.showScene) drawSceneWindow(ed, renderer);
      if (ed.showGame) drawGameWindow(ed, renderer, false);
      if (ed.showEditMesh) drawFeatureStub(ed, T("Edit Mesh"), T("Edit the 3D model of an object: move vertices, faces and edges. In development."), &ed.showEditMesh);
      if (ed.showPaintMesh) drawFeatureStub(ed, T("Paint Mesh"), T("Paint colors and textures directly on the 3D model. In development."), &ed.showPaintMesh);
      if (focusTabs) ImGui::SetNextWindowFocus();
      if (ed.showAssets) editor::drawAssets(ed);
      if (ed.showScenes) editor::drawScenes(ed);
      if (ed.showSettings) drawSettings(ed);
      if (ed.showNarrative) ed.narrative.draw(&ed.showNarrative);
      if (ed.showInputMap) editor::drawInputMap(ed, &ed.showInputMap);
      if (ed.showEditorInfo) editor::drawEditorInfo(ed, &ed.showEditorInfo);
      if (ed.showExport) editor::drawExport(ed, &ed.showExport);
    }
    { // "Panels" = tempo dos paineis SEM as renderizacoes 3D (essas ja foram contadas nas suas proprias zonas)
      const float total = std::chrono::duration<float, std::milli>(editor::FrameProfiler::Clock::now() - panelsStart).count();
      const float inner = ed.prof.cur[static_cast<int>(editor::Zone::SceneRender)] + ed.prof.cur[static_cast<int>(editor::Zone::GameRender)] + ed.prof.cur[static_cast<int>(editor::Zone::PostFx)] + ed.prof.cur[static_cast<int>(editor::Zone::Sky)];
      ed.prof.add(editor::Zone::Hierarchy, std::max(0.f, total - inner));
    }
    zone.restart(editor::Zone::Ui);
    ImGui::Render();
    if (!headless && !playerMode && (ed.themeDirty || ed.langDirty || ed.settingsDirty) && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) { saveSettings(ed); ed.themeDirty = ed.langDirty = ed.settingsDirty = false; }

    { const ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg); SDL_SetRenderDrawColor(renderer, static_cast<Uint8>(bg.x * 255), static_cast<Uint8>(bg.y * 255), static_cast<Uint8>(bg.z * 255), 255); }
    SDL_RenderClear(renderer);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
    drawViewPanels(ed, renderer);   // Scene e Jogo em SpriteUI: por cima do ImGui
#ifdef TOOL_WITH_SPRITEUI
    const double nowSec = static_cast<double>(SDL_GetTicksNS()) * 1e-9;
    if (profPanel && ed.showProfiler && gProfilerFrame) {
      if (profPanel->due(nowSec)) profPanel->refresh(nowSec, ed.prof, fillProfilerExtras(ed));   // so 4 vezes por segundo
      if (!profPanel->draw(renderer, gProfilerRect)) {
        tool::logError(std::string("Profiler SpriteUI render failed: ") + SDL_GetError());
        ed.showProfiler = false;
      }
    }
    if (helpPanel) {   // por cima do Profiler; o "?" e F1 abrem o topico em helpState().pending
      editor::HelpState& help = editor::helpState();
      if (!help.pending.empty()) {
        helpPanel->openTopic(help.pending);
        help.pending.clear();
      }
      if (!helpPanel->draw(renderer, window, help.open, editor::helpIsPt())) {
        tool::logError(std::string("Help SpriteUI render failed: ") + SDL_GetError());
        helpPanel.reset();
      }
    }
    if (testMapPanel) {
      editor::TestMapState& testMap = editor::testMapState();
      if (!testMapPanel->draw(renderer, window, testMap.open, testMap.side, editor::I18n::get().code())) {
        tool::logError(std::string("Test map SpriteUI render failed: ") + SDL_GetError());
        testMapPanel.reset();
      }
      if (testMapGenerate) {
        testMapGenerate = false;
        const int made = ed.generateChunkMap(testMap.side);
        tool::logInfo("Test map: " + std::to_string(made) + " chunks");
      }
    }
    if (hierPanel) {   // Hierarchy: lista desenhada pelo painel SpriteUI, por cima da moldura ImGui
      const bool shown = ed.showHierarchy && gHierarchyFrame;
      editor::hierarchy_panel::Requests requests;
      const editor::hierarchy_panel::Source source = shown ? hierarchySource(ed) : editor::hierarchy_panel::Source{};
      if (!hierPanel->draw(renderer, window, gHierarchyRect, shown, source, requests)) {
        tool::logError(std::string("Hierarchy SpriteUI render failed: ") + SDL_GetError());
        hierPanel.reset();
        gHierarchySpriteOn = false;
      }
      applyHierarchyRequests(ed, requests);
    }
    if (inspPanel) {   // Inspector: componentes do objeto, no bloco reservado pelo ImGui (drawInspectorBlock)
      const bool shown = ed.showInspector && gInspectorFrame;
      if (!inspPanel->draw(renderer, gInspectorOrigin, gInspectorVisible, shown)) {
        tool::logError(std::string("Inspector SpriteUI render failed: ") + SDL_GetError());
        inspPanel.reset();
        gInspectorPanel = nullptr;
        gInspectorSpriteOn = false;
      }
    }
    if (settingsPanel) {   // Configuracoes: faixa de abas e aba Graficos por cima da janela ImGui
      const bool shown = ed.showSettings && gSettingsFrame;
      editor::settings_changes::Requests requests;
      const editor::settings_panel::Values values = shown ? settingsValues(ed) : editor::settings_panel::Values{};
      if (!settingsPanel->draw(renderer, gSettingsRect, shown, values, requests)) {
        tool::logError(std::string("Settings SpriteUI render failed: ") + SDL_GetError());
        settingsPanel.reset();
        gSettingsSpriteOn = false;
      }
      SettingsEditorTarget target{ed};
      editor::settings_changes::ApplyChanges(target, requests);
    }
    if (menuPanel) {
      std::vector<editor::mainmenu::Request> menuRequests;
      menuPanel->setState(menuStateOf(ed), [](const char* text) { return T(text); });
      if (!menuPanel->draw(renderer, window, ImGui::GetFrameHeight(), menuRequests)) {
        tool::logError(std::string("Menu bar SpriteUI render failed: ") + SDL_GetError());
        menuPanel.reset();
      }
      for (const editor::mainmenu::Request& request : menuRequests) applyMenuRequest(ed, quit, request);
    }
    if (consolePanel) {
      const editor::console_panel::Source source{ed.console, ed.consoleSerial, tool::debug().count(tool::Severity::Error),
                                                 tool::debug().count(tool::Severity::Warning)};
      editor::console_panel::Requests requests;
      if (!consolePanel->draw(renderer, window, ed.showConsole && gConsoleFrame, gConsoleRect, source, requests)) {
        tool::logError(std::string("Console SpriteUI render failed: ") + SDL_GetError());
        ed.showConsole = false;
      }
      applyConsoleRequests(ed, requests);
    }
    if (statusPanel) {   // por ultimo: a barra fica por cima do Profiler e da Ajuda (mesmo renderer, depois do RenderDrawData)
      const ImVec2 viewport = ImGui::GetMainViewport()->Size;
      if (!statusPanel->draw(renderer, statusBarInfo(ed), [](const char* s) { return T(s); }, nowSec, viewport.x, viewport.y, ImGui::GetFrameHeight())) {
        tool::logError(std::string("Status bar SpriteUI render failed: ") + SDL_GetError());
        statusPanel.reset();
      }
    }
    if (!headless && !playerMode && ed.showSpriteUiDiagnostics) {
      const auto stats = ed.prof.stats();
      if (!spriteUiDiagnostics.draw(
              renderer, window, ed.showSpriteUiDiagnostics, stats.fps,
              ed.prof.cur[static_cast<int>(editor::Zone::SceneRender)],
              ed.prof.cur[static_cast<int>(editor::Zone::PostFx)])) {
        tool::logError(std::string("SpriteUI diagnostics render failed: ") + SDL_GetError());
        ed.showSpriteUiDiagnostics = false;
      }
    }
#endif
#ifdef TOOL_WITH_SPRITEUI
    float mouseX = 0.f, mouseY = 0.f;
    SDL_GetMouseState(&mouseX, &mouseY);
    const bool textFieldHovered =
        (hierPanel && ed.showHierarchy && gHierarchyFrame && hierPanel->wantsTextCursor(mouseX, mouseY)) ||
        (consolePanel && ed.showConsole && gConsoleFrame && consolePanel->wantsTextCursor(mouseX, mouseY)) ||
        (inspPanel && ed.showInspector && gInspectorFrame && inspPanel->wantsTextCursor(mouseX, mouseY)) ||
        (helpPanel && editor::helpState().open && helpPanel->wantsTextCursor(mouseX, mouseY));
    if (textFieldHovered && spriteTextCursor) {
      if (!SDL_SetCursor(spriteTextCursor) && !cursorWarningLogged) {
        tool::logWarn(std::string("Could not activate text cursor: ") + SDL_GetError());
        cursorWarningLogged = true;
      }
      spriteTextCursorActive = true;
    } else if (spriteTextCursorActive) {
      SDL_SetCursor(SDL_GetDefaultCursor());
      spriteTextCursorActive = false;
    }
#endif
    zone.restart(editor::Zone::Present);
    SDL_RenderPresent(renderer);
    zone.stop();
    tool::debug().endFrame();
    { static editor::PerfWarnings perfWarn; static unsigned seenLoads = 0;
      if (ed.sceneLoads != seenLoads) { seenLoads = ed.sceneLoads; perfWarn.quietFor(240); } // cena nova: compila shaders e cria pipelines
      perfWarn.check(ed.prof); }   // avisos de recurso no Console (a cada 30 quadros)
    if (ed.fpsMode >= 1 && ed.fpsMode <= 3 && !headless) { // limite manual de FPS (30/60/120): espera o que sobrar do quadro
      static const Uint64 target[4] = {0, 1000000000ull / 30, 1000000000ull / 60, 1000000000ull / 120};
      const Uint64 spent = SDL_GetTicksNS() - frameStart;
      if (spent < target[ed.fpsMode]) {
        const auto waitStart = editor::FrameProfiler::Clock::now();
        SDL_DelayNS(target[ed.fpsMode] - spent);
        ed.prof.addWait(std::chrono::duration<float, std::milli>(editor::FrameProfiler::Clock::now() - waitStart).count());
      }
    }
    ++frame; if (maxFrames > 0 && frame >= maxFrames) quit = true;
    if (bench) {
      bool armed = false;
      ed.scene.world().each<editor::SpawnerComp>([&](tool::Entity, editor::SpawnerComp& s) { if (s.armed) armed = true; });
      if (armed) benchArmed = true;
      if (voxelCount > 0 && frame == 10) benchArmed = true;   // chunks nao tem nascimento: mede a partir do 10o quadro
      if (benchArmed && !armed) {   // nascimento terminou: mede os proximos quadros
        benchMs.push_back(static_cast<double>(SDL_GetTicksNS() - frameStart) / 1e6);
        for (size_t z = 0; z < benchZoneMs.size(); ++z) benchZoneMs[z] += ed.prof.cur[z];
        if (benchCsv) { std::fprintf(benchCsv, "%zu,%.3f", benchMs.size(), benchMs.back()); for (size_t z = 0; z < benchZoneMs.size(); ++z) std::fprintf(benchCsv, ",%.3f", ed.prof.cur[z]); std::fprintf(benchCsv, "\n"); }
        if (benchMs.size() >= static_cast<size_t>(benchFrames)) {
          std::vector<double> s = benchMs; std::sort(s.begin(), s.end());
          if (benchCsv) { std::fclose(benchCsv); benchCsv = nullptr; }
          double sum = 0; for (double v : s) sum += v;
          std::printf("ECS bench: %s, %zu bodies, %zu frames\n", ecsCount > 0 ? (std::to_string(ecsCount) + " spheres").c_str() : (std::to_string(voxelCount) + " voxel chunks").c_str(), ed.play.ents.size(), s.size());
          std::printf("ECS bench: avg %.2f ms (%.1f fps), p50 %.2f ms, p95 %.2f ms, max %.2f ms\n", sum / s.size(), 1000.0 * s.size() / sum,
                      s[s.size() / 2], s[s.size() * 95 / 100], s.back());
          for (int z = 0; z < static_cast<int>(editor::Zone::Count); ++z) std::printf("ECS bench:   %-34s %7.2f ms\n", editor::zoneName(static_cast<editor::Zone>(z)), benchZoneMs[static_cast<size_t>(z)] / s.size());
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
  SDL_SetCursor(SDL_GetDefaultCursor());
  ImNodes::DestroyContext(); ImGui::DestroyContext();
#ifdef TOOL_WITH_SPRITEUI
  profPanel.reset();
  menuPanel.reset();
  statusPanel.reset();
  helpPanel.reset();
  testMapPanel.reset();
  if (spriteTextCursor) SDL_DestroyCursor(spriteTextCursor);
  uiRenderer.destroy();   // a textura do atlas e destruida antes do renderer SDL
#endif
  SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit();
  std::printf("editor exited after %d frames\n", frame);
  return 0;
}
