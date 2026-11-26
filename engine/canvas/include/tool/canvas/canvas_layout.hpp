#pragma once

// Canvas: contêiner de UI com posicao por ancora e tamanho que pode acompanhar a tela. Este arquivo so tem dados
// simples (sem alocacao, sem ponteiros, sem funcoes virtuais) e as contas de layout. O componente que guarda um
// Canvas no ECS e o sistema que o atualiza ficam no editor e no runtime; a regra de calculo fica aqui, para ser a
// mesma nos dois.
//
// Dados e calculo:
//   - Layout: ancora (fracao da tela, 0 a 1), deslocamento e tamanho em pixels de referencia.
//   - Resize::Follow: o tamanho em pixels fica fixo; so a posicao acompanha a ancora ao mudar a resolucao.
//   - Resize::Scale: o canvas encolhe ou cresce com a tela (fator = tela / referencia, por eixo).
//   - Cache: guarda o retangulo calculado. Refresh so recalcula quando o layout, a tela ou a referencia mudam,
//     e devolve false quando o retangulo ja estava valido (o custo por quadro de um canvas parado e uma comparacao).
//
// Exemplo: um popup com ancora no centro (0.5, 0.5), deslocamento (10, 0) e 200x100 px de referencia. Na tela de
// 1000x800 (a referencia), o retangulo e x=510, y=400, 200x100. Trocando para 2000x1600 em modo Scale, ele vira
// x=1010, y=800, 400x200; em modo Follow, continua 200x100 em x=1010, y=800.

#include <cstdint>

namespace tool::canvas {

// Cor empacotada como RGBA de 8 bits (byte baixo = vermelho), igual ao formato usado pelo editor.
using Color = std::uint32_t;
inline constexpr Color Rgba(unsigned r, unsigned g, unsigned b, unsigned a = 255) noexcept {
  return (r & 0xffu) | ((g & 0xffu) << 8) | ((b & 0xffu) << 16) | ((a & 0xffu) << 24);
}
inline constexpr Color kTransparent = 0u;   // fundo transparente: so os filhos aparecem

enum class Resize : std::uint8_t {
  Follow,   // tamanho fixo em pixels; a ancora move a posicao
  Scale,    // encolhe e cresce com a tela
};

struct Screen {
  float w = 1920.f;
  float h = 1080.f;
};

struct Rect {
  float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
};

struct Layout {
  float anchorX = 0.5f, anchorY = 0.5f;   // ancora em fracao da tela (0 a 1)
  float offsetX = 0.f, offsetY = 0.f;     // deslocamento a partir da ancora, em pixels de referencia
  float width = 320.f, height = 240.f;    // tamanho em pixels de referencia
  Resize resize = Resize::Follow;
};

struct Style {
  float radius = 0.f;          // cantos arredondados (0 = reto)
  float borderWidth = 0.f;     // espessura da borda (0 = sem borda)
  Color background = kTransparent;
  Color border = Rgba(255, 255, 255);
};

// Fator de escala de um eixo: 1 fora do modo Scale ou com referencia invalida (evita divisao por zero).
[[nodiscard]] inline float ScaleFactor(float screenSize, float refSize, Resize mode) noexcept {
  if (mode != Resize::Scale || refSize <= 0.f) return 1.f;
  return screenSize / refSize;
}

// Retangulo do canvas na tela. ref = resolucao em que o layout foi definido.
[[nodiscard]] inline Rect Compute(const Layout& l, Screen screen, Screen ref) noexcept {
  const float sx = ScaleFactor(screen.w, ref.w, l.resize);
  const float sy = ScaleFactor(screen.h, ref.h, l.resize);
  Rect r;
  r.x = l.anchorX * screen.w + l.offsetX * sx;
  r.y = l.anchorY * screen.h + l.offsetY * sy;
  r.w = l.width * sx;
  r.h = l.height * sy;
  return r;
}

[[nodiscard]] inline bool SameLayout(const Layout& a, const Layout& b) noexcept {
  return a.anchorX == b.anchorX && a.anchorY == b.anchorY && a.offsetX == b.offsetX && a.offsetY == b.offsetY &&
         a.width == b.width && a.height == b.height && a.resize == b.resize;
}

[[nodiscard]] inline bool SameScreen(Screen a, Screen b) noexcept { return a.w == b.w && a.h == b.h; }

// Ultimo calculo guardado. valid = false ate o primeiro Refresh.
struct Cache {
  Rect rect{};
  Layout layout{};
  Screen screen{};
  Screen ref{};
  bool valid = false;
};

// Recalcula o retangulo so se a entrada mudou. Devolve true se recalculou (para contar o trabalho feito).
inline bool Refresh(Cache& c, const Layout& l, Screen screen, Screen ref) noexcept {
  if (c.valid && SameLayout(c.layout, l) && SameScreen(c.screen, screen) && SameScreen(c.ref, ref)) return false;
  c.rect = Compute(l, screen, ref);
  c.layout = l;
  c.screen = screen;
  c.ref = ref;
  c.valid = true;
  return true;
}

}  // namespace tool::canvas
