// fx.hpp - Formatacao do texto de UI do jogo ("{variavel}"). As particulas (biblioteca engine/particles) ficam em
// fx_particles.hpp; o desenho delas e da UI do jogo esta em view_overlays.hpp (SpriteUI). O sistema antigo de particulas
// (ParticleComp + Editor::updateParticles) foi removido: cenas antigas sao convertidas ao abrir (components.hpp).
#pragma once
#include "editor_state.hpp"

#include <cstdio>
#include <string>

namespace editor {

// "{nome}" no texto vira o valor da variavel do jogo (ed.vars); suporta varias por texto.
inline std::string uiFormat(const Editor& ed, const char* text) {
  std::string out, src = text;
  for (size_t i = 0; i < src.size();) {
    if (src[i] == '{') { const size_t e = src.find('}', i); if (e != std::string::npos && e - i < 40) {
      const auto it = ed.vars.find(src.substr(i + 1, e - i - 1)); char buf[32]; std::snprintf(buf, sizeof buf, "%g", static_cast<double>(it == ed.vars.end() ? 0.f : it->second)); out += buf; i = e + 1; continue; } }
    out += src[i++];
  }
  return out;
}

} // namespace editor
