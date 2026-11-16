#pragma once

// Menu flutuante da janela Scene: regras do modo, do espaco e do passo do ima que nao dependem de janela. A janela
// (scene_view_panel.hpp) mostra o menu e manda a acao; quem decide o novo estado e este arquivo. Sem ImGui e sem
// SpriteUI: roda no ctest como Editor.SceneTransformMenu.
//
// Exemplo: com a Caixa azul selecionada, o jogador aperta E e o gizmo vira o anel de girar. Apertar Q troca o
// espaco para Local: a seta de mover da Caixa passa a seguir a frente dela, mesmo que ela esteja girada. O passo
// do ima (0,25 m) faz a Caixa parar em 0,25 e nao em 0,27.
//
// Como funciona:
//   applyAction:      W, E, R trocam o modo (mover, girar, escalar); Q alterna mundo e local.
//   snapStepFromSlider / sliderFromSnapStep: o controle de passo vai de 0,05 m a 2 m, em posicao de 0 a 1.

#include "scene_view_model.hpp"

#include <algorithm>
#include <cstdint>

namespace editor::scene_transform_menu {

// Acao de uma tecla do menu. None: a tecla nao e do menu.
enum class Action : std::uint8_t { None, Move, Rotate, Scale, ToggleSpace };

inline constexpr float kMinSnapStep = 0.05f;   // menor passo do ima (m)
inline constexpr float kMaxSnapStep = 2.f;     // maior passo do ima (m)

// Aplica a acao ao modo (0 mover, 1 girar, 2 escalar; ver scene_view_model::Mode) e ao espaco (true = local).
// Acao sem efeito (None) nao muda nada.
inline void applyAction(Action action, int& mode, bool& local) {
  switch (action) {
    case Action::Move: mode = static_cast<int>(scene_view_model::Mode::Move); break;
    case Action::Rotate: mode = static_cast<int>(scene_view_model::Mode::Rotate); break;
    case Action::Scale: mode = static_cast<int>(scene_view_model::Mode::Scale); break;
    case Action::ToggleSpace: local = !local; break;
    case Action::None: break;
  }
}

// Passo do ima para a posicao t (0 a 1) do controle: t = 0 e o minimo, t = 1 e o maximo. Fora de 0 a 1 e cortado.
inline float snapStepFromSlider(float t) {
  return kMinSnapStep + std::clamp(t, 0.f, 1.f) * (kMaxSnapStep - kMinSnapStep);
}

// Posicao do controle (0 a 1) para um passo. Passo fora da faixa e cortado antes.
inline float sliderFromSnapStep(float step) {
  return std::clamp((std::clamp(step, kMinSnapStep, kMaxSnapStep) - kMinSnapStep) / (kMaxSnapStep - kMinSnapStep), 0.f, 1.f);
}

}  // namespace editor::scene_transform_menu
