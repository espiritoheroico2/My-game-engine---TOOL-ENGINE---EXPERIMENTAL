#pragma once

// Cores dos paineis de arquivos (Scenes e Assets) a partir da paleta em uso (Configuracoes > Appearance).
// Antes cada janela tinha cores fixas (cinza) e o conteudo herdava a cor padrao da moldura. Agora as duas
// janelas leem o mesmo mapeamento da paleta, em um lugar so.

#include "sui_panel.hpp"

namespace editor::file_panel_colors {

namespace SU = ToolEngine::SpriteUi;

struct Colors {
  SU::Vec4 field{};
  SU::Vec4 fieldFocus{};
  SU::Vec4 rowSelected{};
  SU::Vec4 button{};
  SU::Vec4 buttonHover{};
  SU::Vec4 buttonDanger{};
  SU::Vec4 buttonActive{};   // modo de lista/icones em uso
  SU::Vec4 help{};
  SU::Vec4 disabled{};
  SU::Vec4 text{};
  SU::Vec4 textDim{};
  SU::Vec4 caret{};
  SU::Vec4 warning{};
  SU::Vec4 ink{};            // letras sobre os icones
};

// Mistura a cor `a` com `b` (t = 0 devolve a, t = 1 devolve b). Alfa sempre 1.
inline palette::Rgb MixRgb(const palette::Rgb& a, const palette::Rgb& b, float t) {
  return palette::Rgb{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1.f};
}

// Cores dos paineis de arquivos a partir da paleta ativa: campos e linhas sobre a superficie, botoes na cor de destaque.
inline Colors FromPalette() {
  const palette::Palette& p = palette::active();
  Colors c;
  c.field = sui::PaletteColor(p.surface);
  c.fieldFocus = sui::PaletteColor(MixRgb(p.surface, p.text, 0.08f));
  c.rowSelected = sui::PaletteColor(MixRgb(p.surface, p.accent, 0.45f));
  c.button = sui::PaletteColor(MixRgb(p.surface, p.accent, 0.6f));
  c.buttonHover = sui::PaletteColor(MixRgb(p.surface, p.accent, 0.8f));
  c.buttonDanger = sui::PaletteColor(MixRgb(p.surface, p.error, 0.6f));
  c.buttonActive = c.buttonHover;
  c.help = sui::PaletteColor(p.accent);
  c.disabled = sui::PaletteColor(MixRgb(p.panel, p.surface, 0.5f));
  c.text = sui::PaletteColor(p.text);
  c.textDim = sui::PaletteColor(p.textDim);
  c.caret = sui::PaletteColor(p.text);
  c.warning = sui::PaletteColor(p.error);
  c.ink = sui::PaletteColor(p.panel);
  return c;
}

}  // namespace editor::file_panel_colors
