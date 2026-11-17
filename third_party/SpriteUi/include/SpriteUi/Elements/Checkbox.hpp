#pragma once

// Caixa de selecao: uma caixa de tamanho fixo a esquerda da area util, com a marca quando marcada. O rotulo,
// se houver, e um filho que preenche a area util com o padding esquerdo de CheckboxLabelPadding, assim o
// texto nunca encosta na caixa.
//
// Click inverte o valor exatamente uma vez (Down e Up dentro, mesmo ponteiro). Arrastar para fora e soltar
// nao muda o valor. Espaco inverte com foco. Desabilitado nao muda nada.

#include "SpriteUi/Core/Math.hpp"
#include "SpriteUi/Elements/Element.hpp"
#include "SpriteUi/Elements/PointerEvent.hpp"
#include "SpriteUi/Elements/PressGesture.hpp"
#include "SpriteUi/Rendering/DrawList.hpp"

#include <cstdint>
#include <vector>

namespace ToolEngine::SpriteUi::Elements {

struct CheckboxStyle {
    Vec4 boxBackground{0.12f, 0.12f, 0.14f, 1.0f};
    Vec4 checkedBackground{0.05f, 0.2f, 0.55f, 1.0f};
    Vec4 disabledBackground{0.15f, 0.15f, 0.16f, 1.0f};
    Vec4 mark{1.0f, 1.0f, 1.0f, 1.0f};
    Vec4 focusRing{0.95f, 0.8f, 0.3f, 1.0f};
    float boxSize = 18.0f;
    float boxGap = 8.0f;          // Espaco entre a caixa e o rotulo.
    float markThickness = 2.0f;
    float focusRingWidth = 2.0f;
    float hoverAmount = 0.10f;
};

struct CheckboxEvents {
    bool pressStarted = false;
    bool pressEnded = false;
    bool clicked = false;
    bool valueChanged = false;
};

class Checkbox final : public Element {
  public:
    bool checked = false;

    [[nodiscard]] CheckboxEvents OnPointer(const PointerEvent& event) noexcept;

    // Espaco com foco inverte o valor. Sem foco ou desabilitado, nao faz nada.
    [[nodiscard]] CheckboxEvents ToggleByKey() noexcept;

  private:
    PressGesture gesture_;
};

// Padding esquerdo do rotulo filho: reserva a caixa e o espaco entre ela e o texto.
[[nodiscard]] Insets CheckboxLabelPadding(const CheckboxStyle& style) noexcept;

// Caixa da marca: tamanho fixo, a esquerda da area util, centralizada na vertical.
[[nodiscard]] Rect CheckboxBox(const Checkbox& checkbox, const CheckboxStyle& style) noexcept;

// Itens da caixa: fundo (conforme o estado), marca quando marcada e contorno de foco.
[[nodiscard]] std::vector<Rendering::UIRenderItem> BuildCheckboxItems(const Checkbox& checkbox,
                                                                      const CheckboxStyle& style, float zOrder,
                                                                      std::uint64_t paintOrder);

} // namespace ToolEngine::SpriteUi::Elements
