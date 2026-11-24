#pragma once

// Entrada do quadro lida do SDL. Substitui o que a UI pegava do ImGui (ImGui::GetIO, IsKeyPressed, IsMouseClicked,
// IsMouseDragging, MouseWheel): o laco principal alimenta com cada evento (onEvent) e o painel de cena le aqui.
// Ciclo: eventos do quadro -> onEvent; os consumidores leem; no fim do quadro, endFrame() limpa as bordas (pressed,
// clicked, roda e movimento). Estado mantido (held, modificadores) vem direto do SDL.

#include <SDL3/SDL.h>

namespace editor {

struct FrameInput {
  bool pressed[SDL_SCANCODE_COUNT] = {};   // tecla apertada neste quadro (borda, sem repeticao)
  bool clicked[3] = {};                    // botao 0 esq., 1 meio, 2 dir.: clicado neste quadro
  bool held[3] = {};                       // botao mantido agora
  float mdx = 0.f, mdy = 0.f;              // movimento do mouse no quadro (px logicos)
  float wheel = 0.f;                       // roda no quadro (+ = para cima)
  float dt = 0.f;                          // tempo do quadro (s), limitado
  bool shift = false, ctrl = false;        // modificadores agora
  bool typing = false;                     // um campo de texto esta recebendo digitacao

  [[nodiscard]] bool down(SDL_Scancode sc) const {
    const bool* ks = SDL_GetKeyboardState(nullptr);
    return ks && ks[sc];
  }

  void onEvent(const SDL_Event& e) {
    switch (e.type) {
      case SDL_EVENT_KEY_DOWN:
        if (!e.key.repeat && e.key.scancode < SDL_SCANCODE_COUNT) pressed[e.key.scancode] = true;
        break;
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e.button.button >= 1 && e.button.button <= 3) clicked[e.button.button - 1] = true;
        break;
      case SDL_EVENT_MOUSE_MOTION:
        mdx += e.motion.xrel;
        mdy += e.motion.yrel;
        break;
      case SDL_EVENT_MOUSE_WHEEL:
        wheel += e.wheel.y;
        break;
      default:
        break;
    }
  }

  // Estado mantido: chamado uma vez por quadro, antes de ler (depois de processar os eventos).
  void refresh() {
    const Uint32 mb = SDL_GetMouseState(nullptr, nullptr);
    held[0] = (mb & SDL_BUTTON_LMASK) != 0;
    held[1] = (mb & SDL_BUTTON_MMASK) != 0;
    held[2] = (mb & SDL_BUTTON_RMASK) != 0;
    const SDL_Keymod mod = SDL_GetModState();
    shift = (mod & SDL_KMOD_SHIFT) != 0;
    ctrl = (mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
  }

  // Fim do quadro: as bordas valem so um quadro.
  void endFrame() {
    for (bool& p : pressed) p = false;
    for (bool& c : clicked) c = false;
    mdx = mdy = 0.f;
    wheel = 0.f;
  }
};

// Entrada compartilhada do editor (um so laco, um so mouse).
inline FrameInput& frameInput() {
  static FrameInput f;
  return f;
}

}  // namespace editor
