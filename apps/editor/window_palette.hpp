#pragma once

// Paleta unica das molduras SpriteUI do editor. As cores vem da paleta em uso (Configuracoes > Appearance,
// palette::active()), lidas a cada quadro: trocar o tema muda o cabecalho, a borda e o corpo de todas as janelas.

#include "spriteui_host.hpp"
#include "sui_palette.hpp"
#include "sui_panel.hpp"

#include "SpriteUi/Widgets/WindowFramePaint.hpp"

namespace editor::window_palette {

namespace Wd = ToolEngine::SpriteUi::Widgets;

// Mistura linear entre duas cores da paleta (t = 0 e a, t = 1 e b), em sRGB sem pre-multiplicar.
inline palette::Rgb Mix(const palette::Rgb& a, const palette::Rgb& b, float t) {
  return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1.f};
}

inline Wd::WindowFrameColors Standard() {
  const palette::Palette& p = palette::active();
  Wd::WindowFrameColors c{};
  c.body = sui::PaletteColor(p.panel);
  c.border = sui::PaletteColor(p.border);
  c.borderDragging = sui::PaletteColor(p.accent);
  c.headerActive = sui::PaletteColor(p.surface);
  c.headerInactive = sui::PaletteColor(Mix(p.panel, p.surface, 0.5f));
  c.headerDragging = sui::PaletteColor(Mix(p.surface, p.accent, 0.3f));
  c.glyph = sui::PaletteColor(p.text);
  return c;
}

}  // namespace editor::window_palette
