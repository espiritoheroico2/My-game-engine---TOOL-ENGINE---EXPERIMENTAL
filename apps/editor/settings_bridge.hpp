#pragma once

// Ponte entre os valores da janela Configuracoes e os elementos da SpriteUI (RadioGroup, Dropdown, Slider,
// Checkbox e TabBar). Cada funcao Sync* copia um valor do editor para um elemento que ja existe na arvore; as
// funcoes Read* fazem o caminho de volta. Assim o painel (settings_panel.hpp) cuida so de posicao e clique.
//
// Exemplo: a API pedida e 1 (Vulkan) e so Software e Vulkan estao compiladas. SyncRadio deixa dois botoes e
// marca o segundo. Se a API pedida nao esta compilada, nenhum botao fica marcado.
//
// Sem SDL, sem ImGui e sem o Editor: so a SpriteUI e settings_model.hpp. Roda no ctest como Editor.SettingsBridge.

#include "settings_model.hpp"

#include "SpriteUi/Elements/Checkbox.hpp"
#include "SpriteUi/Elements/Dropdown.hpp"
#include "SpriteUi/Elements/RadioButton.hpp"
#include "SpriteUi/Elements/Slider.hpp"
#include "SpriteUi/Elements/TabBar.hpp"
#include "SpriteUi/Widgets/Controls.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace editor::settings_bridge {

namespace Elm = ToolEngine::SpriteUi::Elements;
namespace Wd = ToolEngine::SpriteUi::Widgets;

// APIs de renderizacao, na ordem do radio do ImGui. O indice e o id do botao.
inline constexpr int kApiCount = 5;
inline constexpr std::array<const char*, kApiCount> kApiLabels{
    "Software (CPU)", "Vulkan", "OpenGL", "Direct3D 12", "Direct3D 11"};

// Escala de renderizacao da aba Graficos (o mesmo intervalo do slider do ImGui).
inline constexpr float kRenderScaleMin = 0.25f;
inline constexpr float kRenderScaleMax = 2.0f;

// Radio das APIs: entram so as compiladas. O id de cada botao e o indice da API. API pedida que nao esta
// compilada deixa o radio sem marca.
inline void SyncApiRadio(Elm::RadioGroup& group, const std::array<bool, kApiCount>& available, int selectedApi) {
    group.options.clear();
    for (int i = 0; i < kApiCount; ++i) {
        if (available[static_cast<std::size_t>(i)]) {
            (void)group.AddOption(static_cast<Elm::RadioOptionId>(i), kApiLabels[static_cast<std::size_t>(i)]);
        }
    }
    const bool asked = selectedApi >= 0 && selectedApi < kApiCount && available[static_cast<std::size_t>(selectedApi)];
    group.selectedId = asked ? static_cast<Elm::RadioOptionId>(selectedApi) : Elm::NoRadioOption;
}

// Indice da API marcada no radio, ou -1 se nenhuma.
[[nodiscard]] inline int ReadApiRadio(const Elm::RadioGroup& group) noexcept {
    if (group.selectedId == Elm::NoRadioOption) return -1;
    return static_cast<int>(group.selectedId);
}

// Combo com as opcoes dadas. O indice e prendido em [0, n - 1] pelo modelo (ClampChoice).
inline void SyncDropdown(Elm::Dropdown& dropdown, const std::vector<std::string>& options, int selected) {
    dropdown.control.options = options;
    dropdown.control.selectedIndex = options.empty()
        ? Wd::NoControlIndex
        : static_cast<std::size_t>(editor::settings_model::ClampChoice(selected, static_cast<int>(options.size())));
}

// Slider da escala de renderizacao: intervalo fixo, valor prendido pelo proprio elemento.
inline void SyncRenderScale(Elm::Slider& slider, float value) {
    slider.range.minimum = kRenderScaleMin;
    slider.range.maximum = kRenderScaleMax;
    slider.range.step = 0.0f;
    slider.range.integer = false;
    (void)slider.SetValue(value);
}

inline void SyncCheckbox(Elm::Checkbox& checkbox, bool checked) {
    checkbox.checked = checked;
}

// Abas da janela: os nomes vem do modelo (kTabNames). A aba ativa fica marcada.
inline void SyncTabs(Elm::TabBar& tabs, int active) {
    std::vector<std::string> labels;
    for (int i = 0; i < editor::settings_model::kTabCount; ++i) labels.emplace_back(editor::settings_model::kTabNames[i]);
    tabs.control = Wd::TabBarControl(std::move(labels));
    if (active >= 0 && active < editor::settings_model::kTabCount) (void)tabs.control.Select(static_cast<std::size_t>(active));
}

} // namespace editor::settings_bridge
