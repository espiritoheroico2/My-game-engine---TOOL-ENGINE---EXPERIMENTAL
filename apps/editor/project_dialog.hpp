// ToolEngine Editor - janela de INICIO: pede o nome do projeto, o autor e a pasta onde ele vai ser salvo.
//
// Aparece antes do editor abrir (main.cpp), a menos que o editor seja iniciado com --project <pasta> ou em modo
// de captura de tela (--frames). Todos os arquivos do projeto (Assets, Scenes, Saves, project.json, scripts) ficam
// dentro da pasta escolhida. Os ajustes globais do editor (editor_settings.json, editor_layout.ini, tema) continuam
// ao lado do executavel.
//
// LOGO: a area da direita e um espaco reservado para o logo da engine (drawLogoSlot). Quando existir a imagem,
// troque o conteudo dessa funcao por uma textura; o layout nao precisa mudar.
#pragma once
#include "i18n.hpp"
#include "project_settings.hpp"

#include "imgui.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_dialog.h>

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>

namespace editor {

struct ProjectDialog {
  char name[101] = "Meu Jogo";
  char author[101] = "";
  char location[512] = "";
  char openPath[512] = "";
  std::string error;            // chave em ingles; exibida com T()
  std::string lastProject;      // ultimo projeto aberto (editor_settings.json), vazio se nenhum
  bool done = false;
  fs::path result;
  std::mutex mu;                // o dialogo nativo de pasta devolve o resultado em outra thread
  std::string browsed;
  bool browseReady = false;
};

// Pasta padrao sugerida: a pasta-mae do ultimo projeto, ou "ToolEngine Projects" na pasta do usuario.
inline std::string defaultProjectsFolder(const std::string& lastProject) {
  if (!lastProject.empty()) {
    const fs::path parent = fs::path(lastProject).parent_path();
    if (!parent.empty()) return parent.string();
  }
  const char* home = std::getenv("USERPROFILE");
  if (!home || !*home) home = std::getenv("HOME");
  if (!home || !*home) return "";
  return (fs::path(home) / "ToolEngine Projects").string();
}

inline void SDLCALL onProjectFolderPicked(void* userdata, const char* const* files, int) {
  auto* d = static_cast<ProjectDialog*>(userdata);
  std::lock_guard<std::mutex> lock(d->mu);
  if (files && files[0]) { d->browsed = files[0]; d->browseReady = true; }
}

// Espaco reservado para o logo (quadrado com a palavra "Logo"). Troque pelo desenho da imagem quando existir.
inline void drawLogoSlot(float size) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::Dummy(ImVec2(size, size));
  dl->AddRect(p, ImVec2(p.x + size, p.y + size), ImGui::GetColorU32(ImGuiCol_Border), 8.f, 0, 1.5f);
  const char* label = T("Logo");
  const ImVec2 ts = ImGui::CalcTextSize(label);
  dl->AddText(ImVec2(p.x + (size - ts.x) * 0.5f, p.y + (size - ts.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_TextDisabled), label);
}

inline void drawProjectDialog(ProjectDialog& d, SDL_Window* window) {
  { // pasta escolhida no dialogo nativo
    std::lock_guard<std::mutex> lock(d.mu);
    if (d.browseReady) { std::snprintf(d.location, sizeof d.location, "%s", d.browsed.c_str()); d.browseReady = false; }
  }
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(680.f, 0.f), ImGuiCond_Always);
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize;
  ImGui::Begin("ToolEngine##start", nullptr, flags);

  ImGui::TextUnformatted(T("Create a new project"));
  ImGui::Separator();
  if (ImGui::BeginTable("##newproj", 2, ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("fields", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("logo", ImGuiTableColumnFlags_WidthFixed, 136.f);
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(T("Project name")); ImGui::SetNextItemWidth(-1.f);
    ImGui::InputText("##name", d.name, sizeof d.name);
    ImGui::TextUnformatted(T("Author")); ImGui::SetNextItemWidth(-1.f);
    ImGui::InputText("##author", d.author, sizeof d.author);
    ImGui::TextUnformatted(T("Save in folder")); ImGui::SetNextItemWidth(-110.f);
    ImGui::InputText("##location", d.location, sizeof d.location);
    ImGui::SameLine();
    if (ImGui::Button(T("Browse..."))) SDL_ShowOpenFolderDialog(onProjectFolderPicked, &d, window, d.location[0] ? d.location : nullptr, false);
    ImGui::TextDisabled("%s", T("The project folder will be created inside this folder."));
    ImGui::TableSetColumnIndex(1);
    drawLogoSlot(120.f);
    ImGui::EndTable();
  }
  if (ImGui::Button(T("Create project"))) {
    std::string err;
    std::string loc = d.location;
    if (loc.empty()) loc = defaultProjectsFolder(d.lastProject);
    const fs::path root = createProjectFolder(d.name, d.author, loc, err);
    if (root.empty()) d.error = err;
    else { d.result = root; d.done = true; }
  }

  ImGui::Spacing();
  ImGui::TextUnformatted(T("Or open an existing project folder"));
  ImGui::Separator();
  ImGui::SetNextItemWidth(-110.f);
  ImGui::InputText("##openpath", d.openPath, sizeof d.openPath);
  ImGui::SameLine();
  if (ImGui::Button(T("Open"))) {
    std::string err;
    const fs::path root = openProjectFolder(d.openPath, err);
    if (root.empty()) d.error = err;
    else { d.result = root; d.done = true; }
  }
  if (!d.lastProject.empty() && ImGui::Button(T("Open last project"))) {
    std::string err;
    const fs::path root = openProjectFolder(d.lastProject, err);
    if (root.empty()) d.error = err;
    else { d.result = root; d.done = true; }
  }
  if (!d.lastProject.empty()) { ImGui::SameLine(); ImGui::TextDisabled("%s", d.lastProject.c_str()); }

  if (!d.error.empty()) ImGui::TextColored(ImVec4(1.f, 0.45f, 0.45f, 1.f), "%s", T(d.error.c_str()));
  ImGui::End();
}

} // namespace editor
