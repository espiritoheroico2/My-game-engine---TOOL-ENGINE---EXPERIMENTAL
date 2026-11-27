#pragma once

// Barra de rolagem vertical unica dos paineis do editor. Geometria e estado puros (sem SDL, sem SpriteUI):
// roda no ctest como Editor.UiScrollbar.
//
// Uso no painel:
//   - a cada quadro: bar_.layout(trilho, alturaConteudo, alturaVisivel) (nas mesmas coordenadas do mouse);
//   - Down esquerdo: if (bar_.pointerDown(x, y, scroll)) { consumiu; }   (clique no polegar arrasta; no trilho pagina)
//   - Motion: bar_.pointerMove(x, y, scroll) (so rola enquanto arrasta, mesmo com o mouse fora do trilho)
//   - Up (ou soltura perdida): bar_.pointerUp()
//   - desenho: for (q : bar_.quads(palette::active(), scroll)) se q.rect.w > 0, desenha o retangulo com q.color
// A roda do mouse continua nas maos do painel (scroll -= wheel.y * passo).
// Hover e arraste mudam so a cor: o painel pede novo quadro quando visualChanged() ou o arraste rolou.
#include "sui_palette.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace editor::ui_scrollbar {

inline constexpr float kWidth = 8.f;           // largura do trilho (e da zona de clique)
inline constexpr float kMinThumb = 24.f;       // menor comprimento do polegar
inline constexpr float kPageFraction = 0.9f;   // clique no trilho rola 90% da altura visivel

// Retangulo em pixels (x, y = canto superior esquerdo), proprio deste modulo para nao depender do SpriteUI.
struct Rect {
  float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
  [[nodiscard]] bool contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

// Rolagem maxima (0 se o conteudo cabe).
[[nodiscard]] inline float maxScroll(float contentH, float viewH) { return std::max(0.f, contentH - viewH); }

// A barra aparece so quando o conteudo nao cabe.
[[nodiscard]] inline bool needed(float contentH, float viewH) { return viewH > 0.f && contentH > viewH + 0.5f; }

// Prende a rolagem em [0, maxScroll]; valor nao finito (NaN/inf) vira 0.
[[nodiscard]] inline float clampScroll(float scroll, float contentH, float viewH) {
  if (!std::isfinite(scroll)) return 0.f;
  return std::clamp(scroll, 0.f, maxScroll(contentH, viewH));
}

// Comprimento do polegar: proporcional a visivel/total, entre kMinThumb e o trilho.
[[nodiscard]] inline float thumbLength(float trackH, float contentH, float viewH) {
  if (trackH <= 0.f) return 0.f;
  if (!needed(contentH, viewH)) return trackH;
  return std::min(trackH, std::max(kMinThumb, trackH * viewH / contentH));
}

// Posicao Y do polegar para uma rolagem.
[[nodiscard]] inline float thumbTop(const Rect& track, float contentH, float viewH, float scroll) {
  const float len = thumbLength(track.h, contentH, viewH);
  const float room = std::max(0.f, track.h - len);
  const float mx = maxScroll(contentH, viewH);
  return track.y + (mx > 0.f ? clampScroll(scroll, contentH, viewH) / mx * room : 0.f);
}

// Retangulo completo do polegar (mesma largura do trilho) para a rolagem atual.
[[nodiscard]] inline Rect thumbRect(const Rect& track, float contentH, float viewH, float scroll) {
  return Rect{track.x, thumbTop(track, contentH, viewH, scroll), track.w, thumbLength(track.h, contentH, viewH)};
}

// Rolagem para o polegar com o topo em `top` (preso ao trilho).
[[nodiscard]] inline float scrollForThumbTop(const Rect& track, float contentH, float viewH, float top) {
  const float len = thumbLength(track.h, contentH, viewH);
  const float room = track.h - len;
  if (room <= 0.f) return 0.f;
  const float t = std::clamp((top - track.y) / room, 0.f, 1.f);
  return t * maxScroll(contentH, viewH);
}

// Arraste: o mouse esta em pointerY e agarrou o polegar a `grab` pixels do topo dele.
[[nodiscard]] inline float dragScroll(const Rect& track, float contentH, float viewH, float pointerY, float grab) {
  return scrollForThumbTop(track, contentH, viewH, pointerY - grab);
}

// Clique no trilho fora do polegar: uma pagina para cima (acima do polegar) ou para baixo (abaixo).
[[nodiscard]] inline float pageScroll(const Rect& track, float contentH, float viewH, float scroll, float pointerY) {
  const Rect thumb = thumbRect(track, contentH, viewH, scroll);
  const float page = viewH * kPageFraction;
  if (pointerY < thumb.y) return clampScroll(scroll - page, contentH, viewH);
  if (pointerY >= thumb.y + thumb.h) return clampScroll(scroll + page, contentH, viewH);
  return clampScroll(scroll, contentH, viewH);
}

// Retangulo de cor para o painel desenhar (w = 0: nada a desenhar).
struct Quad {
  Rect rect;
  palette::Rgb color;
};

// Estado da barra de rolagem vertical (posicao, arraste, hover). Logica pura: o painel so desenha o resultado e repassa o mouse.
class Scrollbar {
 public:
  // Posiciona a barra (a cada quadro). Se o conteudo cabe, ela some e solta qualquer arraste.
  void layout(const Rect& track, float contentH, float viewH) {
    track_ = track;
    contentH_ = contentH;
    viewH_ = viewH;
    if (!visible()) {
      dragging_ = false;
      hover_ = false;
    }
  }

  [[nodiscard]] bool visible() const { return track_.h > 0.f && needed(contentH_, viewH_); }
  [[nodiscard]] bool dragging() const { return dragging_; }
  [[nodiscard]] bool hovered() const { return hover_; }
  [[nodiscard]] const Rect& track() const { return track_; }

  // True se (x, y) esta sobre a barra (visivel).
  [[nodiscard]] bool hit(float x, float y) const { return visible() && track_.contains(x, y); }

  // Down esquerdo. True se a barra consumiu o clique. `scroll` muda quando o clique cai no trilho (pagina).
  bool pointerDown(float x, float y, float& scroll) {
    if (!hit(x, y)) return false;
    const Rect thumb = thumbRect(track_, contentH_, viewH_, scroll);
    if (thumb.contains(x, y)) {
      grab_ = y - thumb.y;
    } else {
      scroll = pageScroll(track_, contentH_, viewH_, scroll, y);
      grab_ = thumbLength(track_.h, contentH_, viewH_) * 0.5f;   // segue arrastando a partir do meio, como no sistema
    }
    dragging_ = true;
    hover_ = true;
    return true;
  }

  // Movimento do mouse. Devolve true se a rolagem mudou (arraste). Atualiza o hover.
  bool pointerMove(float x, float y, float& scroll) {
    hover_ = hit(x, y);
    if (!dragging_) return false;
    const float next = dragScroll(track_, contentH_, viewH_, y, grab_);
    const bool moved = next != scroll;
    scroll = next;
    return moved;
  }

  // Up esquerdo (ou soltura perdida). True se havia arraste.
  bool pointerUp() {
    const bool was = dragging_;
    dragging_ = false;
    return was;
  }

  // O visual (hover/arraste) mudou desde a ultima chamada? Para o painel pedir novo quadro.
  [[nodiscard]] bool visualChanged() {
    const int now = (dragging_ ? 2 : 0) | (hover_ ? 1 : 0);
    const bool changed = now != lastVisual_;
    lastVisual_ = now;
    return changed;
  }

  // Cores so da paleta: trilho = surface, polegar = scroll; hover e arraste clareiam o polegar na direcao do texto.
  [[nodiscard]] std::array<Quad, 2> quads(const palette::Palette& pal, float scroll) const {
    std::array<Quad, 2> out{};
    if (!visible()) return out;
    palette::Rgb track = pal.surface;
    track.a = 0.6f;
    palette::Rgb thumb = pal.scroll;
    thumb.a = 0.55f;
    if (dragging_) {
      thumb = palette::mixRgb(pal.scroll, pal.text, 0.35f);
      thumb.a = 1.f;
    } else if (hover_) {
      thumb = palette::mixRgb(pal.scroll, pal.text, 0.15f);
      thumb.a = 0.85f;
    }
    out[0] = Quad{track_, track};
    const Rect t = thumbRect(track_, contentH_, viewH_, scroll);
    // O polegar fica 1 px mais fino de cada lado para respirar nas bordas do trilho.
    out[1] = Quad{Rect{t.x + 1.f, t.y + 1.f, std::max(0.f, t.w - 2.f), std::max(0.f, t.h - 2.f)}, thumb};
    return out;
  }

 private:
  Rect track_;
  float contentH_ = 0.f, viewH_ = 0.f;
  float grab_ = 0.f;
  bool dragging_ = false, hover_ = false;
  int lastVisual_ = 0;
};

}  // namespace editor::ui_scrollbar
