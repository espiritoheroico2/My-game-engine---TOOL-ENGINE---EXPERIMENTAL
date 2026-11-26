#pragma once

// Guarda do botao esquerdo do mouse. Um gesto de arraste ou de redimensionamento so pode seguir enquanto o botao
// esta de fato preso. Se o evento de soltura se perdeu (a janela perdeu o foco, o SDL nao entregou o BUTTON_UP), o
// gesto continuaria "preso". Esta regra detecta esse caso e pede a soltura sintetica no mesmo quadro.
//
// Sem SDL aqui: quem chama le o estado real do botao (SDL_GetMouseState) e passa o booleano.

namespace editor::pointer_guard {

struct State {
  bool leftHeld = false;   // o que o editor viu pela ultima vez: BUTTON_DOWN esquerdo sem BUTTON_UP depois
};

// Registra um evento de botao. `down` true para BUTTON_DOWN, false para BUTTON_UP.
inline void observeLeft(State& s, bool down) { s.leftHeld = down; }

// True quando o editor acredita que o botao esta preso, o SDL diz que ele esta solto, e nenhum roteiro de teste esta
// tocando (o roteiro emite seus proprios eventos e nao reflete o botao real).
[[nodiscard]] inline bool releaseMissed(const State& s, bool sdlLeftHeld, bool scriptActive) {
  return s.leftHeld && !sdlLeftHeld && !scriptActive;
}

}  // namespace editor::pointer_guard
