#pragma once

// Icones pequenos da interface (abas de Configuracoes e itens da barra de menus). Cada icone e um desenho de 12 x 12
// pixels em texto: '#' e pixel aceso, '.' e apagado. Assim o icone nao depende de fonte, de imagem nem de SDL: o
// painel SpriteUI so transforma cada pixel aceso num quadrado solido (forEachPixel).
//
// Exemplo: Icon::Gear tem 12 linhas; a linha 0 ".....##....." acende dois pixels no topo, no meio da largura.
// Sem SDL, sem ImGui e sem SpriteUI: roda no ctest como Editor.UiIcons.

#include <array>
#include <cstddef>
#include <cstdint>

namespace editor::ui_icons {

inline constexpr int kIconSize = 12;   // lado do desenho, em pixels do icone

enum class Icon : std::uint8_t {
  None,
  // Abas de Configuracoes
  Brush,      // Aparencia
  Gear,       // Configuracoes (graficos e GPU)
  Gamepad,    // Jogo
  Lantern,    // Iluminacao
  Blocks,     // Otimizacao
  LetterG,    // Idioma
  Tag,        // Tags
  Folder,     // Projeto
  Orbit,      // Fisica
  // Janelas da barra Janela
  Hierarchy,
  Inspector,
  Console,
  Profiler,
  Mesh,       // Edit Mesh
  Scene,
  Assets,
  Scenes,
  Nodes,      // Narrative Controller
  Keyboard,   // Input Map
  Diagnostic, // SpriteUI diagnostics
  Help,
  Reset,      // Reset layout
};

// Os 12 desenhos de um icone. Icones sem desenho (None) tem todas as linhas apagadas.
using Bitmap = std::array<const char*, kIconSize>;

inline constexpr Bitmap kBrush = {
    "...........#", "..........##", ".........##.", "........##..", ".......##...", "......##....",
    ".....##.....", "....##......", "...##.......", "..##........", ".##.........", "##..........",
};
inline constexpr Bitmap kGear = {
    "....####....", "..##....##..", ".##.####.##.", "##.##..##.##", "##.#....#.##", "#....##....#",
    "#....##....#", "##.#....#.##", "##.##..##.##", ".##.####.##.", "..##....##..", "....####....",
};
inline constexpr Bitmap kGamepad = {
    "............", "............", ".##########.", "#..........#", "#.##.#..##.#", "#.##.#..#.##",
    "#.#..#.....#", "#..........#", ".##########.", "............", "............", "............",
};
inline constexpr Bitmap kLantern = {
    ".....#......", "....###.....", "...#####....", "...#...#....", "...#.#.#....", "...#.#.#....",
    "...#.#.#....", "...#.#.#....", "...#...#....", "...#####....", "..#######...", "............",
};
inline constexpr Bitmap kBlocks = {
    "..####......", "..#..#......", "..####......", "..####.####.", "..#..#.#..#.", "..####.####.",
    "......####..", "......#..#..", "......####..", "............", "............", "............",
};
inline constexpr Bitmap kLetterG = {
    "...######...", "..##....##..", ".##.........", ".##.........", ".##..#####..", ".##....##.#.",
    ".##....###.#", ".##.....##.#", "..##...###..", "...######...", "............", "............",
};
inline constexpr Bitmap kTag = {
    "..#####.....", ".#.....#....", "#.#.#.......", "#.........#.", "#........##.", "#.......##..",
    "#......##...", ".#...##.....", "..###.......", "............", "............", "............",
};
inline constexpr Bitmap kFolder = {
    "............", "##..........", "#.#########.", "#..........#", "#..........#", "#..........#",
    "#..........#", "#..........#", "############", "............", "............", "............",
};
inline constexpr Bitmap kOrbit = {
    "....##......", "..##..#.....", ".#...#......", "#....#.##...", "#...#.#..#..", "#.....#...#.",
    ".#..#...#...", "..##..#.....", "...###....#.", ".........#..", "...........#", "............",
};
inline constexpr Bitmap kHierarchy = {
    "#...........", "#.#.........", "#.##.#####..", "#.#.........", "#.##.#####..", "#.#.........",
    "#.##.#####..", "#...........", "............", "............", "............", "............",
};
inline constexpr Bitmap kInspector = {
    "############", "#..........#", "#.##.#.###.#", "#..........#", "#.#.##.#.##.", "#..........#",
    "#.###.##.#.#", "#..........#", "############", "............", "............", "............",
};
inline constexpr Bitmap kConsole = {
    "############", "#..........#", "#...........", "#..........#", "#.........#.", "#..........#",
    "#..........#", "#..........#", "############", "............", "............", "............",
};
inline constexpr Bitmap kProfiler = {
    "............", ".........#..", ".......#.#..", "...#...#.#..", "...#.#.#.#..", "...#.#.#.#..",
    "#..#.#.#.#.#", "############", "............", "............", "............", "............",
};
inline constexpr Bitmap kMesh = {
    "....#.......", "...#.#......", "..#...#.....", ".#######....", ".#.#...#....", "#...#...#...",
    "#.#...##....", ".#..##.#....", "...#...##...", "....###.##..", "...........#", "............",
};
inline constexpr Bitmap kScene = {
    "............", "...#........", "..#.#.......", ".#...#...#..", "#.....#.#.#.", "#.###.....#.",
    "#.....###..#", "#..........#", "############", "............", "............", "............",
};
inline constexpr Bitmap kAssets = {
    "............", "#.........#.", "#.#########.", "#..........#", "#.........##", "#..........#",
    "#.........##", "#..........#", "############", "............", "............", "............",
};
inline constexpr Bitmap kScenes = {
    "...#########", "..#.........", ".#.#######.#", "#.#.......#.", "#.#.......#.", "#.#.......#.",
    "#.#.......#.", "#.#.......#.", "#.#########.", "............", "............", "............",
};
inline constexpr Bitmap kNodes = {
    "............", "..####......", "..#..#...###", "..####...#.#", "......#..###", ".........#..",
    "...###...#..", "..#..#....#.", "..####.....#", "............", "............", "............",
};
inline constexpr Bitmap kKeyboard = {
    "............", "#..........#", "#.#.#.#.#.#.", "#..........#", "#.#.#.#.#.#.", "#..........#",
    "#.....#....#", "#..........#", "############", "............", "............", "............",
};
inline constexpr Bitmap kDiagnostic = {
    "............", "....####....", "...#....#...", "..#......#..", "..#.#..#.#..", "..#.#..#.#..",
    "..#......#..", "...#....#...", "....####....", "............", "............", "............",
};
inline constexpr Bitmap kHelp = {
    "....####....", "...#....#...", "..#......#..", "........#...", ".......#....", "......#.....",
    "......#.....", "............", "......#.....", "......#.....", "............", "............",
};
inline constexpr Bitmap kReset = {
    "....###.....", "..##...#....", ".#.......#..", "#.........##", "#.........#.", "#.........#.",
    ".#.......#..", "..##...##...", "....###.....", "............", "............", "............",
};

// Desenho de um icone. None (e qualquer valor fora da lista) devolve um desenho vazio.
[[nodiscard]] inline const Bitmap& bitmapOf(Icon icon) noexcept {
  static constexpr Bitmap kEmpty = {"............", "............", "............", "............", "............", "............",
                                    "............", "............", "............", "............", "............", "............"};
  switch (icon) {
    case Icon::Brush: return kBrush;
    case Icon::Gear: return kGear;
    case Icon::Gamepad: return kGamepad;
    case Icon::Lantern: return kLantern;
    case Icon::Blocks: return kBlocks;
    case Icon::LetterG: return kLetterG;
    case Icon::Tag: return kTag;
    case Icon::Folder: return kFolder;
    case Icon::Orbit: return kOrbit;
    case Icon::Hierarchy: return kHierarchy;
    case Icon::Inspector: return kInspector;
    case Icon::Console: return kConsole;
    case Icon::Profiler: return kProfiler;
    case Icon::Mesh: return kMesh;
    case Icon::Scene: return kScene;
    case Icon::Assets: return kAssets;
    case Icon::Scenes: return kScenes;
    case Icon::Nodes: return kNodes;
    case Icon::Keyboard: return kKeyboard;
    case Icon::Diagnostic: return kDiagnostic;
    case Icon::Help: return kHelp;
    case Icon::Reset: return kReset;
    case Icon::None: break;
  }
  return kEmpty;
}

// Chama fn(coluna, linha) para cada pixel aceso do icone. Coluna e linha vao de 0 a 11, a partir do canto superior
// esquerdo. O painel desenha um quadrado por pixel.
template <class Fn>
void forEachPixel(Icon icon, Fn&& fn) {
  const Bitmap& bmp = bitmapOf(icon);
  for (int row = 0; row < kIconSize; ++row) {
    const char* line = bmp[static_cast<std::size_t>(row)];
    for (int col = 0; col < kIconSize; ++col) {
      if (line[col] == '#') fn(col, row);
    }
  }
}

// Quantidade de pixels acesos (para o teste e para decidir se o icone tem desenho).
[[nodiscard]] inline int litPixels(Icon icon) noexcept {
  int n = 0;
  forEachPixel(icon, [&](int, int) { ++n; });
  return n;
}

} // namespace editor::ui_icons
