// Modelo da barra de menus principal: titulos, itens, marcas, habilitado, pedidos e botoes Play/Pausa.
// Nao usa SDL. Roda no ctest como Editor.MainMenuModel.
#include "mainmenu_model.hpp"

#include <cstdio>
#include <string>

namespace {

using editor::mainmenu::BarButton;
using editor::mainmenu::Command;
using editor::mainmenu::MenuState;
using editor::mainmenu::MenuSpec;

int run = 0;
int failed = 0;
const char* current = "";

void Check(bool ok, const char* expr) {
  ++run;
  if (!ok) {
    ++failed;
    std::fprintf(stderr, "[FAIL] %s: %s\n", current, expr);
  }
}
#define CHECK(expr) Check((expr), #expr)

const char* Identity(const char* s) { return s; }

void TestMenuTitlesInOrder() {
  current = "TestMenuTitlesInOrder";
  const auto menus = editor::mainmenu::buildMenus(MenuState{}, Identity);
  CHECK(menus.size() == 5);
  CHECK(menus[0].title == "File");
  CHECK(menus[1].title == "Edit");
  CHECK(menus[2].title == "Window");
  CHECK(menus[3].title == "Interface");
  CHECK(menus[4].title == "Options");
}

void TestSeparatorsAndShortcuts() {
  current = "TestSeparatorsAndShortcuts";
  const auto menus = editor::mainmenu::buildMenus(MenuState{}, Identity);
  const MenuSpec& file = menus[0];
  CHECK(file.items[1].label == "Save scene");
  CHECK(file.items[1].shortcut == "Ctrl+S");
  CHECK(file.items[3].separator);
  CHECK(file.items[6].separator);
  CHECK(menus[2].items[16].shortcut == "F1");
}

void TestRequestsFollowTheRows() {
  current = "TestRequestsFollowTheRows";
  CHECK(editor::mainmenu::requestAt(0, 1).command == Command::SaveScene);
  CHECK(editor::mainmenu::requestAt(0, 3).command == Command::None);   // separador
  CHECK(editor::mainmenu::requestAt(0, 99).command == Command::None);  // fora de faixa
  CHECK(editor::mainmenu::requestAt(9, 0).command == Command::None);   // menu inexistente
  CHECK(editor::mainmenu::requestAt(4, 6).command == Command::Settings);
  CHECK(editor::mainmenu::requestAt(4, 6).tab == 1);
}

void TestSettingsTabsFromOptionsMenu() {
  current = "TestSettingsTabsFromOptionsMenu";
  const int expectedTabs[] = {0, 1, 2, 3, 4, 6, 1, 1};
  for (std::size_t i = 0; i < 8; ++i) {
    const auto request = editor::mainmenu::requestAt(4, i);
    CHECK(request.command == Command::Settings);
    CHECK(request.tab == expectedTabs[i]);
  }
}

void TestWindowItemsShowCheckState() {
  current = "TestWindowItemsShowCheckState";
  MenuState state;
  state.hierarchy = true;
  auto menus = editor::mainmenu::buildMenus(state, Identity);
  CHECK(menus[2].items[0].label == "[x] Hierarchy");
  CHECK(menus[2].items[1].label == "[ ] Inspector");
  CHECK(editor::mainmenu::requestAt(2, 0).command == Command::ToggleHierarchy);
  state.lockInterface = true;
  menus = editor::mainmenu::buildMenus(state, Identity);
  CHECK(menus[3].items[0].label == "[x] Lock interface");
}

void TestUndoRedoDisabledWhilePlaying() {
  current = "TestUndoRedoDisabledWhilePlaying";
  MenuState state;
  state.canUndo = true;
  auto menus = editor::mainmenu::buildMenus(state, Identity);
  CHECK(!menus[1].items[0].disabled);
  state.playing = true;
  menus = editor::mainmenu::buildMenus(state, Identity);
  CHECK(menus[1].items[0].disabled);
  CHECK(menus[1].items[1].disabled);   // nada para refazer
}

void TestBarButtonsFollowPlayState() {
  current = "TestBarButtonsFollowPlayState";
  MenuState state;
  auto buttons = editor::mainmenu::barButtons(state, Identity);
  CHECK(buttons.size() == 1);
  CHECK(buttons[0].command == Command::Play);
  CHECK(buttons[0].label == "Play");
  state.playing = true;
  buttons = editor::mainmenu::barButtons(state, Identity);
  CHECK(buttons.size() == 2);
  CHECK(buttons[0].command == Command::Stop);
  CHECK(buttons[1].command == Command::Pause);
  CHECK(buttons[1].label == "Pause");
  state.paused = true;
  buttons = editor::mainmenu::barButtons(state, Identity);
  CHECK(buttons[1].label == "Resume");
}

void TestTranslatorIsUsedForLabels() {
  current = "TestTranslatorIsUsedForLabels";
  const auto upper = [](const char* s) -> const char* {
    if (std::string(s) == "File") return "Arquivo";
    return s;
  };
  const auto menus = editor::mainmenu::buildMenus(MenuState{}, upper);
  CHECK(menus[0].title == "Arquivo");
  CHECK(menus[0].items[1].label == "Save scene");
}

}  // namespace

int main() {
  TestMenuTitlesInOrder();
  TestSeparatorsAndShortcuts();
  TestRequestsFollowTheRows();
  TestSettingsTabsFromOptionsMenu();
  TestWindowItemsShowCheckState();
  TestUndoRedoDisabledWhilePlaying();
  TestBarButtonsFollowPlayState();
  TestTranslatorIsUsedForLabels();
  std::printf("Editor.MainMenuModel: %d checks, %d failed\n", run, failed);
  return failed == 0 ? 0 : 1;
}
