#pragma once

// Cores derivadas da paleta em uso (Configuracoes > Appearance) para janelas e barras. Um lugar so: o painel
// refaz as suas cores a partir daqui a cada quadro, entao trocar o tema repinta tudo (antes cada painel tinha
// cores fixas e so as molduras mudavam). Sem dependencia de SpriteUI, para o teste rodar sem janela.
#include "sui_palette.hpp"

namespace editor::theme_colors {

using palette::Rgb;

// Cores derivadas da paleta para janelas e barras (um campo por uso: botoes, campos, linhas, cabecalho da moldura...).
struct Colors {
  Rgb panel, surface, text, textDim, accent, border, error;
  Rgb scroll;                                     // polegar da barra (alfa 0.75)
  Rgb btn, btnHover, btnPressed, btnOn;           // botoes
  Rgb field, fieldFocus, selection, caret;        // campos de texto
  Rgb rowHover, rowSel;                           // linhas de listas
  Rgb heading;                                    // titulos de secao
  Rgb headerActive, headerInactive, headerDragging, borderDragging;   // cabecalho e borda da moldura
};

// Copia da cor com o alfa trocado.
inline Rgb WithAlpha(Rgb c, float a) {
  c.a = a;
  return c;
}

// Deriva todas as cores da paleta `p` misturando painel, superficie, texto e destaque em proporcoes fixas.
inline Colors From(const palette::Palette& p) {
  Colors c{};
  c.panel = p.panel;
  c.surface = p.surface;
  c.text = p.text;
  c.textDim = p.textDim;
  c.accent = p.accent;
  c.border = p.border;
  c.error = p.error;
  c.scroll = WithAlpha(p.scroll, 0.75f);
  c.btn = palette::mixRgb(p.surface, p.text, 0.08f);
  c.btnHover = palette::mixRgb(p.surface, p.text, 0.18f);
  c.btnPressed = palette::mixRgb(p.surface, p.panel, 0.6f);
  c.btnOn = palette::mixRgb(p.panel, p.accent, 0.7f);
  c.field = palette::mixRgb(p.panel, p.surface, 0.6f);
  c.fieldFocus = palette::mixRgb(p.surface, p.accent, 0.2f);
  c.selection = WithAlpha(p.accent, 0.45f);
  c.caret = p.text;
  c.rowHover = palette::mixRgb(p.panel, p.text, 0.07f);
  c.rowSel = WithAlpha(palette::mixRgb(p.panel, p.accent, 0.65f), 0.9f);
  c.heading = palette::mixRgb(p.accent, p.text, 0.4f);
  c.headerActive = p.surface;
  c.headerInactive = palette::mixRgb(p.panel, p.surface, 0.5f);
  c.headerDragging = palette::mixRgb(p.surface, p.accent, 0.3f);
  c.borderDragging = p.accent;
  return c;
}

// Assinatura da paleta: muda quando qualquer token muda. Paineis que guardam o desenho em cache (chave de
// reconstrucao) incluem isto na chave para refazer o desenho quando o tema muda.
inline unsigned long long Signature(const palette::Palette& p) {
  const Rgb* all[] = {&p.panel, &p.surface, &p.text, &p.textDim, &p.accent, &p.scroll, &p.border, &p.error};
  unsigned long long h = 1469598103934665603ull;
  for (const Rgb* c : all) {
    for (float v : {c->r, c->g, c->b}) {
      h ^= static_cast<unsigned long long>(v * 4096.f + 0.5f);
      h *= 1099511628211ull;
    }
  }
  return h;
}
inline unsigned long long Signature() { return Signature(palette::active()); }

// Da paleta em uso agora.
inline Colors Active() { return From(palette::active()); }

}  // namespace editor::theme_colors
