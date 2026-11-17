#pragma once

// Botao: um fundo (retangulo ou retangulo arredondado) com o rotulo como filho, centralizado na area util.
//
// Comportamento (Button::OnPointer, com PressGesture) e apresentacao (BuildButtonBackground) sao separados.
// Click e Hold seguem as regras de PressGesture: Hold nao e evento, e o tempo pressionado comparado com
// holdSeconds.
//
// Teclado: Enter e Espaco ativam o botao quando ele esta focado (o chamador traduz a tecla em ActivateByKey).
//
// Area de clique: o proprio fundo (cantos arredondados incluidos). Ponto fora do desenho nao pressiona nem clica.
// Altura padrao: 28 px a 100 por cento (kStandardControlHeight), como altura minima do elemento.

#include "SpriteUi/Core/Math.hpp"
#include "SpriteUi/Elements/Element.hpp"
#include "SpriteUi/Elements/PointerEvent.hpp"
#include "SpriteUi/Elements/PressGesture.hpp"
#include "SpriteUi/Rendering/DrawList.hpp"
#include "SpriteUi/Text/TextLayout.hpp"

#include <cstdint>
#include <vector>

namespace ToolEngine::SpriteUi::Elements {

// Cores em RGBA linear. O estado de hover clareia e o de pressionado escurece pela mesma fracao.
struct ButtonStyle {
    Vec4 background{0.05f, 0.2f, 0.55f, 1.0f};
    Vec4 disabledBackground{0.15f, 0.15f, 0.16f, 1.0f};
    Vec4 focusRing{0.95f, 0.8f, 0.3f, 1.0f};
    float focusRingWidth = 2.0f;
    float hoverAmount = 0.10f;   // Fracao de clareamento no hover.
    float pressedAmount = 0.10f; // Fracao de escurecimento quando pressionado.
    float pressOffsetY = 1.0f;   // Deslocamento para baixo, em pixels, quando pressionado.
};

struct ButtonEvents {
    bool pressStarted = false;
    bool pressEnded = false;
    bool clicked = false;
    bool activated = false; // Ativado pelo teclado.
};

class Button final : public Element {
  public:
    float cornerRadius = 4.0f; // 0 = retangulo reto.
    float holdSeconds = 0.25f; // Tempo pressionado a partir do qual o botao esta em Hold.

    Button() noexcept { minSize.y = kStandardControlHeight; }

    // Aplica um evento de ponteiro. So conta o que cai no fundo desenhado. Desabilitado: limpa hover e pressionado
    // e nao emite nada.
    [[nodiscard]] ButtonEvents OnPointer(const PointerEvent& event) noexcept;

    // Enter ou Espaco com foco. Sem foco ou desabilitado, nao faz nada.
    [[nodiscard]] ButtonEvents ActivateByKey() noexcept;

    // Avanca o tempo pressionado pelo tempo do quadro informado pelo host.
    void Advance(float frameSeconds) noexcept;

    // Pressionado ha pelo menos holdSeconds.
    [[nodiscard]] bool IsHeld() const noexcept;

  private:
    PressGesture gesture_;
};

// Tamanho Auto: o rotulo medido mais o padding. Entradas nao finitas viram zero.
[[nodiscard]] Vec2 PreferredButtonSize(const Text::TextLayoutResult& labelLayout, Insets padding) noexcept;

// Deslocamento do conteudo do botao (fundo e rotulo): para baixo quando pressionado.
[[nodiscard]] Vec2 ButtonPressOffset(const Button& button, const ButtonStyle& style) noexcept;

// Itens do fundo e do contorno de foco. O rotulo e desenhado pelo BuildLabelItems do proprio filho, com o
// deslocamento de ButtonPressOffset somado a sua area util.
[[nodiscard]] std::vector<Rendering::UIRenderItem> BuildButtonBackground(const Button& button,
                                                                         const ButtonStyle& style,
                                                                         float zOrder, std::uint64_t paintOrder);

} // namespace ToolEngine::SpriteUi::Elements
