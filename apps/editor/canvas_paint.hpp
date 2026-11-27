#pragma once

// Pintura do fundo e da borda de um Canvas, igual na Game e na previa da Scene (sem janela).
//
// As cores ficam no componente como float[4] (RGBA, 0..1): o color picker escreve direto nelas, e o ponteiro continua
// valido enquanto o picker esta aberto. O pintor (view_painter.hpp) espera cor RETA (RGB sem multiplicar pelo alfa) e
// quem faz a mistura e o SDL com BLEND reto. Por isso shapeColor so copia os canais.
//
// Exemplo: fundo azul (0, 0, 1) com opacidade 0,5 vai para o pintor como (0, 0, 1) com alfa 0,5: metade azul, metade
// do que estiver por baixo. Antes, o RGB era multiplicado pelo alfa duas vezes (aqui e no pintor), escurecendo as cores.

#include "view_shapes.hpp"

#include <algorithm>

namespace editor::canvas_paint {

// Cor RGBA 0..1 para a forma, em cor reta (o alfa vale na mistura).
inline view::Color shapeColor(const float c[4]) {
  return view::rgbaF(c[0], c[1], c[2], std::clamp(c[3], 0.f, 1.f));
}

// Raio de canto limitado a metade do menor lado (w, h em pixels): raio grande nao inverte a forma. Negativo vira 0.
inline float clampRadius(float radius, float w, float h) {
  const float half = std::max(0.f, std::min(w, h)) * 0.5f;
  return std::clamp(radius, 0.f, half);
}

// Desenha um elemento de UI (painel/botao) com cantos redondos. radius ja na escala do canvas. Sem raio e sem borda,
// e o retangulo cheio de sempre; com raio ou borda usa o painel da SpriteUI (fundo + contorno arredondado).
inline void drawBox(view::ShapeList& out, view::Pixel a, view::Pixel b, view::Color fill, view::Color edge, float radius,
                    bool withEdge) {
  const float r = clampRadius(radius, b.x - a.x, b.y - a.y);
  if (withEdge && r > 0.f) out.panel(a, b, r, fill, edge, 1.f);
  else out.rectFilled(a, b, fill, r);
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
