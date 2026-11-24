#pragma once
// Estilo da interface que o usuario escolhe em Configuracoes > Appearance: raio dos botoes, raio das janelas e escala
// do texto. Sao valores globais, lidos a cada quadro pelas molduras e pelos paineis SpriteUI. Os padroes sao o visual
// de antes (raio 3 nos botoes, 6 nas janelas, texto 100%). Persistidos em editor_settings.json.
#include "settings_model.hpp"

namespace editor::ui_style {

[[nodiscard]] inline float& buttonRadius() {
  static float value = 3.f;
  return value;
}
[[nodiscard]] inline float& windowRadius() {
  static float value = 6.f;
  return value;
}
// Escala que a SpriteUI le a cada quadro: texto do usuario x escala da interface (UI scale). Atualizada por refreshScale().
[[nodiscard]] inline float& textScale() {
  static float value = 1.f;
  return value;
}
// Tamanho de texto escolhido em Appearance > Interface text size (sem a escala da interface).
[[nodiscard]] inline float& textSetting() {
  static float value = 1.f;
  return value;
}
// Fator da escala da interface (UI scale / padrao 125%): 1 no padrao, maior aumenta os elementos.
[[nodiscard]] inline float& interfaceFactor() {
  static float value = 1.f;
  return value;
}
inline void refreshScale() { textScale() = textSetting() * interfaceFactor(); }

// Tamanho de fonte da interface: o tamanho base vezes a escala do texto.
[[nodiscard]] inline float uiFont(float base) { return base * textScale(); }
// Altura da barra de menu e da barra de status (SpriteUI): antes era a altura de linha do ImGui.
[[nodiscard]] inline float barHeight() { return 28.f * textScale(); }

}  // namespace editor::ui_style
