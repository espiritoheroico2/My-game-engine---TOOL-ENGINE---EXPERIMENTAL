// Acoes de Editor que nao sao janelas: abrir arquivo no editor externo.
// Estado do Help e do Test map: help_state.hpp.
#pragma once
#include "editor_state.hpp"
#include "help_state.hpp"
#include "i18n.hpp"

#include <SDL3/SDL.h>

#include <filesystem>
#include <string>

namespace editor {

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











} // namespace editor
