#pragma once

// Gesto de pressionar e soltar, compartilhado pelos controles que recebem Click (Button, Checkbox, Slider).
//
// Regras:
//  - Down dentro do elemento captura o ponteiro. Sair da area nao solta a captura; o Up ainda decide o Click.
//  - Click so existe quando Down e Up caem dentro, com o mesmo ponteiro.
//  - Cancel solta a captura sem Click. Outro ponteiro nao solta a captura.
//  - Desabilitado: limpa hover e pressionado e nao gera nada.
//  - Hold nao e evento: e o tempo pressionado acumulado por Advance, comparado com um limite.

#include "SpriteUi/Elements/Element.hpp"
#include "SpriteUi/Elements/PointerEvent.hpp"

#include <cstdint>

namespace ToolEngine::SpriteUi::Elements {

struct GestureResult {
    bool pressStarted = false;
    bool pressEnded = false;
    bool clicked = false;
};

class PressGesture {
  public:
    // Aplica um evento de ponteiro aos estados (hover, pressed) e devolve o que aconteceu.
    [[nodiscard]] GestureResult Apply(ElementStates& states, const PointerEvent& event) noexcept;

    // Avanca o tempo pressionado pelo tempo do quadro informado pelo host. Sem pressionado, nao faz nada.
    void Advance(const ElementStates& states, float frameSeconds) noexcept;

    // Pressionado ha pelo menos holdSeconds.
    [[nodiscard]] bool IsHeld(const ElementStates& states, float holdSeconds) const noexcept;

  private:
    void EndPress(ElementStates& states) noexcept;

    std::uint32_t pointer_ = 0;
    float pressedSeconds_ = 0.0f;
};

} // namespace ToolEngine::SpriteUi::Elements
