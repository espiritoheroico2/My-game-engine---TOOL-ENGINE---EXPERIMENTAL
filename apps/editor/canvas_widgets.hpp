#pragma once

// Desenho dos elementos compostos do Canvas (Window, Menu, List, Inventory, ListContainer), so com formas simples da
// ShapeList: paineis, retangulos, linhas e texto. Sem janela e sem Editor, para testar o que sai na lista.
//
// Todos recebem o retangulo (a, b) em pixels de tela, a cor do elemento, a fonte `fs` (pixels) e `k`, a escala do
// canvas (pixels de tela por pixel de projeto), que dimensiona margens e linhas.
//
// Limite: o pintor nao recorta por elemento (a lista de recortes e do painel). Por isso o ListContainer e uma caixa
// com moldura e so desenha as linhas que cabem inteiras dentro dela (o resto some em vez de ser cortado no meio).
//
// Exemplo: Menu com 3 entradas numa caixa de 100 px de altura: 3 botoes de (100 - 2 * gap) / 3 px, um sobre o outro.

#include "canvas_paint.hpp"

#include <algorithm>
#include <string>

namespace editor::canvas_widgets {

// Um texto de entrada (Menu, List, ListContainer, slot do Inventory).
using Entries = const char (*)[32];

// Cor RGBA 0..1 multiplicada nos canais RGB por m (alfa mantido), para variar o tom do mesmo elemento.
inline view::Color shade(const float c[4], float m, float alphaScale = 1.f) {
  return view::rgbaF(c[0] * m, c[1] * m, c[2] * m, c[3] * alphaScale);
}

// Window: painel com faixa de titulo no topo (altura 2 linhas de fonte) e corpo livre embaixo.
inline void drawWindow(view::ShapeList& out, view::Pixel a, view::Pixel b, const float color[4], float radius, float fs,
                       const std::string& title) {
  canvas_paint::drawBox(out, a, b, canvas_paint::shapeColor(color), view::Rgba(255, 255, 255, 140), radius, true);
  const float th = std::min(fs * 2.f, (b.y - a.y) * 0.5f);   // faixa de titulo, no maximo metade da janela
  out.rectFilled({a.x + 1.f, a.y + 1.f}, {b.x - 1.f, a.y + th}, shade(color, 0.45f), 0.f);
  out.line({a.x + 1.f, a.y + th}, {b.x - 1.f, a.y + th}, 1.f, view::Rgba(255, 255, 255, 110));
  if (!title.empty()) out.text({a.x + fs * 0.6f, a.y + (th - fs) * 0.5f}, title, fs, view::Rgba(255, 255, 255), false, b.x - a.x - fs);
}

// Menu: botoes de mesma altura em coluna, rotulo centrado em cada um. Sem entradas, desenha um botao vazio.
inline void drawMenu(view::ShapeList& out, view::Pixel a, view::Pixel b, const float color[4], float radius, float fs, float k,
                     Entries entries, int count) {
  const int n = std::max(1, count);
  const float gap = 4.f * k;
  const float h = std::max(1.f, ((b.y - a.y) - gap * static_cast<float>(n - 1)) / static_cast<float>(n));   // altura igual
  for (int i = 0; i < n; ++i) {
    const float y0 = a.y + (h + gap) * static_cast<float>(i);
    canvas_paint::drawBox(out, {a.x, y0}, {b.x, y0 + h}, canvas_paint::shapeColor(color), view::Rgba(255, 255, 255, 160), radius, true);
    if (i < count) out.text({(a.x + b.x) * 0.5f, y0 + (h - fs) * 0.5f}, entries[i], fs, view::Rgba(0, 0, 0), true, b.x - a.x);
  }
}

// List: fundo escuro, linhas de texto (cor do elemento) com uma linha separadora entre elas. Linha = 1,8 x a fonte.
// So as linhas que cabem inteiras sao desenhadas.
inline void drawList(view::ShapeList& out, view::Pixel a, view::Pixel b, const float color[4], float radius, float fs,
                     Entries entries, int count) {
  out.panel(a, b, canvas_paint::clampRadius(radius, b.x - a.x, b.y - a.y), view::Rgba(0, 0, 0, 120), view::Rgba(255, 255, 255, 90), 1.f);
  const float row = fs * 1.8f;
  for (int i = 0; i < count && a.y + row * static_cast<float>(i + 1) <= b.y; ++i) {
    const float y0 = a.y + row * static_cast<float>(i);
    out.text({a.x + fs * 0.5f, y0 + (row - fs) * 0.5f}, entries[i], fs, canvas_paint::shapeColor(color), false, b.x - a.x - fs);
    if (i > 0) out.line({a.x + 2.f, y0}, {b.x - 2.f, y0}, 1.f, view::Rgba(255, 255, 255, 60));
  }
}

// Inventory: grade rows x cols de slots quadrados que preenchem a caixa (cada slot = o menor lado da celula menos o
// vao), centrada. O rotulo da entrada i, se houver, aparece no slot i (linha por linha, da esquerda para a direita).
inline void drawInventory(view::ShapeList& out, view::Pixel a, view::Pixel b, const float color[4], float radius, float fs, float k,
                          int rows, int cols, Entries entries, int count, const std::string& backImg = {},
                          const std::string& frontImg = {}, bool preserveAspect = false) {
  // backImg/frontImg: caminhos absolutos; vazio = quadrado de sempre. A de fundo vai primeiro e a da frente por cima
  // dela, ambas abaixo do rotulo. preserveAspect mantem a proporcao das imagens dentro do slot.
  rows = std::max(1, rows);
  cols = std::max(1, cols);
  const float gap = 3.f * k;
  const float cw = (b.x - a.x) / static_cast<float>(cols), ch = (b.y - a.y) / static_cast<float>(rows);
  const float side = std::max(1.f, std::min(cw, ch) - gap);
  const float ox = a.x + ((b.x - a.x) - cw * static_cast<float>(cols)) * 0.5f + (cw - side) * 0.5f;
  const float oy = a.y + ((b.y - a.y) - ch * static_cast<float>(rows)) * 0.5f + (ch - side) * 0.5f;
  for (int r = 0; r < rows; ++r) {
    for (int c = 0; c < cols; ++c) {
      const view::Pixel p{ox + cw * static_cast<float>(c), oy + ch * static_cast<float>(r)};
      const view::Pixel q{p.x + side, p.y + side};
      if (backImg.empty()) out.panel(p, q, canvas_paint::clampRadius(radius, side, side), shade(color, 0.25f, 0.8f), shade(color, 1.f), 1.f);
      else canvas_paint::drawImageBox(out, p, q, backImg, view::Rgba(255, 255, 255), preserveAspect);
      if (!frontImg.empty()) canvas_paint::drawImageBox(out, p, q, frontImg, view::Rgba(255, 255, 255), preserveAspect);
      const int idx = r * cols + c;
      if (idx < count && entries[idx][0]) out.text({p.x + side * 0.5f, p.y + (side - fs) * 0.5f}, entries[idx], fs * 0.8f, view::Rgba(255, 255, 255), true, side);
    }
  }
}

// ListContainer: caixa com moldura e linhas de texto empilhadas. Sem recorte real: linhas que passam da borda de
// baixo nao sao desenhadas (a ultima linha so aparece se cabe inteira). Retorna quantas linhas couberam.
inline int drawListContainer(view::ShapeList& out, view::Pixel a, view::Pixel b, const float color[4], float radius, float fs,
                             Entries entries, int count) {
  out.panel(a, b, canvas_paint::clampRadius(radius, b.x - a.x, b.y - a.y), view::Rgba(0, 0, 0, 110), canvas_paint::shapeColor(color), 1.f);
  const float row = fs * 1.8f, pad = 3.f;
  int shown = 0;
  for (int i = 0; i < count; ++i) {
    const float y0 = a.y + pad + row * static_cast<float>(i);
    if (y0 + row > b.y - pad) break;
    out.text({a.x + fs * 0.5f, y0 + (row - fs) * 0.5f}, entries[i], fs, canvas_paint::shapeColor(color), false, b.x - a.x - fs);
    ++shown;
  }
  return shown;
}

}  // namespace editor::canvas_widgets
