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
#include <cmath>
#include <string>

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

// Sombra projetada: deslocamento (x, y) e suavizacao (blur) ja na escala da tela, cor RGBA 0..1. Desligada = nada.
struct Shadow {
  bool on = false;
  float x = 2.f, y = 2.f;   // deslocamento em pixels
  float blur = 0.f;         // raio de suavizacao em pixels (0 = borda dura)
  float color[4] = {0.f, 0.f, 0.f, 0.6f};
};

// Quantidade de camadas da sombra: 1 sem blur, ate kMaxShadowSteps com blur (uma camada a cada ~2 px).
inline constexpr int kMaxShadowSteps = 4;
inline int shadowSteps(float blur) {
  if (blur <= 0.f) return 1;
  return std::clamp(static_cast<int>(std::ceil(blur / 2.f)) + 1, 2, kMaxShadowSteps);
}

// Sombra de uma caixa (a, b): copias do retangulo deslocadas por (x, y), da mais larga e fraca para a mais justa e
// forte. O alfa total fica em torno do alfa da cor (cada camada soma alfa/steps). Sem shader: so retangulos.
inline void drawShadowBox(view::ShapeList& out, view::Pixel a, view::Pixel b, float radius, const Shadow& sh) {
  if (!sh.on || sh.color[3] <= 0.f) return;
  const int steps = shadowSteps(sh.blur);
  for (int i = 0; i < steps; ++i) {
    const float grow = steps == 1 ? 0.f : sh.blur * static_cast<float>(steps - 1 - i) / static_cast<float>(steps - 1);
    const float alpha = sh.color[3] / static_cast<float>(steps);
    const view::Pixel pa{a.x + sh.x - grow, a.y + sh.y - grow}, pb{b.x + sh.x + grow, b.y + sh.y + grow};
    const float r = clampRadius(radius + grow, pb.x - pa.x, pb.y - pa.y);
    out.rectFilled(pa, pb, view::rgbaF(sh.color[0], sh.color[1], sh.color[2], alpha), r);
  }
}

// Sombra de um texto: copias deslocadas por (x, y). Com blur, cada camada repete o texto em 4 direcoes (cruz) a uma
// distancia crescente, com alfa menor. `fn` grava uma copia do texto na posicao e cor dadas (pode ser com quebra).
template <class Fn>
inline void drawShadowText(view::Pixel at, const Shadow& sh, Fn fn) {
  if (!sh.on || sh.color[3] <= 0.f) return;
  const int steps = shadowSteps(sh.blur);
  if (steps == 1) { fn(view::Pixel{at.x + sh.x, at.y + sh.y}, view::rgbaF(sh.color[0], sh.color[1], sh.color[2], sh.color[3])); return; }
  for (int i = 0; i < steps; ++i) {
    const float d = sh.blur * static_cast<float>(steps - 1 - i) / static_cast<float>(steps - 1);   // do mais longe ao centro
    const float alpha = sh.color[3] * (i == steps - 1 ? 0.6f : 0.4f / static_cast<float>(steps - 1));
    const view::Color c = view::rgbaF(sh.color[0], sh.color[1], sh.color[2], alpha);
    if (d <= 0.f) { fn(view::Pixel{at.x + sh.x, at.y + sh.y}, c); continue; }
    const float ox[4] = {d, -d, 0.f, 0.f}, oy[4] = {0.f, 0.f, d, -d};
    for (int k = 0; k < 4; ++k) fn(view::Pixel{at.x + sh.x + ox[k], at.y + sh.y + oy[k]}, c);
  }
}

// Desenha uma imagem (caminho absoluto) no retangulo (a, b) com a cor de tinta `tint`. O pintor cria a textura e, se o
// arquivo falhar, desenha um placeholder visivel. O raio de canto nao vale para a imagem (o SDL recorta so retangulos).
// preserveAspect = true: a imagem entra inteira no retangulo mantendo a proporcao dela (centrada, sem esticar).
inline void drawImageBox(view::ShapeList& out, view::Pixel a, view::Pixel b, const std::string& absPath, view::Color tint,
                         bool preserveAspect = false) {
  out.image(a, b, absPath, tint, preserveAspect);
}

}  // namespace editor::canvas_paint
