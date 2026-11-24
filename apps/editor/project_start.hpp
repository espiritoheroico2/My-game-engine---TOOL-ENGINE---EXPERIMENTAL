#pragma once

// Pasta padrao dos projetos novos, usada pela tela de inicio SpriteUI (start_screen_spriteui.hpp) e pelo host (main.cpp).
// Sem Dear ImGui: antes este codigo ficava em project_dialog.hpp, junto do dialogo ImGui que foi removido.

#include "project_settings.hpp"

#include <cstdlib>
#include <string>

namespace editor {

// Pasta sugerida: a pasta-mae do ultimo projeto, ou "ToolEngine Projects" na pasta do usuario.
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

}  // namespace editor
