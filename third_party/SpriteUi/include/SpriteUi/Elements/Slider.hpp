#pragma once

// Controle deslizante (float ou int). A trilha e a area util; o polegar fica dentro da trilha, com o centro
// variando entre (esquerda + metade do polegar) e (direita - metade do polegar).
//
// O valor sempre fica em [minimum, maximum]. NaN e rejeitado (o valor anterior fica). minimum igual a maximum
// e valido: o valor e fixo. Com integer, o valor e arredondado para inteiro; senao, step (se maior que zero)
// define os degraus, e step zero deixa o valor continuo.
//
// Teclado: StepBy (setas: um passo; PageUp e PageDown: dez passos), SetToMinimum (Home) e SetToMaximum (End).

#include "SpriteUi/Core/Math.hpp"
#include "SpriteUi/Elements/Element.hpp"
#include "SpriteUi/Elements/PointerEvent.hpp"
#include "SpriteUi/Elements/PressGesture.hpp"
#include "SpriteUi/Rendering/DrawList.hpp"

#include <string>
#include <vector>

namespace ToolEngine::SpriteUi::Elements {

struct SliderRange {
    float minimum = 0.0f;
    float maximum = 1.0f;
    float step = 0.0f; // 0 = valor continuo.
    bool integer = false;
};

struct SliderStyle {
    Vec4 track{0.2f, 0.2f, 0.24f, 1.0f};
    Vec4 filled{0.05f, 0.2f, 0.55f, 1.0f};
    Vec4 thumb{0.92f, 0.92f, 0.94f, 1.0f};
    Vec4 disabled{0.35f, 0.35f, 0.37f, 1.0f};
    Vec4 focusRing{0.95f, 0.8f, 0.3f, 1.0f};
    float trackHeight = 4.0f;
    float focusRingWidth = 2.0f;
    float hoverAmount = 0.10f;
};

struct SliderEvents {
    bool pressStarted = false;
    bool pressEnded = false;
    bool valueChanged = false;
};

class Slider final : public Element {
  public:
    SliderRange range;
    float thumbSize = 14.0f; // Lado do polegar, que e um disco.

    // Valor atual, sempre dentro do intervalo.
    [[nodiscard]] float Value() const noexcept;

    // Define o valor (com arredondamento e limites). Devolve true se o valor mudou.
    bool SetValue(float value) noexcept;

    // Move o valor em steps passos. Sem step, um passo e 1% do intervalo (inteiro: 1). Devolve true se mudou.
    bool StepBy(int steps) noexcept;
    bool SetToMinimum() noexcept;
    bool SetToMaximum() noexcept;

    // Posicao relativa no intervalo, de 0 a 1. Intervalo vazio vale 0.
    [[nodiscard]] float Ratio() const noexcept;

    // Clique ou arrasto. position e absoluta (mesmo espaco da caixa do elemento).
    [[nodiscard]] SliderEvents OnPointer(const PointerEvent& event) noexcept;

  private:
    [[nodiscard]] float ValueAtPosition(Vec2 position) const noexcept;

    PressGesture gesture_;
    float value_ = 0.0f;
};

// Texto do valor para um rotulo: inteiro sem casas, float com duas casas.
[[nodiscard]] std::string SliderValueText(const Slider& slider);

// Itens da trilha preenchida, do polegar e do contorno de foco.
[[nodiscard]] std::vector<Rendering::UIRenderItem> BuildSliderItems(const Slider& slider,
                                                                     const SliderStyle& style, float zOrder,
                                                                     std::uint64_t paintOrder);

} // namespace ToolEngine::SpriteUi::Elements
