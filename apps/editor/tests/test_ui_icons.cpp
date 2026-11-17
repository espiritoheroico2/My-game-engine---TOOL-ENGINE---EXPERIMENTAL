// Teste dos icones da interface (ui_icons.hpp): cada desenho tem 12 x 12 pixels, todo icone real tem pixels acesos e
// None fica apagado. Sem janela; roda no ctest como Editor.UiIcons.
#include "ui_icons.hpp"

#include <cstdio>
#include <cstring>

namespace {

using editor::ui_icons::Icon;
using editor::ui_icons::kIconSize;

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

// Todo icone (exceto None) tem pixels acesos e cada linha tem exatamente kIconSize colunas.
void TestEveryIconIsAFullBitmap() {
  const Icon all[] = {Icon::Brush,     Icon::Gear,      Icon::Gamepad,  Icon::Lantern,  Icon::Blocks,   Icon::LetterG,
                      Icon::Tag,       Icon::Folder,    Icon::Orbit,    Icon::Hierarchy, Icon::Inspector, Icon::Console,
                      Icon::Profiler,  Icon::Mesh,      Icon::Scene,    Icon::Assets,   Icon::Scenes,   Icon::Nodes,
                      Icon::Keyboard,  Icon::Diagnostic, Icon::Help,    Icon::Reset};
  for (Icon icon : all) {
    CHECK(editor::ui_icons::litPixels(icon) > 0);
    const auto& bmp = editor::ui_icons::bitmapOf(icon);
    for (const char* row : bmp) CHECK(std::strlen(row) == static_cast<std::size_t>(kIconSize));
  }
}

void TestNoneIsBlank() {
  CHECK(editor::ui_icons::litPixels(Icon::None) == 0);
}

// Pixel fora do desenho nao acende: a conta de forEachPixel bate com a contagem de '#'.
void TestForEachPixelVisitsLitCellsOnly() {
  int visits = 0;
  bool inside = true;
  editor::ui_icons::forEachPixel(Icon::Gear, [&](int col, int row) {
    ++visits;
    inside = inside && col >= 0 && col < kIconSize && row >= 0 && row < kIconSize;
  });
  CHECK(visits == editor::ui_icons::litPixels(Icon::Gear));
  CHECK(inside);
}

}  // namespace

int main() {
  TestEveryIconIsAFullBitmap();
  TestNoneIsBlank();
  TestForEachPixelVisitsLitCellsOnly();
  if (failed == 0) {
    std::printf("ui_icons: OK (%d checks)\n", run);
    return 0;
  }
  std::printf("ui_icons: %d de %d falharam\n", failed, run);
  return 1;
}
