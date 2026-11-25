#pragma once

// Regras puras do redimensionamento pelo mouse: qual cursor mostrar sobre uma borda e onde desenhar o destaque
// da borda (faixa mais grossa e brilhante). Sem SDL: o host (dock_host.hpp) aplica o cursor e desenha o destaque.
// Mascara de borda (window_frame::Frame::edgeAt): 1 esquerda, 2 direita, 4 topo, 8 base.

#include "SpriteUi/Core/Math.hpp"    // SU::Rect, SU::Vec2

#include <array>
#include <cstddef>

namespace editor::resize_hover {

namespace SU = ToolEngine::SpriteUi;

// Forma do cursor. Os valores sao usados como indice do cache do host.
enum class CursorShape : unsigned char { Arrow, ResizeH, ResizeV, ResizeNWSE, ResizeNESW };

// Cursor para uma mascara de borda. Cantos (duas bordas) viram diagonal; mascara 0 e a seta normal.
[[nodiscard]] inline CursorShape EdgeCursor(int mask) {
  const bool left = (mask & 1) != 0, right = (mask & 2) != 0, top = (mask & 4) != 0, bottom = (mask & 8) != 0;
  if ((left || right) && (top || bottom)) {
    // Canto superior esquerdo e inferior direito: diagonal \ ; os outros dois: diagonal /.
    return (left == top) ? CursorShape::ResizeNWSE : CursorShape::ResizeNESW;
  }
  if (left || right) return CursorShape::ResizeH;
  if (top || bottom) return CursorShape::ResizeV;
  return CursorShape::Arrow;
}

// Cursor de uma divisoria do dock: corte vertical (folhas lado a lado) pede seta horizontal, e vice-versa.
[[nodiscard]] inline CursorShape DividerCursor(bool sideBySide) {
  return sideBySide ? CursorShape::ResizeH : CursorShape::ResizeV;
}

// Retangulos do destaque: uma faixa de `thickness` px por dentro de cada borda marcada na mascara.
// Ate quatro retangulos; o restante e preenchido com tamanho zero.
struct Highlight {
  std::array<SU::Rect, 4> rects{};
  std::size_t count = 0;
};

[[nodiscard]] inline Highlight EdgeHighlight(SU::Rect r, int mask, float thickness) {
  Highlight h;
  auto add = [&](SU::Rect x) { h.rects[h.count++] = x; };
  const float t = thickness;
  if (mask & 1) add({r.position, {t, r.size.y}});
  if (mask & 2) add({{r.position.x + r.size.x - t, r.position.y}, {t, r.size.y}});
  if (mask & 4) add({r.position, {r.size.x, t}});
  if (mask & 8) add({{r.position.x, r.position.y + r.size.y - t}, {r.size.x, t}});
  return h;
}

// Faixa do destaque de uma divisoria: `at` e a posicao do corte no eixo perpendicular; a faixa cobre a folha toda.
[[nodiscard]] inline SU::Rect DividerHighlight(SU::Rect bounds, bool sideBySide, float at, float thickness) {
  if (sideBySide) return {{at - thickness * 0.5f, bounds.position.y}, {thickness, bounds.size.y}};
  return {{bounds.position.x, at - thickness * 0.5f}, {bounds.size.x, thickness}};
}

}  // namespace editor::resize_hover
