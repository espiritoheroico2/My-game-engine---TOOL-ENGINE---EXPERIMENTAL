#pragma once

// Janela Jogo: regras de enquadramento que nao dependem de janela. Encaixa a imagem da camera de jogo na area da
// janela (letterbox: proporcao mantida, barras nas laterais ou em cima e embaixo), divide a area entre duas
// cameras principais e calcula o recorte da textura no modo preencher. A janela SpriteUI (game_view_panel.hpp)
// so desenha. Sem SpriteUI: roda no ctest como Editor.GameViewModel.
//
// Exemplo: a camera do jogo renderiza 16:9 e a janela tem 800 x 800. A imagem fica com 800 x 450, centrada, e
// sobram 175 px de barra preta em cima e 175 px embaixo. Se a janela ficar 1600 x 900, a imagem vira 1600 x 900 e
// as barras vao para as laterais (400 px de cada lado).
//
// Como funciona:
//   letterbox:   maior retangulo com a proporcao da imagem, centrado na area.
//   splitCells:  uma ou duas cameras; duas dividem a area lado a lado (vertical) ou em cima e embaixo, com folga.
//   fillUv:      recorte centrado da textura para cobrir a area sem distorcer (o que sobra e cortado).

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

namespace editor::game_view_model {

struct Rect {
  float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
};

// Maior retangulo com a proporcao srcW / srcH cabendo em area, centrado. Imagem sem tamanho valido ocupa a area toda.
inline Rect letterbox(Rect area, float srcW, float srcH) {
  if (!(srcW > 0.f) || !(srcH > 0.f) || !(area.w > 0.f) || !(area.h > 0.f)) return area;
  const float k = std::min(area.w / srcW, area.h / srcH);
  const float w = srcW * k, h = srcH * k;
  return {area.x + (area.w - w) * 0.5f, area.y + (area.h - h) * 0.5f, w, h};
}

// Celulas das cameras dentro de area. count = 1 usa a area toda; count = 2 divide com folga `gap` entre as duas.
// vertical = lado a lado (a folga corre na horizontal); senao, uma em cima da outra.
inline std::vector<Rect> splitCells(Rect area, std::size_t count, bool vertical, float gap) {
  if (count == 0) return {};
  if (count == 1) return {area};
  if (vertical) {
    const float cw = std::max(0.f, (area.w - gap) * 0.5f);
    return {{area.x, area.y, cw, area.h}, {area.x + cw + gap, area.y, cw, area.h}};
  }
  const float ch = std::max(0.f, (area.h - gap) * 0.5f);
  return {{area.x, area.y, area.w, ch}, {area.x, area.y + ch + gap, area.w, ch}};
}

// Regiao da textura (u, v, largura, altura) que cobre area sem distorcer. O que passa da proporcao e cortado ao centro.
inline std::array<float, 4> fillUv(Rect area, float srcW, float srcH) {
  if (!(srcW > 0.f) || !(srcH > 0.f) || !(area.w > 0.f) || !(area.h > 0.f)) return {0.f, 0.f, 1.f, 1.f};
  const float areaAspect = area.w / area.h, srcAspect = srcW / srcH;
  if (srcAspect > areaAspect) {   // imagem mais larga: corta os lados
    const float w = areaAspect / srcAspect;
    return {(1.f - w) * 0.5f, 0.f, w, 1.f};
  }
  const float h = srcAspect / areaAspect;   // imagem mais alta: corta em cima e embaixo
  return {0.f, (1.f - h) * 0.5f, 1.f, h};
}

}  // namespace editor::game_view_model
