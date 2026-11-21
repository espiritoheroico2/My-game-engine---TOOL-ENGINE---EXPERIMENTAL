#pragma once

// Paleta unica das molduras SpriteUI do editor: as cores da janela Inspector. Toda janela usa esta funcao,
// assim o cabecalho, a borda e o corpo ficam iguais em todo o editor.

#include "spriteui_host.hpp"

#include "SpriteUi/Widgets/WindowFramePaint.hpp"

namespace editor::window_palette {

namespace Wd = ToolEngine::SpriteUi::Widgets;

inline Wd::WindowFrameColors Standard() {
  Wd::WindowFrameColors c{};
  c.body = sui::Srgb(14.f / 255.f, 19.f / 255.f, 28.f / 255.f, 0.98f);
  c.border = sui::Srgb(0.160f, 0.220f, 0.330f);
  c.borderDragging = sui::Srgb(0.420f, 0.600f, 0.980f);
  c.headerActive = sui::Srgb(0.120f, 0.160f, 0.230f);
  c.headerInactive = sui::Srgb(0.090f, 0.120f, 0.180f);
  c.headerDragging = sui::Srgb(0.160f, 0.210f, 0.300f);
  c.glyph = sui::Srgb(0.910f, 0.918f, 0.933f);
  return c;
}

}  // namespace editor::window_palette
