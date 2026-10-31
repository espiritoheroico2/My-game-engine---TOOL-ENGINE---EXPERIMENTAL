// ToolEngine Editor - janelas e paineis novos da v0.5: Help ("?"), Physics (Inspector + Settings), Tools, Input Map,
// desenho dos colliders na Scene e "Edit external" (editor de codigo externo).
// Tudo aqui e interface: os dados vivem em components.hpp / input_map.hpp / editor_state.hpp.
#pragma once
#include "editor_state.hpp"
#include "help_content.hpp"
#include "i18n.hpp"
#include "viewport_tools.hpp"

#include "imgui.h"
#include "imgui_internal.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace editor {

// ====================================================================================================== HELP
struct HelpState { bool open = false; std::string topic = "start"; std::string pending; };
inline HelpState& helpState() { static HelpState h; return h; }
inline void openHelp(const std::string& topic) { auto& h = helpState(); h.open = true; h.pending = topic; }

// Icone "?" no canto da barra de titulo da janela ATUAL (chame logo depois do Begin). Clique = abre o Help no topico.
inline void helpIcon(const char* topic) {
  ImGuiWindow* win = ImGui::GetCurrentWindow(); if (!win || win->Collapsed) return;
  const float h = ImGui::GetFrameHeight();
  const ImVec2 mn{win->Pos.x + win->Size.x - h * 2.f - 4.f, win->Pos.y + 2.f}, mx{mn.x + h - 4.f, mn.y + h - 4.f};
  ImDrawList* dl = win->DrawList; dl->PushClipRect(win->Pos, {win->Pos.x + win->Size.x, win->Pos.y + win->Size.y}, false);
  const bool hot = ImGui::IsMouseHoveringRect(mn, mx, false) && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
  const ImVec2 c{(mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f};
  dl->AddCircleFilled(c, (mx.x - mn.x) * 0.5f, hot ? ImGui::GetColorU32(ImGuiCol_ButtonHovered) : ImGui::GetColorU32(ImGuiCol_Button));
  const ImVec2 ts = ImGui::CalcTextSize("?");
  dl->AddText({c.x - ts.x * 0.5f, c.y - ts.y * 0.5f}, ImGui::GetColorU32(ImGuiCol_Text), "?");
  dl->PopClipRect();
  if (hot) { ImGui::SetTooltip("%s", T("Help for this window (F1)")); if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) openHelp(topic); }
}

inline bool helpIsPt() { return I18n::get().code().rfind("pt", 0) == 0; }

inline void drawHelp(Editor& ed, bool* open) {
  (void)ed;
  HelpState& hs = helpState();
  if (!hs.pending.empty()) { hs.topic = hs.pending; hs.pending.clear(); ImGui::SetNextWindowFocus(); }
  ImGui::SetNextWindowSize({820, 520}, ImGuiCond_FirstUseEver);
  if (!ImGui::Begin(W("Help", "help").c_str(), open)) { ImGui::End(); return; }
  const auto& topics = helpTopics(); const bool pt = helpIsPt();
  static char filter[48] = "";
  ImGui::BeginChild("helplist", ImVec2(230, 0), ImGuiChildFlags_Borders);
  ImGui::SetNextItemWidth(-1); ImGui::InputTextWithHint("##hf", T("search..."), filter, sizeof filter);
  const std::string f = filter;
  auto lower = [](std::string s) { for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; };
  const std::string fl = lower(f);
  const char* group = "";
  for (const HelpTopic& t : topics) {
    const std::string title = pt ? t.titlePt : t.titleEn;
    if (!fl.empty() && lower(title).find(fl) == std::string::npos && lower(pt ? t.bodyPt : t.bodyEn).find(fl) == std::string::npos) continue;
    if (std::strcmp(group, t.group) != 0) { group = t.group; ImGui::SeparatorText(T(group)); }
    if (ImGui::Selectable(title.c_str(), hs.topic == t.id)) hs.topic = t.id;
  }
  ImGui::EndChild(); ImGui::SameLine();
  ImGui::BeginChild("helpbody", ImVec2(0, 0), ImGuiChildFlags_Borders);
  const HelpTopic* cur = &topics.front(); for (const HelpTopic& t : topics) if (hs.topic == t.id) cur = &t;
  ImGui::PushFont(nullptr); ImGui::TextColored({0.55f, 0.8f, 1.f, 1.f}, "%s", pt ? cur->titlePt : cur->titleEn); ImGui::PopFont();
  ImGui::Separator();
  // renderiza a marcacao simples
  const std::string body = pt ? cur->bodyPt : cur->bodyEn;
  size_t pos = 0; std::vector<std::string> code; bool inCode = false; int codeId = 0;
  while (pos <= body.size()) {
    size_t e = body.find('\n', pos); if (e == std::string::npos) e = body.size();
    const std::string line = body.substr(pos, e - pos); pos = e + 1;
    if (line.rfind("```", 0) == 0) {
      if (!inCode) { inCode = true; code.clear(); }
      else {
        inCode = false; const float lh = ImGui::GetTextLineHeightWithSpacing();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.09f, 0.11f, 1.f));
        ImGui::BeginChild(("code" + std::to_string(codeId++)).c_str(), ImVec2(0, std::min(360.f, lh * static_cast<float>(code.size()) + 16.f)), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
        for (auto& c : code) ImGui::TextColored({0.75f, 0.95f, 0.75f, 1.f}, "%s", c.c_str());
        ImGui::EndChild(); ImGui::PopStyleColor();
        std::string all; for (auto& c : code) all += c + "\n";
        if (ImGui::SmallButton((std::string(T("Copy code")) + "##c" + std::to_string(codeId)).c_str())) ImGui::SetClipboardText(all.c_str());
        ImGui::Spacing();
      }
      continue;
    }
    if (inCode) { code.push_back(line); continue; }
    if (line.rfind("# ", 0) == 0) { ImGui::Spacing(); ImGui::SeparatorText(line.c_str() + 2); }
    else if (line.rfind("- ", 0) == 0) { ImGui::Bullet(); ImGui::TextWrapped("%s", line.c_str() + 2); }
    else if (!line.empty()) ImGui::TextWrapped("%s", line.c_str());
  }
  ImGui::EndChild();
  ImGui::End();
}

// ====================================================================================================== EDIT EXTERNAL
// Abre um arquivo do projeto no editor de codigo escolhido em Settings > Project. NUNCA usa shell nem "abrir com o programa
// padrao": no Windows o padrao de .js e o Windows Script Host, que EXECUTARIA o script. Roda `editor arquivo` direto.
inline bool openInExternalEditor(Editor& ed, const std::string& rel) {
  const std::filesystem::path abs = ed.project.resolve(rel);
  if (abs.empty() || !std::filesystem::exists(abs)) { ed.statusLine = "File not found"; return false; }
  std::string cmd = ed.projSettings.externalEditor;
  if (cmd.empty()) {
#if defined(_WIN32)
    cmd = "notepad.exe";
#elif defined(__APPLE__)
    cmd = "open";
#else
    cmd = "xdg-open";
#endif
  }
  if (cmd.find_first_of("\r\n\"") != std::string::npos) { ed.statusLine = "Invalid external editor command"; return false; }
  const std::string path = abs.string();
  const char* args[] = {cmd.c_str(), path.c_str(), nullptr};
  SDL_Process* p = SDL_CreateProcess(args, false);
  if (!p) { ed.statusLine = std::string("Could not start the external editor: ") + SDL_GetError(); tool::logError(ed.statusLine); return false; }
  SDL_DestroyProcess(p); // solta o handle; o editor continua rodando sozinho
  ed.statusLine = "Opened in external editor: " + rel; return true;
}

// ====================================================================================================== PHYSICS (Inspector)
inline void actionCombo(Editor& ed, const char* label, char* buf, size_t cap, int player, int kind) {
  const size_t si = static_cast<size_t>(std::clamp(player, 1, 8) - 1);
  if (ImGui::BeginCombo(label, buf[0] ? buf : T("(none)"))) {
    if (si < ed.inputMap.schemes.size()) for (const MapRow& r : ed.inputMap.schemes[si].rows) if (r.kind == kind && ImGui::Selectable(r.action, std::strcmp(r.action, buf) == 0)) ser::copyStr(buf, cap, r.action);
    ImGui::EndCombo();
  }
}

// Coloca o proximo botao na mesma linha se couber; senao quebra a linha (listas de presets em paineis estreitos).
inline void sameLineIfFits(const char* nextLabel) {
  const ImGuiStyle& st = ImGui::GetStyle(); const float w = ImGui::CalcTextSize(nextLabel).x + st.FramePadding.x * 2.f;
  if (ImGui::GetItemRectMax().x + st.ItemSpacing.x + w < ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x) ImGui::SameLine();
}

inline void physicsUi(Editor& ed) {
  tool::World& w = ed.scene.world();
  auto* p = w.get<PhysicsComp>(ed.selected);
  if (!p) return;
  ImGui::SeparatorText("Physics (Newtoon)");
  const std::string bodies = std::string(T("Static")) + '\0' + T("Dynamic") + '\0' + T("Kinematic") + '\0';
  ImGui::Combo(T("Body"), &p->body, bodies.c_str());
  const std::string cols = std::string(T("Auto (from mesh)")) + '\0' + T("Box") + '\0' + T("Sphere") + '\0';
  ImGui::Combo(T("Collider"), &p->collider, cols.c_str());
  if (p->body == 1) {
    ImGui::DragFloat(T("Mass (kg)"), &p->mass, 0.05f, 0.001f, 100000.f);
    ImGui::SliderFloat(T("Gravity scale"), &p->gravityScale, -3.f, 3.f);
    ImGui::SliderFloat(T("Linear drag"), &p->linearDamping, 0.f, 5.f); ImGui::SliderFloat(T("Angular drag"), &p->angularDamping, 0.f, 5.f);
    ImGui::Checkbox(T("Use gravity"), &p->useGravity); ImGui::SameLine(); ImGui::Checkbox(T("Freeze rotation"), &p->freezeRotation); ImGui::SameLine(); ImGui::Checkbox("CCD", &p->ccd);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("Continuous collision detection: fast small objects cannot pass through walls."));
  }
  ImGui::SliderFloat(T("Friction"), &p->friction, 0.f, 2.f); ImGui::SliderFloat(T("Bounciness"), &p->bounciness, 0.f, 1.f);
  ImGui::Checkbox(T("Trigger (detects, does not push)"), &p->trigger);
  if (p->body == 1) {
    ImGui::Checkbox(T("Vector physics (own gravity)"), &p->overrideGravity);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("Replaces the world gravity for this object only."));
    if (p->overrideGravity) {
      ImGui::DragFloat3(T("Gravity vector (m/s2)"), p->gravity, 0.05f, -1000.f, 1000.f);
      struct Pre { const char* n; float v[3]; } pre[] = {{"Down", {0, -9.81f, 0}}, {"Up", {0, 9.81f, 0}}, {"Left", {-9.81f, 0, 0}}, {"Right", {9.81f, 0, 0}}, {"Forward", {0, 0, -9.81f}}, {"Back", {0, 0, 9.81f}}, {"Zero", {0, 0, 0}}};
      for (size_t i = 0; i < 7; ++i) { if (i) sameLineIfFits(T(pre[i].n)); if (ImGui::SmallButton(T(pre[i].n))) std::memcpy(p->gravity, pre[i].v, sizeof p->gravity); }
    } else ImGui::TextDisabled("%s", F("World gravity: %.2f, %.2f, %.2f", static_cast<double>(ed.sceneInfo.physics.gravity[0]), static_cast<double>(ed.sceneInfo.physics.gravity[1]), static_cast<double>(ed.sceneInfo.physics.gravity[2])).c_str());
  }
  if (ed.playing && ed.play.built) { auto it = ed.play.indexOf.find(ed.selected.index); if (it != ed.play.indexOf.end()) { const auto& rb = ed.play.rb[it->second];
    ImGui::TextDisabled("%s", F("Play: velocity %.2f, %.2f, %.2f  %s%s", static_cast<double>(rb.velocity.x), static_cast<double>(rb.velocity.y), static_cast<double>(rb.velocity.z), rb.grounded ? "grounded " : "", rb.isSleeping ? "asleep" : "").c_str()); } }
  if (ImGui::SmallButton(T("Remove Physics"))) w.remove<PhysicsComp>(ed.selected);
}

// ====================================================================================================== UI CANVAS / PARTICULAS (Inspector)
inline void uiCanvasUi(Editor& ed) {
  tool::World& w = ed.scene.world(); auto* u = w.get<UiCanvas>(ed.selected); if (!u) return;
  ImGui::SeparatorText(T("Game UI (camera layer)"));
  ImGui::Checkbox(T("Visible"), &u->visible);
  // escolhe a camera: vazio = todas as cameras Principais
  const auto* curName = u->camera;
  if (ImGui::BeginCombo(T("Camera"), curName[0] ? curName : T("(all Principal cameras)"))) {
    if (ImGui::Selectable(T("(all Principal cameras)"), !curName[0])) u->camera[0] = 0;
    w.each<CameraComp>([&](tool::Entity ce, CameraComp&) { if (auto* n = w.get<Name>(ce)) if (ImGui::Selectable(n->text, std::string(n->text) == u->camera)) ser::copyStr(u->camera, sizeof u->camera, n->text); });
    ImGui::EndCombo();
  }
  static const char* kinds[] = {"Text", "Panel", "Button", "Bar"};
  int removeAt = -1;
  for (int i = 0; i < u->count; ++i) {
    UiElem& it = u->items[i]; ImGui::PushID(i);
    const bool open = ImGui::TreeNodeEx("##el", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth, "%s: %s", T(kinds[std::clamp(it.kind, 0, 3)]), it.text);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 30.f); if (ImGui::SmallButton("x")) removeAt = i;
    if (open) {
      const std::string kc = std::string(T("Text")) + '\0' + T("Panel") + '\0' + T("Button") + '\0' + T("Bar") + '\0';
      ImGui::Combo(T("Type"), &it.kind, kc.c_str());
      ImGui::InputText(T("Text (use {variable})"), it.text, sizeof it.text);
      if (it.kind == 2) ImGui::InputText(T("Event name (on click)"), it.action, sizeof it.action);
      if (it.kind == 3) { ImGui::InputText(T("Variable"), it.action, sizeof it.action); ImGui::DragFloat(T("Full at"), &it.maxValue, 1.f, 0.0001f, 1e9f); }
      ImGui::SliderFloat2(T("Position (0..1)"), &it.x, 0.f, 1.f); ImGui::SliderFloat2(T("Size (0..1)"), &it.w, 0.f, 1.f);
      ImGui::ColorEdit4(T("Color"), it.color); ImGui::SliderFloat(T("Font scale"), &it.fontScale, 0.5f, 4.f);
      ImGui::TreePop();
    }
    ImGui::PopID();
  }
  if (removeAt >= 0) { for (int i = removeAt; i + 1 < u->count; ++i) u->items[i] = u->items[i + 1]; --u->count; }
  if (u->count < kMaxUi) {
    if (ImGui::Button(T("+ Text"))) { UiElem& e = u->items[u->count++]; e = UiElem{}; e.y = 0.05f + 0.07f * static_cast<float>(u->count); ser::copyStr(e.text, sizeof e.text, "Score: {score}"); }
    ImGui::SameLine(); if (ImGui::Button(T("+ Button"))) { UiElem& e = u->items[u->count++]; e = UiElem{}; e.kind = 2; e.x = 0.4f; e.y = 0.8f; e.color[0] = 0.9f; e.color[1] = 0.8f; e.color[2] = 0.3f; ser::copyStr(e.text, sizeof e.text, "Play"); ser::copyStr(e.action, sizeof e.action, "play_clicked"); }
    ImGui::SameLine(); if (ImGui::Button(T("+ Bar"))) { UiElem& e = u->items[u->count++]; e = UiElem{}; e.kind = 3; e.w = 0.3f; e.h = 0.03f; e.y = 0.92f; ser::copyStr(e.action, sizeof e.action, "health"); e.color[1] = 0.8f; e.color[2] = 0.2f; }
    ImGui::SameLine(); if (ImGui::Button(T("+ Panel"))) { UiElem& e = u->items[u->count++]; e = UiElem{}; e.kind = 1; e.color[0] = e.color[1] = e.color[2] = 0.1f; e.color[3] = 0.6f; ser::copyStr(e.text, sizeof e.text, ""); }
  }
  ImGui::TextDisabled("%s", T("Appears over the Game view of the chosen camera. Button clicks (while playing) send their event name."));
  if (ImGui::SmallButton(T("Remove UI"))) w.remove<UiCanvas>(ed.selected);
}

inline void particleUi(Editor& ed) {
  tool::World& w = ed.scene.world(); auto* p = w.get<ParticleComp>(ed.selected); if (!p) return;
  ImGui::SeparatorText(T("Particles"));
  ImGui::Checkbox(T("Emitting"), &p->emitting);
  ImGui::DragInt(T("Max particles"), &p->maxParticles, 1.f, 1, 5000); ImGui::DragFloat(T("Rate (per second)"), &p->rate, 0.5f, 0.f, 5000.f); ImGui::DragFloat(T("Lifetime (s)"), &p->lifetime, 0.05f, 0.05f, 60.f);
  ImGui::DragFloat(T("Speed"), &p->speed, 0.05f, 0.f, 500.f); ImGui::SliderFloat(T("Spread (deg)"), &p->spread, 0.f, 180.f);
  ImGui::DragFloat(T("Size start"), &p->size, 0.005f, 0.001f, 50.f); ImGui::DragFloat(T("Size end"), &p->sizeEnd, 0.005f, 0.f, 50.f);
  ImGui::ColorEdit3(T("Color start"), p->color); ImGui::ColorEdit3(T("Color end"), p->colorEnd); ImGui::SliderFloat(T("Opacity"), &p->alpha, 0.f, 1.f);
  ImGui::SliderFloat(T("Gravity scale (world physics)"), &p->gravityScale, -3.f, 3.f);
  ImGui::SliderFloat(T("Bounce on floor"), &p->bounce, 0.f, 1.f); if (p->bounce > 0.f) ImGui::DragFloat(T("Floor Y"), &p->floorY, 0.05f);
  ImGui::TextDisabled("%s", T("Emits along the object's +Y axis. Drawn as soft dots over the view (no depth test)."));
  if (ImGui::SmallButton(T("Remove Particles"))) w.remove<ParticleComp>(ed.selected);
}

// ====================================================================================================== ADD RESOURCE (Inspector)
// Um unico botao com a lista de recursos que o objeto pode receber (fisica, tools, luz, audio, camera...).
inline void addResourceUi(Editor& ed) {
  tool::World& w = ed.scene.world();
  const tool::Entity sel = ed.selected;
  static const char* names[] = {"Jump", "Move", "Follow Target", "Spin", "On Collision", "Script library"};
  static const char* tips[] = {"Jumps when the action is pressed (needs a Dynamic Physics body).", "Moves with two axes from the Input Map.", "Follows another object by name, keeping an offset (cameras).",
                               "Rotates continuously.", "Reacts when this object touches another.", "Attaches an extra .js library/addon to this object's code."};
  ImGui::Spacing();
  if (ImGui::Button(T("+ Add Resource"), {-1.f, 0.f})) ImGui::OpenPopup("addres");
  if (!ImGui::BeginPopup("addres")) return;
  auto* tl = w.get<ToolsComp>(sel);
  ImGui::SeparatorText(T("Physics"));
  if (!w.has<PhysicsComp>(sel)) { if (ImGui::MenuItem(T("Physics body (Newtoon)"))) { PhysicsComp pc; if (auto* m = w.get<MeshRenderer>(sel)) if (m->shape == 2) pc.body = 0; w.add<PhysicsComp>(sel, pc); } }
  else ImGui::TextDisabled("%s", T("Physics already added"));
  ImGui::SeparatorText(T("Tools (behaviours)"));
    for (int k = 0; k < static_cast<int>(ToolKind::Count); ++k) {
      if (ImGui::MenuItem(T(names[k]))) {
        if (!tl) { w.add<ToolsComp>(sel, ToolsComp{}); tl = w.get<ToolsComp>(sel); }
        if (tl->count < kMaxTools) {
          ToolItem& it = tl->items[tl->count++]; it = ToolItem{}; it.kind = k;
          if (k == 0) { ser::copyStr(it.action, sizeof it.action, "jump"); it.a = 1.5f; }
          if (k == 1) { ser::copyStr(it.action, sizeof it.action, "moveX"); ser::copyStr(it.target, sizeof it.target, "moveY"); it.a = 5.f; it.b = 1.f; }
          if (k == 2) { it.b = 4.f; it.c = 8.f; it.d = 6.f; it.mode = 1; }
          if (k == 3) it.b = 90.f;
          if (k == 5) { // cria um arquivo de biblioteca
            static int n = 0; const std::string rel = "Assets/Scripts/Libs/Lib" + std::to_string(++n + static_cast<int>(ed.created)) + ".js";
            if (scriptPathOk(ed.project, rel) && writeScript(ed.project, rel, "// Library for this object. Functions here become part of its code once the script runtime is on.\nexport function hello(obj) {\n  console.log(\"hello from a library\", obj.name);\n}\n")) ser::copyStr(it.action, sizeof it.action, rel);
          }
        }
      }
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T(tips[k]));
    }
  ImGui::SeparatorText(T("Rendering and world"));
  if (!w.has<MeshRenderer>(sel) && ImGui::MenuItem(T("Mesh (cube)"))) w.add<MeshRenderer>(sel, MeshRenderer{});
  if (!w.has<LightComp>(sel) && ImGui::MenuItem(T("Point Light"))) { tool::Light l; l.type = tool::LightType::Point; l.range = 12.f; l.intensity = 18.f; l.color = {1.f, 0.7f, 0.4f}; w.add<LightComp>(sel, LightComp{l}); }
  if (!w.has<CameraComp>(sel) && ImGui::MenuItem(T("Camera"))) w.add<CameraComp>(sel);
  if (!w.has<AudioSource>(sel) && ImGui::MenuItem(T("Audio Source"))) w.add<AudioSource>(sel);
  if (!w.has<ParticleComp>(sel) && ImGui::MenuItem(T("Particle System"))) w.add<ParticleComp>(sel);
  if (!w.has<UiCanvas>(sel) && ImGui::MenuItem(T("Game UI (canvas)"))) { UiCanvas u; UiElem& e = u.items[u.count++]; ser::copyStr(e.text, sizeof e.text, "Hello UI"); w.add<UiCanvas>(sel, u); }
  // Recursos planejados: aparecem bloqueados (cinza) ate serem implementados.
  ImGui::SeparatorText(T("Coming in a future update (locked)"));
  ImGui::BeginDisabled();
  ImGui::MenuItem(T("Textures / Normal map")); ImGui::MenuItem(T("Material (.tmat) and custom shader")); ImGui::MenuItem(T("Point light shadows"));
  ImGui::MenuItem(T("Compress as NTC")); ImGui::MenuItem(T("Ray tracing (DXR)"));
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", T("Planned: see docs/TEXTURES.md, docs/RENDER_PERF.md, docs/DX12_DXR.md"));
  ImGui::EndPopup();
}

// ====================================================================================================== TOOLS (Inspector)
inline void toolsUi(Editor& ed) {
  tool::World& w = ed.scene.world();
  auto* tl = w.get<ToolsComp>(ed.selected);
  if (!tl) return;
  ImGui::SeparatorText("Tools");
  static const char* names[] = {"Jump", "Move", "Follow Target", "Spin", "On Collision", "Script library"};
  int removeAt = -1;
  for (int i = 0; i < tl->count; ++i) {
    ToolItem& it = tl->items[i]; ImGui::PushID(i);
    const int k = std::clamp(it.kind, 0, static_cast<int>(ToolKind::Count) - 1);
    ImGui::Checkbox("##en", &it.enabled); ImGui::SameLine();
    const bool openNode = ImGui::TreeNodeEx(T(names[k]), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 40.f); if (ImGui::SmallButton("x")) removeAt = i;
    if (openNode) {
      if (k <= 1) ImGui::SliderInt(T("Player (controller map)"), &it.player, 1, 8);
      switch (static_cast<ToolKind>(k)) {
        case ToolKind::Jump: actionCombo(ed, T("Action"), it.action, sizeof it.action, it.player, 0); ImGui::DragFloat(T("Height (m)"), &it.a, 0.05f, 0.1f, 50.f);
          { bool air = it.b != 0.f; if (ImGui::Checkbox(T("Allow air jump"), &air)) it.b = air ? 1.f : 0.f; } break;
        case ToolKind::Move: actionCombo(ed, T("X axis"), it.action, sizeof it.action, it.player, 1); actionCombo(ed, T("Y axis (forward)"), it.target, sizeof it.target, it.player, 1);
          ImGui::DragFloat(T("Speed (m/s)"), &it.a, 0.05f, 0.1f, 100.f);
          { bool face = it.b != 0.f; if (ImGui::Checkbox(T("Face direction"), &face)) it.b = face ? 1.f : 0.f; bool rel = it.mode == 1; ImGui::SameLine(); if (ImGui::Checkbox(T("Camera relative"), &rel)) it.mode = rel ? 1 : 0; } break;
        case ToolKind::FollowTarget: ImGui::InputText(T("Target object name"), it.target, sizeof it.target); ImGui::DragFloat3(T("Offset"), &it.a, 0.05f); ImGui::SliderFloat(T("Smoothing"), &it.d, 0.f, 20.f);
          { bool look = it.mode == 1; if (ImGui::Checkbox(T("Look at target"), &look)) it.mode = look ? 1 : 0; } break;
        case ToolKind::Spin: ImGui::DragFloat3(T("Degrees / second (X Y Z)"), &it.a, 1.f); break;
        case ToolKind::OnCollision: {
          ImGui::InputText(T("Only objects with tag"), it.target, sizeof it.target);
          const std::string modes = std::string(T("Log to Console")) + '\0' + T("Destroy the other object") + '\0' + T("Destroy this object") + '\0' + T("Bounce this object") + '\0';
          ImGui::Combo(T("Action"), &it.mode, modes.c_str()); if (it.mode == 3) ImGui::DragFloat(T("Bounce speed (m/s)"), &it.a, 0.1f, 0.1f, 100.f);
          if (!w.has<PhysicsComp>(ed.selected)) ImGui::TextColored({1.f, .7f, .3f, 1.f}, "%s", T("This tool needs a Physics component on the object."));
        } break;
        case ToolKind::ScriptLib: ImGui::InputText(T("Library file"), it.action, sizeof it.action);
          if (it.action[0] && ImGui::SmallButton(T("Edit external"))) openInExternalEditor(ed, it.action);
          ImGui::TextDisabled("%s", T("Runs once the script runtime exists.")); break;
        default: break;
      }
      ImGui::TreePop();
    }
    ImGui::PopID();
  }
  if (removeAt >= 0) { for (int i = removeAt; i + 1 < tl->count; ++i) tl->items[i] = tl->items[i + 1]; --tl->count; }
}

// ====================================================================================================== SETTINGS > PHYSICS
inline void drawPhysicsTab(Editor& ed) {
  PhysicsSettings& ps = ed.sceneInfo.physics;
  ImGui::TextWrapped("%s", T("World physics of this scene (saved with the scene). Objects with 'Vector physics' ignore the world gravity."));
  ImGui::Checkbox(T("Physics enabled"), &ps.enabled);
  ImGui::SeparatorText(T("World gravity (m/s2) - any axis"));
  ImGui::DragFloat3(T("Gravity X / Y / Z"), ps.gravity, 0.05f, -1000.f, 1000.f);
  struct Pre { const char* n; float v[3]; } pre[] = {{"Earth", {0, -9.81f, 0}}, {"Moon", {0, -1.62f, 0}}, {"Mars", {0, -3.71f, 0}}, {"Zero-G", {0, 0, 0}}, {"Sideways (+X)", {9.81f, 0, 0}}, {"Toward -Z", {0, 0, -9.81f}}, {"Upside down", {0, 9.81f, 0}}};
  for (size_t i = 0; i < 7; ++i) { if (i) sameLineIfFits(T(pre[i].n)); if (ImGui::SmallButton(T(pre[i].n))) std::memcpy(ps.gravity, pre[i].v, sizeof ps.gravity); }
  ImGui::SeparatorText(T("Simulation"));
  ImGui::SliderInt(T("Solver iterations"), &ps.solverIterations, 1, 30); ImGui::SliderInt(T("Steps per second"), &ps.stepHz, 15, 240);
  ImGui::TextDisabled("%s", T("More iterations stack better; more steps per second is more precise. Both cost CPU."));
  ImGui::Checkbox(T("Draw colliders in the Scene view"), &ps.drawColliders);
  if (ed.playing && ed.play.built) { const auto& s = ed.play.world.stats; ImGui::SeparatorText(T("Live"));
    ImGui::Text("%s", F("bodies %d   awake %d   pairs %d   contacts %d   steps/frame %d", s.bodies, s.awake, s.pairs, s.contacts, s.steps).c_str()); }
}

// ====================================================================================================== COLLIDERS NA SCENE
inline void drawPhysicsShapes(Editor& ed, ImDrawList* dl, const ViewXf& v) {
  if (!ed.sceneInfo.physics.drawColliders) return;
  tool::World& w = ed.scene.world();
  w.each<PhysicsComp, Transform>([&](tool::Entity e, PhysicsComp& p, Transform& t) {
    const Newtoon::Collider c = ed.colliderFor(e); const Newtoon::Mat3 R = eulerToQuat(t.rot).ToMat3();
    const Newtoon::Vec3 off = R * c.offset; const tool::Vec3 ctr{t.pos[0] + off.x, t.pos[1] + off.y, t.pos[2] + off.z};
    const ImU32 col = p.trigger ? IM_COL32(230, 90, 230, 200) : p.body == 0 ? IM_COL32(90, 160, 255, 170) : p.body == 2 ? IM_COL32(255, 210, 80, 200) : IM_COL32(110, 230, 120, 190);
    if (c.type == Newtoon::ShapeType::Sphere) {
      const tool::Vec3 ax[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
      for (int a = 0; a < 3; ++a) { const tool::Vec3 u = ax[(a + 1) % 3], q = ax[(a + 2) % 3];
        tool::Vec3 prev = ctr + u * c.radius; for (int s = 1; s <= 24; ++s) { const float an = static_cast<float>(s) * 6.2831853f / 24.f; const tool::Vec3 cur = ctr + u * (std::cos(an) * c.radius) + q * (std::sin(an) * c.radius); line3(dl, v, prev, cur, col); prev = cur; } }
      return;
    }
    tool::Vec3 corner[8];
    for (int i = 0; i < 8; ++i) { const Newtoon::Vec3 l{(i & 1 ? 1.f : -1.f) * c.extents.x, (i & 2 ? 1.f : -1.f) * c.extents.y, (i & 4 ? 1.f : -1.f) * c.extents.z}; const Newtoon::Vec3 wv = R * l; corner[i] = {ctr.x + wv.x, ctr.y + wv.y, ctr.z + wv.z}; }
    for (int i = 0; i < 8; ++i) for (int b = 0; b < 3; ++b) if (!(i & (1 << b))) line3(dl, v, corner[i], corner[i | (1 << b)], col);
  });
}

// ====================================================================================================== INPUT MAP (janela)
inline const std::vector<std::pair<int, std::string>>& keyList() {
  static std::vector<std::pair<int, std::string>> l;
  if (l.empty()) for (int i = 4; i < static_cast<int>(tool::kMaxKeys); ++i) { const char* n = SDL_GetScancodeName(static_cast<SDL_Scancode>(i)); if (n && *n) l.push_back({i, n}); }
  return l;
}

inline void drawInputMap(Editor& ed, bool* open) {
  ImGui::SetNextWindowSize({980, 520}, ImGuiCond_FirstUseEver);
  if (!ImGui::Begin(W("Input Map", "inmap").c_str(), open)) { ImGui::End(); return; }
  helpIcon("inputmap");
  ImGuiIO& io = ImGui::GetIO();
  if (!ed.playing) { ed.sampler.sample(ed.rawInput, !io.WantTextInput); ed.input.update(ed.rawInput); } // leitura ao vivo para os indicadores
  static bool dirty = false; bool changed = false;
  static int listenScheme = -1, listenRow = -1, listenField = -1; static bool prevKeys[tool::kMaxKeys] = {}; static bool prevBtn[tool::kMaxPads][16] = {};
  ImGui::Text("%s", F("Gamepads detected: %d", ed.sampler.connected()).c_str());
  for (int i = 0; i < ed.sampler.connected(); ++i) { ImGui::SameLine(); ImGui::TextDisabled("[%d] %s", i + 1, ed.sampler.padName(static_cast<size_t>(i)).c_str()); }
  if (ImGui::Button(T("+ Add controller map"))) { if (ed.inputMap.schemes.size() < kMaxSchemes) { Scheme s = ed.inputMap.schemes.empty() ? InputMap::defaults().schemes[0] : ed.inputMap.schemes[0];
      std::snprintf(s.name, sizeof s.name, "Player %zu", ed.inputMap.schemes.size() + 1); s.pad = std::min(3, static_cast<int>(ed.inputMap.schemes.size())); ed.inputMap.schemes.push_back(s); changed = true; } }
  ImGui::SameLine(); if (ImGui::Button(T("Reset to defaults"))) { ed.inputMap = InputMap::defaults(); changed = true; }
  ImGui::SameLine(); ImGui::TextDisabled("%s", T("Saved in Project/input_map.json"));
  // captura "Listen": proxima tecla / botao / eixo vira a vinculacao
  tool::RawInput raw = ed.rawInput; bool anyKeyNow = false; int newKey = -1, newBtn = -1, newAxis = -1;
  if (listenScheme >= 0) {
    for (int k = 4; k < static_cast<int>(tool::kMaxKeys); ++k) { if (raw.keys[k] && !prevKeys[k] && newKey < 0) newKey = k; anyKeyNow = anyKeyNow || raw.keys[k]; }
    for (size_t p = 0; p < tool::kMaxPads; ++p) { for (int b = 0; b < 15; ++b) { if (raw.pads[p].buttons[b] && !prevBtn[p][b] && newBtn < 0) newBtn = b; prevBtn[p][b] = raw.pads[p].buttons[b]; }
      for (int a = 0; a < 6; ++a) if (std::fabs(raw.pads[p].axes[a]) > 0.7f && newAxis < 0) newAxis = a; }
  }
  for (int k = 0; k < static_cast<int>(tool::kMaxKeys); ++k) prevKeys[k] = raw.keys[k];
  int delScheme = -1;
  for (size_t si = 0; si < ed.inputMap.schemes.size(); ++si) {
    Scheme& s = ed.inputMap.schemes[si]; ImGui::PushID(static_cast<int>(si));
    const bool openS = ImGui::CollapsingHeader((std::string(s.name) + "###s").c_str(), ImGuiTreeNodeFlags_DefaultOpen);
    if (openS) {
      ImGui::SetNextItemWidth(160); changed |= ImGui::InputText(T("Name"), s.name, sizeof s.name);
      ImGui::SameLine(); ImGui::SetNextItemWidth(150);
      const std::string pads = std::string(T("Keyboard only")) + '\0' + "Gamepad 1" + '\0' + "Gamepad 2" + '\0' + "Gamepad 3" + '\0' + "Gamepad 4" + '\0';
      int pi = s.pad + 1; if (ImGui::Combo(T("Controller"), &pi, pads.c_str())) { s.pad = pi - 1; changed = true; }
      if (ed.inputMap.schemes.size() > 1) { ImGui::SameLine(); if (ImGui::SmallButton(T("Delete map"))) delScheme = static_cast<int>(si); }
      if (ImGui::BeginTable("rows", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollX | ImGuiTableFlags_BordersInnerV)) {
        const ImGuiTableColumnFlags fx = ImGuiTableColumnFlags_WidthFixed;
        ImGui::TableSetupColumn(T("Action"), fx, 130.f); ImGui::TableSetupColumn(T("Type"), fx, 60.f); ImGui::TableSetupColumn(T("Keyboard"), fx, 250.f); ImGui::TableSetupColumn(T("Gamepad"), fx, 400.f);
        ImGui::TableSetupColumn(T("Now"), fx, 60.f); ImGui::TableSetupColumn("", fx, 30.f); ImGui::TableHeadersRow();
        int delRow = -1;
        for (size_t ri = 0; ri < s.rows.size(); ++ri) {
          MapRow& r = s.rows[ri]; ImGui::PushID(static_cast<int>(ri)); ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0); ImGui::SetNextItemWidth(-1); changed |= ImGui::InputText("##a", r.action, sizeof r.action);
          ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(r.kind == 0 ? T("Button") : T("Axis"));
          ImGui::TableSetColumnIndex(2);
          auto keyCombo = [&](const char* id, int& code, int field, const char* prefix) {
            ImGui::SetNextItemWidth(110); std::string cur = code > 0 ? SDL_GetScancodeName(static_cast<SDL_Scancode>(code)) : T("(none)");
            if (ImGui::BeginCombo(id, (std::string(prefix) + cur).c_str())) { if (ImGui::Selectable(T("(none)"), code == 0)) { code = 0; changed = true; }
              for (auto& kv : keyList()) if (ImGui::Selectable(kv.second.c_str(), kv.first == code)) { code = kv.first; changed = true; }
              ImGui::EndCombo(); }
            ImGui::SameLine(); const bool lis = listenScheme == static_cast<int>(si) && listenRow == static_cast<int>(ri) && listenField == field;
            if (ImGui::SmallButton(lis ? (std::string(T("Press a key...")) + std::string("##l") + id).c_str() : (std::string(T("Listen")) + "##l" + id).c_str())) { listenScheme = lis ? -1 : static_cast<int>(si); listenRow = static_cast<int>(ri); listenField = field; }
            if (lis && newKey >= 0) { code = newKey; changed = true; listenScheme = -1; }
          };
          if (r.kind == 0) keyCombo("##k", r.key, 0, ""); else { keyCombo("##kn", r.keyNeg, 1, "- "); keyCombo("##kp", r.key, 0, "+ "); }
          ImGui::TableSetColumnIndex(3); ImGui::SetNextItemWidth(170);
          if (r.kind == 0) {
            const char* cur = r.padButton >= 0 ? kPadButtonNames[static_cast<size_t>(r.padButton)] : T("(none)");
            if (ImGui::BeginCombo("##pb", cur)) { if (ImGui::Selectable(T("(none)"), r.padButton < 0)) { r.padButton = -1; changed = true; }
              for (int b = 0; b < 15; ++b) if (ImGui::Selectable(kPadButtonNames[static_cast<size_t>(b)], r.padButton == b)) { r.padButton = b; changed = true; }
              ImGui::EndCombo(); }
            ImGui::SameLine(); const bool lis = listenScheme == static_cast<int>(si) && listenRow == static_cast<int>(ri) && listenField == 2;
            if (ImGui::SmallButton((std::string(lis ? T("Press...") : T("Listen")) + "##lb").c_str())) { listenScheme = lis ? -1 : static_cast<int>(si); listenRow = static_cast<int>(ri); listenField = 2; }
            if (lis && newBtn >= 0) { r.padButton = newBtn; changed = true; listenScheme = -1; }
          } else {
            const char* cur = r.padAxis >= 0 ? kPadAxisNames[static_cast<size_t>(r.padAxis)] : T("(none)");
            if (ImGui::BeginCombo("##pa", cur)) { if (ImGui::Selectable(T("(none)"), r.padAxis < 0)) { r.padAxis = -1; changed = true; }
              for (int a = 0; a < 6; ++a) if (ImGui::Selectable(kPadAxisNames[static_cast<size_t>(a)], r.padAxis == a)) { r.padAxis = a; changed = true; }
              ImGui::EndCombo(); }
            ImGui::SameLine(); changed |= ImGui::Checkbox(T("Invert"), &r.padInvert);
            const bool lis = listenScheme == static_cast<int>(si) && listenRow == static_cast<int>(ri) && listenField == 3; ImGui::SameLine();
            if (ImGui::SmallButton((std::string(lis ? T("Move stick...") : T("Listen")) + "##la").c_str())) { listenScheme = lis ? -1 : static_cast<int>(si); listenRow = static_cast<int>(ri); listenField = 3; }
            if (lis && newAxis >= 0) { r.padAxis = newAxis; changed = true; listenScheme = -1; }
          }
          ImGui::TableSetColumnIndex(4);
          if (r.kind == 0) { const bool d = ed.inputMap.down(ed.input, si, r.action); ImGui::TextColored(d ? ImVec4(0.3f, 1.f, 0.4f, 1.f) : ImVec4(0.5f, 0.5f, 0.5f, 1.f), "%s", d ? "ON" : "-"); }
          else ImGui::Text("%+.2f", static_cast<double>(ed.inputMap.axis(ed.input, si, r.action)));
          ImGui::TableSetColumnIndex(5); if (ImGui::SmallButton("x")) delRow = static_cast<int>(ri);
          ImGui::PopID();
        }
        if (delRow >= 0) { s.rows.erase(s.rows.begin() + delRow); changed = true; }
        ImGui::EndTable();
      }
      if (s.rows.size() < kMaxRows) {
        if (ImGui::SmallButton(T("+ Add button"))) { MapRow r; std::snprintf(r.action, sizeof r.action, "button%zu", s.rows.size() + 1); r.kind = 0; s.rows.push_back(r); changed = true; }
        ImGui::SameLine(); if (ImGui::SmallButton(T("+ Add axis"))) { MapRow r; std::snprintf(r.action, sizeof r.action, "axis%zu", s.rows.size() + 1); r.kind = 1; s.rows.push_back(r); changed = true; }
      }
    }
    ImGui::PopID();
  }
  if (delScheme >= 0) { ed.inputMap.schemes.erase(ed.inputMap.schemes.begin() + delScheme); changed = true; listenScheme = -1; }
  if (listenScheme >= 0 && ImGui::IsKeyPressed(ImGuiKey_Escape)) listenScheme = -1;
  if (changed) { ed.applyInputMap(); dirty = true; }
  if (dirty && !ImGui::IsAnyItemActive()) { ed.inputMap.save(ed.project); dirty = false; }
  ImGui::End();
}

} // namespace editor
