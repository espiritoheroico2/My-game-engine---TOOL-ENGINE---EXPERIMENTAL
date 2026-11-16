// Ponte da janela Configuracoes para os elementos SpriteUI (settings_bridge.hpp): radio das APIs compiladas,
// combo, slider da escala, caixa e abas. Tambem confere os itens de desenho: caixa marcada sai diferente da vazia
// (marca ou cor), radio e combo com dados tem itens. Sem janela. Roda no ctest como Editor.SettingsBridge.
// Exemplo: num jogo com so CPU e Vulkan, o radio mostra 2 botoes e o de Vulkan vem marcado se a API pedida for 1.
#include "settings_bridge.hpp"

#include <cstdio>

namespace {

using namespace editor::settings_bridge;
namespace Elm = ToolEngine::SpriteUi::Elements;
namespace Rendering = ToolEngine::SpriteUi::Rendering;

int run = 0;
int failed = 0;

void Check(bool ok, const char* expr, int line) {
  ++run;
  if (!ok) {
    ++failed;
    std::fprintf(stderr, "[FAIL] line %d: %s\n", line, expr);
  }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)

void TestRadioShowsOnlyBuiltApis() {
  // Software, Vulkan e OpenGL compiladas; D3D12 e D3D11 nao.
  const std::array<bool, kApiCount> built{true, true, true, false, false};
  Elm::RadioGroup group;
  SyncApiRadio(group, built, 1);
  CHECK(group.options.size() == 3);
  CHECK(group.options[2].label == "OpenGL");
  CHECK(ReadApiRadio(group) == 1);
}

void TestRadioWithUnbuiltApiHasNoMark() {
  const std::array<bool, kApiCount> built{true, false, false, false, false};
  Elm::RadioGroup group;
  SyncApiRadio(group, built, 3);
  CHECK(group.options.size() == 1);
  CHECK(ReadApiRadio(group) == -1);
}

void TestDropdownClampsSelection() {
  Elm::Dropdown dropdown;
  const std::vector<std::string> options{"VSync", "30 FPS", "Unlimited"};
  SyncDropdown(dropdown, options, 9);
  CHECK(dropdown.control.options.size() == 3);
  CHECK(dropdown.control.selectedIndex == 2);
  SyncDropdown(dropdown, options, -4);
  CHECK(dropdown.control.selectedIndex == 0);
}

void TestDropdownWithoutOptionsHasNoSelection() {
  Elm::Dropdown dropdown;
  SyncDropdown(dropdown, std::vector<std::string>{}, 1);
  CHECK(dropdown.control.options.empty());
  CHECK(dropdown.control.selectedIndex == Wd::NoControlIndex);
}

void TestRenderScaleRangeAndValue() {
  Elm::Slider slider;
  SyncRenderScale(slider, 0.5f);
  CHECK(slider.range.minimum == kRenderScaleMin && slider.range.maximum == kRenderScaleMax);
  CHECK(slider.Value() == 0.5f);
  SyncRenderScale(slider, 7.0f);
  CHECK(slider.Value() == kRenderScaleMax); // acima do maximo: prendido
}

void TestCheckboxFollowsValue() {
  Elm::Checkbox checkbox;
  SyncCheckbox(checkbox, true);
  CHECK(checkbox.checked);
  SyncCheckbox(checkbox, false);
  CHECK(!checkbox.checked);
}

void TestTabsUseModelNamesAndActiveTab() {
  Elm::TabBar tabs;
  SyncTabs(tabs, 2);
  CHECK(tabs.control.labels.size() == 8);
  CHECK(tabs.control.activeIndex == 2);
  SyncTabs(tabs, 42);   // indice invalido: nenhuma aba ativa escolhida
  CHECK(tabs.control.activeIndex == 0);
}

// Duas listas de itens sao diferentes se tamanho ou alguma cor (tint) mudou.
bool ItemsDiffer(const std::vector<Rendering::UIRenderItem>& a, const std::vector<Rendering::UIRenderItem>& b) {
  if (a.size() != b.size()) return true;
  for (std::size_t i = 0; i < a.size(); ++i) {
    const auto& ca = a[i].tintPremultipliedLinear;
    const auto& cb = b[i].tintPremultipliedLinear;
    if (ca.x != cb.x || ca.y != cb.y || ca.z != cb.z || ca.w != cb.w) return true;
  }
  return false;
}

void TestBuiltItemsFollowState() {
  Elm::Checkbox empty;
  Elm::Checkbox checked;
  SyncCheckbox(checked, true);
  const auto emptyItems = Elm::BuildCheckboxItems(empty, Elm::CheckboxStyle{}, 0.3f, 1);
  const auto checkedItems = Elm::BuildCheckboxItems(checked, Elm::CheckboxStyle{}, 0.3f, 1);
  CHECK(!emptyItems.empty());
  CHECK(ItemsDiffer(checkedItems, emptyItems));

  Elm::RadioGroup group;
  SyncApiRadio(group, std::array<bool, kApiCount>{true, true, true, false, false}, 1);
  CHECK(!Elm::BuildRadioGroupItems(group, Elm::RadioStyle{}, 0.3f, 1).empty());

  Elm::Dropdown dropdown;
  SyncDropdown(dropdown, std::vector<std::string>{"VSync", "30 FPS"}, 0);
  CHECK(!Elm::BuildDropdownButtonItems(dropdown, Elm::DropdownStyle{}, 0.3f, 1).empty());
}

} // namespace

int main() {
  TestRadioShowsOnlyBuiltApis();
  TestRadioWithUnbuiltApiHasNoMark();
  TestDropdownClampsSelection();
  TestDropdownWithoutOptionsHasNoSelection();
  TestRenderScaleRangeAndValue();
  TestCheckboxFollowsValue();
  TestTabsUseModelNamesAndActiveTab();
  TestBuiltItemsFollowState();
  std::printf("settings bridge: %d/%d checks passed\n", run - failed, run);
  return failed == 0 ? 0 : 1;
}
