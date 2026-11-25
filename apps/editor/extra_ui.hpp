// Acoes de Editor que nao sao janelas: abrir arquivo no editor externo e o preset de fumaca das particulas.
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






// Preset de fumaca: lenta, cresce, sobe um pouco, cinza, some aos poucos depois de um tempo parada.
inline void applySmokePreset(ParticleComp& p) {
  p.form = 6; p.area = 1; p.areaSize[0] = 0.8f; p.areaSize[1] = 0.2f; p.areaSize[2] = 0.8f;
  p.rate = 10.f; p.maxParticles = 250; p.lifetime = 5.f; p.speed = 0.5f; p.spread = 22.f;
  p.size = 0.35f; p.sizeEnd = 1.8f; p.gravityScale = -0.12f; p.bounce = 0.f;
  const float g[4][3] = {{0.62f, 0.62f, 0.64f}, {0.5f, 0.5f, 0.52f}, {0.38f, 0.38f, 0.4f}, {0.26f, 0.26f, 0.28f}};
  for (int c = 0; c < 3; ++c) { p.color[c] = g[0][c]; p.color2[c] = g[1][c]; p.color3[c] = g[2][c]; p.colorEnd[c] = g[3][c]; }
  p.alpha = 0.55f; p.fadeOpacity = true; p.fadeDelay = 1.5f; p.tornado = false; p.route = false;
}





} // namespace editor
