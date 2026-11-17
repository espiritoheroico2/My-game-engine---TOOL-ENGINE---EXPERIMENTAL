#pragma once

// Chave liga/desliga (Toggle, ou switch). E uma trilha arredondada de tamanho fixo com uma bolinha dentro.
// A bolinha fica a esquerda quando desligada e a direita quando ligada.
//
// Partes: a trilha (fundo, muda de cor conforme o estado), a bolinha (filho visual da trilha) e o contorno de
// foco. A trilha tem tamanho fixo (ToggleStyle) e fica centrada na area util: ela nunca estica com o pai.
//
// Click inverte o valor exatamente uma vez (Down e Up dentro, com o mesmo ponteiro). Arrastar para fora e
// soltar nao muda nada. Espaco inverte quando o elemento esta focado. Desabilitado nao muda nada.
//
// Exemplo:
//   auto id = tree.Create<Toggle>(parent);
//   tree.GetAs<Toggle>(id)->on = true;   // Estado inicial de programa.

#include "SpriteUi/Core/Math.hpp"
#include "SpriteUi/Elements/Element.hpp"
#include "SpriteUi/Elements/PointerEvent.hpp"
#include "SpriteUi/Elements/PressGesture.hpp"
#include "SpriteUi/Rendering/DrawList.hpp"

#include <cstdint>
#include <vector>

namespace ToolEngine::SpriteUi::Elements {

struct ToggleStyle {
    Vec4 trackOff{0.2f, 0.2f, 0.22f, 1.0f};
    Vec4 trackOn{0.05f, 0.2f, 0.55f, 1.0f};
    Vec4 disabledTrack{0.15f, 0.15f, 0.16f, 1.0f};
    Vec4 knob{1.0f, 1.0f, 1.0f, 1.0f};
    Vec4 focusRing{0.95f, 0.8f, 0.3f, 1.0f};
    float trackWidth = 40.0f;
    float trackHeight = 22.0f;
    float knobInset = 3.0f;        // Folga entre a bolinha e a borda da trilha.
    float focusRingWidth = 2.0f;
    float hoverAmount = 0.10f;
};

struct ToggleEvents {
    bool pressStarted = false;
    bool pressEnded = false;
    bool clicked = false;
    bool valueChanged = false;
};

class Toggle final : public Element {
  public:
    bool on = false;

    // Ponteiro: Click inverte o valor. Devolve o que aconteceu neste evento.
    [[nodiscard]] ToggleEvents OnPointer(const PointerEvent& event) noexcept;

    // Espaco com foco inverte o valor. Sem foco ou desabilitado, nao faz nada.
    [[nodiscard]] ToggleEvents ToggleByKey() noexcept;

  private:
    PressGesture gesture_;
};

// Trilha: tamanho fixo do estilo, limitado a area util, centrada nela.
[[nodiscard]] Rect ToggleTrackRect(const Toggle& toggle, const ToggleStyle& style) noexcept;

// Bolinha: diametro = altura da trilha menos a folga dos dois lados. Fica a esquerda ou a direita, conforme on.
[[nodiscard]] Rect ToggleKnobRect(const Toggle& toggle, const ToggleStyle& style) noexcept;

// Itens de desenho: trilha (cor conforme o estado), bolinha e contorno de foco quando focado.
[[nodiscard]] std::vector<Rendering::UIRenderItem> BuildToggleItems(const Toggle& toggle, const ToggleStyle& style,
                                                                    float zOrder, std::uint64_t paintOrder);

} // namespace ToolEngine::SpriteUi::Elements
