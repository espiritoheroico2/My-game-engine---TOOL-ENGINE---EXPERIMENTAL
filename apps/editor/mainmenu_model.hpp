#pragma once

// Modelo da barra de menus principal (Arquivo, Editar, Janela, Interface, Opcoes) e dos botoes Play, Pausa e Parar.
// Diz o que cada menu mostra (texto, atalho, marca, desabilitado) e qual pedido cada item dispara. Nao depende de
// SDL, ImGui nem SpriteUI: mainmenu_panel.hpp desenha e main.cpp executa os pedidos.
// Roda no ctest como Editor.MainMenuModel.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace editor::mainmenu {

using Translate = const char* (*)(const char*);

// O que um item pede ao Editor. Toggle* inverte uma janela ou opcao; Settings abre a aba `tab` das configuracoes.
enum class Command : std::uint8_t {
  None,
  NewScene,
  SaveScene,
  ImportAsset,
  NewNarrative,
  SaveNarrative,
  Settings,
  EditorInfo,
  ExportGame,
  Quit,
  Undo,
  Redo,
  ToggleHierarchy,
  ToggleInspector,
  ToggleConsole,
  ToggleProfiler,
  ToggleEditMesh,
  TogglePaintMesh,
  ToggleScene,
  ToggleGame,
  ToggleAssets,
  ToggleScenes,
  ToggleSettings,
  ToggleNarrative,
  ToggleInputMap,
  ToggleSpriteUiDiagnostics,
  ToggleHelp,
  ResetLayout,
  SaveLayout,
  ToggleLockInterface,
  ToggleDockWithShift,
  ToggleLogKeys,
  Play,
  Stop,
  Pause,
};

struct Request {
  Command command = Command::None;
  int tab = 0;   // so para Settings
};

// Estado do Editor que a barra mostra. Qualquer coisa que muda texto, marca ou habilitado entra aqui.
struct MenuState {
  bool playing = false;
  bool paused = false;
  bool canUndo = false;
  bool canRedo = false;
  bool hierarchy = false;
  bool inspector = false;
  bool console = false;
  bool profiler = false;
  bool editMesh = false;
  bool paintMesh = false;
  bool scene = false;
  bool game = false;
  bool assets = false;
  bool scenes = false;
  bool settings = false;
  bool narrative = false;
  bool inputMap = false;
  bool spriteUiDiagnostics = false;
  bool help = false;
  bool lockInterface = false;
  bool dockWithShift = false;
  bool logKeys = false;
  std::string language;   // muda o texto: os menus sao refeitos quando o idioma muda
  bool operator==(const MenuState&) const = default;
};

struct ItemSpec {
  std::string label;      // ja traduzido; itens de janela trazem "[x] " ou "[ ] " antes do nome
  std::string shortcut;   // ex.: "Ctrl+S"; vazio = sem atalho
  bool separator = false;
  bool disabled = false;
};

struct MenuSpec {
  std::string title;
  std::vector<ItemSpec> items;
};

struct BarButton {
  Command command = Command::None;
  std::string label;
};

namespace detail {

// Uma linha da tabela. label == nullptr e um separador.
struct Def {
  const char* label;
  const char* shortcut;
  Command command;
  int tab;
};

struct MenuDef {
  const char* title;
  std::vector<Def> items;
};

inline const std::vector<MenuDef>& definitions() {
  static const std::vector<MenuDef> defs = {
      {"File",
       {{"New scene...", "", Command::NewScene, 0},
        {"Save scene", "Ctrl+S", Command::SaveScene, 0},
        {"Import asset...", "", Command::ImportAsset, 0},
        {nullptr, nullptr, Command::None, 0},
        {"New narrative graph", "", Command::NewNarrative, 0},
        {"Save graph", "", Command::SaveNarrative, 0},
        {nullptr, nullptr, Command::None, 0},
        {"Settings...", "", Command::Settings, 0},
        {"Editor info...", "", Command::EditorInfo, 0},
        {"Export game...", "", Command::ExportGame, 0},
        {"Quit", "", Command::Quit, 0}}},
      {"Edit", {{"Undo", "Ctrl+Z", Command::Undo, 0}, {"Redo", "Ctrl+Y", Command::Redo, 0}}},
      {"Window",
       {{"Hierarchy", "", Command::ToggleHierarchy, 0},
        {"Inspector", "", Command::ToggleInspector, 0},
        {"Console", "", Command::ToggleConsole, 0},
        {"Profiler", "", Command::ToggleProfiler, 0},
        {"Edit Mesh", "", Command::ToggleEditMesh, 0},
        {"Paint Mesh", "", Command::TogglePaintMesh, 0},
        {"Scene", "", Command::ToggleScene, 0},
        {"Game", "", Command::ToggleGame, 0},
        {"Assets", "", Command::ToggleAssets, 0},
        {"Scenes", "", Command::ToggleScenes, 0},
        {"Settings", "", Command::ToggleSettings, 0},
        {"Narrative Controller", "", Command::ToggleNarrative, 0},
        {"Input Map", "", Command::ToggleInputMap, 0},
        {"SpriteUI diagnostics", "", Command::ToggleSpriteUiDiagnostics, 0},
        {"Help", "F1", Command::ToggleHelp, 0},
        {nullptr, nullptr, Command::None, 0},
        {"Reset layout", "", Command::ResetLayout, 0}, {"Save layout", "", Command::SaveLayout, 0}}},
      {"Interface",
       {{"Lock interface", "", Command::ToggleLockInterface, 0},
        {"Dock only while holding Shift", "", Command::ToggleDockWithShift, 0},
        {"Log keyboard events (debug)", "", Command::ToggleLogKeys, 0},
        {nullptr, nullptr, Command::None, 0},
        {"Reset layout", "", Command::ResetLayout, 0}, {"Save layout", "", Command::SaveLayout, 0}}},
      {"Options",
       {{"Appearance and colors...", "", Command::Settings, 0},
        {"Language...", "", Command::Settings, 1},
        {"Graphics API...", "", Command::Settings, 2},
        {"Game view (grid, split)...", "", Command::Settings, 3},
        {"Lighting and shadows...", "", Command::Settings, 4},
        {"Tags...", "", Command::Settings, 5},
        {"Project and export preset...", "", Command::Settings, 6},
        {"Physics (world gravity)...", "", Command::Settings, 7}}},
  };
  return defs;
}

// Valor de uma janela ou opcao marcavel: nullopt para os itens que nao sao marcaveis.
inline std::optional<bool> toggleValue(const MenuState& s, Command c) {
  switch (c) {
    case Command::ToggleHierarchy: return s.hierarchy;
    case Command::ToggleInspector: return s.inspector;
    case Command::ToggleConsole: return s.console;
    case Command::ToggleProfiler: return s.profiler;
    case Command::ToggleEditMesh: return s.editMesh;
    case Command::TogglePaintMesh: return s.paintMesh;
    case Command::ToggleScene: return s.scene;
    case Command::ToggleGame: return s.game;
    case Command::ToggleAssets: return s.assets;
    case Command::ToggleScenes: return s.scenes;
    case Command::ToggleSettings: return s.settings;
    case Command::ToggleNarrative: return s.narrative;
    case Command::ToggleInputMap: return s.inputMap;
    case Command::ToggleSpriteUiDiagnostics: return s.spriteUiDiagnostics;
    case Command::ToggleHelp: return s.help;
    case Command::ToggleLockInterface: return s.lockInterface;
    case Command::ToggleDockWithShift: return s.dockWithShift;
    case Command::ToggleLogKeys: return s.logKeys;
    default: return std::nullopt;
  }
}

// Desfazer e refazer ficam cinza sem nada para desfazer e com o jogo rodando (como no ImGui).
inline bool isDisabled(const MenuState& s, Command c) {
  if (c == Command::Undo) return !s.canUndo || s.playing;
  if (c == Command::Redo) return !s.canRedo || s.playing;
  return false;
}

inline ItemSpec itemFor(const MenuState& s, const Def& def, Translate tr) {
  ItemSpec item;
  if (!def.label) {
    item.separator = true;
    return item;
  }
  const std::optional<bool> on = toggleValue(s, def.command);
  item.label = on ? std::string(*on ? "[x] " : "[ ] ") + tr(def.label) : std::string(tr(def.label));
  item.shortcut = def.shortcut;
  item.disabled = isDisabled(s, def.command);
  return item;
}

}  // namespace detail

// Menus da barra, na ordem da barra. Os rotulos ja vem traduzidos por `tr`.
inline std::vector<MenuSpec> buildMenus(const MenuState& state, Translate tr) {
  std::vector<MenuSpec> menus;
  for (const detail::MenuDef& menu : detail::definitions()) {
    MenuSpec spec;
    spec.title = tr(menu.title);
    for (const detail::Def& def : menu.items) spec.items.push_back(detail::itemFor(state, def, tr));
    menus.push_back(std::move(spec));
  }
  return menus;
}

// Pedido do item `item` do menu `menu` (indices de buildMenus). Fora de faixa ou separador: Command::None.
inline Request requestAt(std::size_t menu, std::size_t item) {
  const auto& defs = detail::definitions();
  if (menu >= defs.size() || item >= defs[menu].items.size()) return {};
  const detail::Def& def = defs[menu].items[item];
  if (!def.label) return {};
  return Request{def.command, def.tab};
}

// Botoes da faixa, depois dos menus: Play (ou Parar) e, com o jogo rodando, Pausa (ou Continuar).
inline std::vector<BarButton> barButtons(const MenuState& state, Translate tr) {
  std::vector<BarButton> out;
  out.push_back(BarButton{state.playing ? Command::Stop : Command::Play, tr(state.playing ? "Stop" : "Play")});
  if (state.playing) out.push_back(BarButton{Command::Pause, tr(state.paused ? "Resume" : "Pause")});
  return out;
}

}  // namespace editor::mainmenu
