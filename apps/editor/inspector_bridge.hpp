#pragma once

// Ponte entre os valores do objeto selecionado e os elementos da SpriteUI do Inspector (Slider, Dropdown, Checkbox,
// ColorPicker e TextField). Cada Sync* copia um valor do objeto para um elemento; os Read* fazem o caminho de volta.
// Sync nunca escreve no objeto: so o painel escreve, em acao do usuario (inspector_panel.hpp).
//
// Exemplo: a intensidade da luz e 2.5 e o limite e de 0 a 200. SyncSlider deixa o deslizante em 2.5 com esses
// limites. Se o objeto tiver 900 (valor de cena antiga), o elemento prende em 200 e o objeto nao muda ate o usuario
// mexer no controle.
//
// Sem SDL e sem o Editor: so a SpriteUI e inspector_model.hpp. Compilado junto com o painel (tool_editor).

#include "inspector_model.hpp"

#include "SpriteUi/Core/Math.hpp"
#include "SpriteUi/Elements/Checkbox.hpp"
#include "SpriteUi/Elements/Dropdown.hpp"
#include "SpriteUi/Elements/Slider.hpp"
#include "SpriteUi/Widgets/ColorPicker.hpp"
#include "SpriteUi/Widgets/Controls.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace editor::inspector_bridge {

namespace Elm = ToolEngine::SpriteUi::Elements;
namespace Wd = ToolEngine::SpriteUi::Widgets;
namespace SU = ToolEngine::SpriteUi;
namespace im = editor::inspector_model;

// Deslizante: intervalo do modelo e valor do objeto. O elemento prende o valor no intervalo.
inline void SyncSlider(Elm::Slider& slider, im::Range range, float value) {
    slider.range.minimum = range.min;
    slider.range.maximum = range.max;
    slider.range.step = 0.0f;
    slider.range.integer = false;
    (void)slider.SetValue(value);
}

inline void SyncCheckbox(Elm::Checkbox& checkbox, bool checked) {
    checkbox.checked = checked;
}

// Caixa de escolha. Indice fora da lista vira "nenhum item"; so o painel decide o que o indice significa.
inline void SyncDropdown(Elm::Dropdown& dropdown, const std::vector<std::string>& options, std::size_t selected) {
    dropdown.control.options = options;
    dropdown.control.selectedIndex = selected < options.size() ? selected : Wd::NoControlIndex;
}

// Seletor de cor: a cor do objeto (0 a 1) vira a amostra atual. Alfa fica 1; o objeto guarda so RGB.
inline void SyncColorPicker(Wd::ColorPickerControl& picker, const float rgb[3]) {
    (void)picker.SetColor(SU::Vec4{rgb[0], rgb[1], rgb[2], 1.0f});
}

// Le a cor do seletor de volta (0 a 1, sem premultiplicar). So o painel chama, em acao do usuario.
inline void ReadColorPicker(const Wd::ColorPickerControl& picker, float rgb[3]) {
    const SU::Vec4 c = picker.Sample();
    rgb[0] = c.x;
    rgb[1] = c.y;
    rgb[2] = c.z;
}

} // namespace editor::inspector_bridge
