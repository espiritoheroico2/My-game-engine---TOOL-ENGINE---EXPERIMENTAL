#pragma once

// Pintura do fundo e da borda de um Canvas, igual na Game e na previa da Scene (sem janela).
//
// As cores ficam no componente como float[4] (RGBA, 0..1): o color picker escreve direto nelas, e o ponteiro continua
// valido enquanto o picker esta aberto. O pintor espera cor pre-multiplicada (RGB ja vezes o alfa), entao
// shapeColor faz essa conta. Sem isso, um fundo com alfa 14 sai branco solido.
//
// Exemplo: fundo azul (0, 0, 1) com opacidade 0,5 vai para o pintor como (0, 0, 0,5) com alfa 0,5.

#include "view_shapes.hpp"

#include <algorithm>

namespace editor::canvas_paint {

// Cor RGBA 0..1 para a forma, pre-multiplicada.
inline view::Color shapeColor(const float c[4]) {
  const float a = std::clamp(c[3], 0.f, 1.f);
  return view::rgbaF(c[0] * a, c[1] * a, c[2] * a, a);
}

// Desenha o fundo e a borda do canvas no retangulo (a, b), em pixels de janela. radius e borderWidth ja estao na escala
// do canvas. Sem alfa no fundo e sem borda, nao desenha nada (o canvas e so os elementos).
inline void drawBase(view::ShapeList& out, view::Pixel a, view::Pixel b, const float bg[4], const float edge[4], float radius,
                     float borderWidth) {
  const bool hasBg = bg[3] > 0.f;
  const bool hasBorder = borderWidth > 0.f && edge[3] > 0.f;
  if (hasBorder) {
    out.panel(a, b, radius, hasBg ? shapeColor(bg) : 0u, shapeColor(edge), borderWidth);
  } else if (hasBg) {
    out.rectFilled(a, b, shapeColor(bg), radius);
  }
}

}  // namespace editor::canvas_paint
