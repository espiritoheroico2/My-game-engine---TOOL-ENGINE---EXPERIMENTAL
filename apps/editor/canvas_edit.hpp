#pragma once

// Edicao do Canvas na janela Scene: a geometria, sem janela. Onde fica a caixa do canvas na area da cena, qual alca o
// clique pegou (corpo ou canto) e como um arrasto em pixels de tela vira mudanca no layout. A janela
// (scene_view_panel.hpp) mostra e capta o mouse; quem decide a nova posicao e este arquivo. Sem SpriteUi: roda no
// ctest como Editor.CanvasEdit.
//
// Exemplo: um canvas de 400 x 300 no canto inferior direito. Arrastar o canto em 50 px de tela, numa cena onde a escala
// e 1, deixa o canvas com 450 x 300. Arrastar o corpo move o deslocamento e a ancora fica onde esta.
//
// Como funciona:
//   boxFor:     retangulo do canvas na area da cena, em pixels de tela (o mesmo calculo do layout do jogo).
//   gripAt:     corpo, canto (a alca de redimensionar) ou nada, para um ponto da tela.
//   pixelsPerUnit: quantos pixels de tela valem uma unidade do layout (a escala do modo Scale; 1 no Follow).
//   drag:       aplica um arrasto em pixels ao layout. Corpo muda o deslocamento; canto muda largura e altura (minimo 1).

#include "tool/canvas/canvas_layout.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace editor::canvas_edit {

inline constexpr float kCornerPx = 10.f;   // raio da alca do canto (px de tela)

struct Box {
  float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
  [[nodiscard]] bool contains(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

// Alca escolhida por um clique: nada, o corpo do canvas ou o canto inferior direito.
enum class Grip : std::uint8_t { None, Body, Corner };

// Retangulo do canvas dentro de uma area de (areaW, areaH) pixels de tela, em coordenadas locais da area.
// ref e a referencia de projeto dos layouts (o modo Scale escala a partir dela).
inline Box boxFor(const tool::canvas::Layout& l, float areaW, float areaH, tool::canvas::Screen ref) {
  const tool::canvas::Rect r = tool::canvas::Compute(l, tool::canvas::Screen{areaW, areaH}, ref);
  return {r.x, r.y, r.w, r.h};
}

// Alca sob o ponto (px, py), em coordenadas da mesma origem da caixa. O canto tem prioridade sobre o corpo.
inline Grip gripAt(const Box& b, float px, float py) {
  if (std::fabs(px - (b.x + b.w)) <= kCornerPx && std::fabs(py - (b.y + b.h)) <= kCornerPx) return Grip::Corner;
  if (b.contains(px, py)) return Grip::Body;
  return Grip::None;
}

// Pixels de tela por unidade do layout, em cada eixo. O modo Scale escala X e Y separadamente (como em
// canvas_layout::Compute); no Follow os dois valem 1. Sem largura ou altura uteis, 1 (o arrasto fica em pixels).
struct Ppu {
  float x = 1.f, y = 1.f;
};
inline Ppu pixelsPerUnit(const Box& b, const tool::canvas::Layout& l) {
  Ppu p;
  if (l.width > 0.f && b.w > 0.f) p.x = b.w / l.width;
  if (l.height > 0.f && b.h > 0.f) p.y = b.h / l.height;
  return p;
}

// Aplica um arrasto em pixels de tela. ppu vem de pixelsPerUnit no momento do clique (a escala nao muda durante o arrasto).
inline void drag(tool::canvas::Layout& l, Grip g, float dx, float dy, Ppu ppu) {
  if (ppu.x <= 0.f || ppu.y <= 0.f) return;
  if (g == Grip::Body) {
    l.offsetX += dx / ppu.x;
    l.offsetY += dy / ppu.y;
  } else if (g == Grip::Corner) {
    l.width = std::max(1.f, l.width + dx / ppu.x);
    l.height = std::max(1.f, l.height + dy / ppu.y);
  }
}

}  // namespace editor::canvas_edit
