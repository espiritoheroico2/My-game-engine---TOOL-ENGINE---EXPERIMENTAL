// ToolEngine Editor - janelas "Editor info" (dados do estudio e do jogo) e "Export game" (exportar a pasta).
// O "Editor info" trabalha com um rascunho: so grava quando clicar em "Save info", assim um campo pela metade nao vai para o projeto.
#pragma once
#include "editor_state.hpp"
#include "export.hpp"
#include "i18n.hpp"
#include "project_settings.hpp"

#include "imgui.h"
#include <SDL3/SDL.h>

#include <cstdio>
#include <string>

namespace editor {

// Nome do executavel do editor neste sistema (o que o CMake gera).
inline std::string editorExeName() {
#if defined(_WIN32)
  return "ToolEditor.exe";
#else
  return "ToolEditor";
#endif
}

inline fs::path editorFolder() {
  const char* base = SDL_GetBasePath();
  return base ? fs::path(base) : fs::current_path();
}

inline void drawEditorInfo(Editor& ed, bool* open) {
  struct Draft { char product[101], creator[101], studio[101], country[101], city[101], version[33], description[401], copyright[201], website[201]; };
  static Draft d{};
  static bool wasOpen = false;
  static std::string msg;
  if (*open && !wasOpen) { // acabou de abrir: copia o que esta salvo
    const auto& s = ed.projSettings;
    auto cp = [](char* dst, size_t n, const std::string& v) { std::snprintf(dst, n, "%s", v.c_str()); };
    cp(d.product, sizeof d.product, s.projectName); cp(d.creator, sizeof d.creator, s.author); cp(d.studio, sizeof d.studio, s.info.studio);
    cp(d.country, sizeof d.country, s.info.country); cp(d.city, sizeof d.city, s.info.city); cp(d.version, sizeof d.version, s.info.version);
    cp(d.description, sizeof d.description, s.info.description); cp(d.copyright, sizeof d.copyright, s.info.copyright); cp(d.website, sizeof d.website, s.info.website);
    msg.clear();
  }
  wasOpen = *open;
  if (!*open) return;
  ImGui::SetNextWindowSize({540, 600}, ImGuiCond_FirstUseEver);
  if (!ImGui::Begin(W("Editor info", "editorinfo").c_str(), open)) { ImGui::End(); return; }
  ImGui::TextWrapped("%s", T("Fill in these details before exporting. They go into the exported game (export.json). Fields marked * are required."));
  ImGui::SeparatorText(T("Game"));
  ImGui::InputText(T("Product name *"), d.product, sizeof d.product);
  ImGui::InputText(T("Version (e.g. 1.0.0) *"), d.version, sizeof d.version);
  ImGui::InputTextMultiline(T("Description *"), d.description, sizeof d.description, ImVec2(-1, 72));
  ImGui::SeparatorText(T("Who made it"));
  ImGui::InputText(T("Creator *"), d.creator, sizeof d.creator);
  ImGui::InputText(T("Studio *"), d.studio, sizeof d.studio);
  ImGui::InputText(T("Country *"), d.country, sizeof d.country);
  ImGui::InputText(T("City"), d.city, sizeof d.city);
  ImGui::InputText(T("Website"), d.website, sizeof d.website);
  ImGui::InputText(T("Copyright (empty = automatic)"), d.copyright, sizeof d.copyright);
  ImGui::TextDisabled("%s", T("Empty copyright becomes 'Copyright (c) <year> <studio>'."));

  ProjectSettings draft; // mesma regra da exportacao, aplicada ao rascunho
  draft.projectName = d.product; draft.author = d.creator; draft.info.studio = d.studio; draft.info.country = d.country;
  draft.info.version = d.version; draft.info.description = d.description;
  const auto miss = draft.missingInfo();
  ImGui::Separator();
  if (!miss.empty()) {
    std::string list; for (auto& m : miss) { if (!list.empty()) list += ", "; list += T(m.c_str()); }
    ImGui::TextColored({1.f, .8f, .3f, 1.f}, "%s %s", T("Missing:"), list.c_str());
  } else {
    ImGui::TextColored({.4f, .9f, .5f, 1.f}, "%s", T("All required fields are filled."));
  }
  if (ImGui::Button(T("Save info"), {-1, 0})) {
    auto& s = ed.projSettings;
    s.projectName = d.product; s.author = d.creator;
    s.info.studio = d.studio; s.info.country = d.country; s.info.city = d.city; s.info.version = d.version;
    s.info.description = d.description; s.info.copyright = d.copyright; s.info.website = d.website;
    msg = s.save(ed.project) ? T("Saved.") : T("Could not save project.json.");
  }
  if (!msg.empty()) ImGui::TextDisabled("%s", msg.c_str());
  ImGui::End();
}

inline void drawExport(Editor& ed, bool* open) {
  static std::string result;
  static bool resultOk = false;
  if (!*open) return;
  ImGui::SetNextWindowSize({540, 420}, ImGuiCond_FirstUseEver);
  if (!ImGui::Begin(W("Export game", "export").c_str(), open)) { ImGui::End(); return; }
  auto& ex = ed.projSettings.exportPreset;
  const int n = static_cast<int>(kExportPlatforms.size());
  if (!platformAvailable(ex.platform)) for (int i = 0; i < n; ++i) if (platformAvailable(i)) { ex.platform = i; break; }
  if (ImGui::BeginCombo(T("Target platform"), kExportPlatforms[static_cast<size_t>(ex.platform)])) {
    for (int i = 0; i < n; ++i) {
      const bool ok = platformAvailable(i);
      if (!ok) ImGui::BeginDisabled();
      if (ImGui::Selectable(kExportPlatforms[static_cast<size_t>(i)], ex.platform == i) && ok) { ex.platform = i; ed.projSettings.save(ed.project); }
      if (!ok) ImGui::EndDisabled();
      if (!ok && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", T("Not available in this build yet. Android, Web and UWP need their own toolchains."));
    }
    ImGui::EndCombo();
  }
  const fs::path outRoot = editorFolder() / "Export";
  ImGui::TextDisabled("%s", T("Output folder:"));
  ImGui::TextWrapped("%s", outRoot.string().c_str());
  ImGui::Separator();
  const auto miss = ed.projSettings.missingInfo();
  if (!miss.empty()) {
    std::string list; for (auto& m : miss) { if (!list.empty()) list += ", "; list += T(m.c_str()); }
    ImGui::TextColored({1.f, .8f, .3f, 1.f}, "%s %s", T("Fill in the Editor info first (File > Editor info). Missing:"), list.c_str());
  }
  ImGui::BeginDisabled(!miss.empty() || !platformAvailable(ex.platform));
  if (ImGui::Button(T("Export game"), {-1, 0})) {
    if (ed.playing) ed.stopPlay();     // exporta a cena parada, como esta salva
    ed.saveScene();
    ed.projSettings.save(ed.project);
    const xp::Result r = xp::exportGame(ed.project, ed.projSettings, ex.platform, editorFolder(), editorExeName(), outRoot);
    resultOk = r.ok;
    if (r.ok) result = F("Exported %zu files (%.1f MB) to %s", r.files, static_cast<double>(r.bytes) / 1048576.0, r.folder.string().c_str());
    else result = std::string(T("Export failed:")) + " " + T(r.err.c_str());
    tool::logInfo(result);
  }
  ImGui::EndDisabled();
  if (!result.empty()) ImGui::TextColored(resultOk ? ImVec4(.4f, .9f, .5f, 1.f) : ImVec4(1.f, .45f, .4f, 1.f), "%s", result.c_str());
  ImGui::Separator();
  ImGui::TextWrapped("%s", T("Open the output folder and double-click Play.bat (Windows) or run play.sh (Linux). The Data/Project folder holds the game files in plain text, so you can open them in other tools."));
  ImGui::End();
}

} // namespace editor
