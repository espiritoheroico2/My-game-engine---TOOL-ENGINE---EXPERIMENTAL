#pragma once

// Formas da Scene e do Jogo antes de desenhar. Quem monta a vista (grade, gizmo, particulas, objetos cortados,
// formas de fisica, UI do jogo) so grava formas numa ShapeList, em pixels da janela. O painel (view_painter.hpp) le a lista e desenha tudo em um lote por textura. A ordem de gravacao e a
// ordem de pintura: o que e gravado depois fica por cima.
//
// Exemplo: o eixo vermelho do objeto Caixa azul selecionado e uma linha de (300, 200) a (340, 200), grossa de 2 px.
// Uma Caixa azul com a UI "Vida" por cima e gravada depois, entao o texto "Vida" aparece sobre o retangulo.
// Um painel (fundo de janela flutuante ou de menu) e gravado com panel(): o que vem depois dele fica por cima.
//
// Cores: RGBA empacotado no mesmo formato de IM_COL32 (byte baixo = vermelho), para as contas de cor antigas
// continuarem valendo sem mudanca.

#include "scene_view_model.hpp"   // Pixel

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace editor::view {

using Pixel = scene_view_model::Pixel;
using Area = scene_view_model::Area;

using Color = std::uint32_t;

inline constexpr Color Rgba(int r, int g, int b, int a = 255) {
  return static_cast<Color>(static_cast<unsigned>(r) | (static_cast<unsigned>(g) << 8) | (static_cast<unsigned>(b) << 16) |
                            (static_cast<unsigned>(a) << 24));
}

// Cor a partir de canais 0..1 (valores fora da faixa sao presos), 
inline Color rgbaF(float r, float g, float b, float a) {
  auto q = [](float v) { return static_cast<int>(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f); };
  return Rgba(q(r), q(g), q(b), q(a));
}

enum class ShapeKind : std::uint8_t { Line, RoundRect, CircleFilled, Ring, Triangle, Text, Panel };

// Uma forma. Campos sem uso no tipo ficam zerados: a/b = pontos, c = terceiro ponto (triangulo), r = raio, espessura
// ou largura maxima do texto, radius = cantos (retangulo e painel), size = fonte (texto), centered = texto centrado
// em a, edge = cor da borda (painel). text fica por ultimo: as formas que nao sao texto o deixam vazio.
struct Shape {
  ShapeKind kind = ShapeKind::Line;
  Pixel a{}, b{}, c{};
  float r = 0.f;
  float thickness = 1.f;
  float radius = 0.f;
  float size = 14.f;
  bool centered = false;
  Color color = 0;
  std::string text;
  Color edge = 0;
  bool middle = false;   // texto: a/y e o centro vertical da linha (a altura medida da fonte), nao o topo
};

class ShapeList {
public:
  void line(Pixel a, Pixel b, float thickness, Color col) { add({ShapeKind::Line, a, b, {}, 0.f, thickness, 0.f, 0.f, false, col, {}}); }
  // Retangulo cheio (cantos arredondados se radius > 0).
  void rectFilled(Pixel a, Pixel b, Color col, float radius = 0.f) {
    add({ShapeKind::RoundRect, a, b, {}, 0.f, 0.f, radius, 0.f, false, col, {}});
  }
  // Fundo de painel com borda (Elements::Panel da SpriteUI). Os textos gravados antes dele ficam embaixo dele.
  // thickness: espessura da borda em pixels (1 por padrao, como o painel da SpriteUi).
  void panel(Pixel a, Pixel b, float radius, Color fill, Color edge, float thickness = 1.f) {
    add({ShapeKind::Panel, a, b, {}, 0.f, thickness, radius, 0.f, false, fill, {}, edge});
  }
  void circleFilled(Pixel center, float r, Color col) { add({ShapeKind::CircleFilled, center, {}, {}, r, 0.f, 0.f, 0.f, false, col, {}}); }
  void circle(Pixel center, float r, Color col, float thickness) { add({ShapeKind::Ring, center, {}, {}, r, thickness, 0.f, 0.f, false, col, {}}); }
  void triangleFilled(Pixel a, Pixel b, Pixel c, Color col) { add({ShapeKind::Triangle, a, b, c, 0.f, 0.f, 0.f, 0.f, false, col, {}}); }
  // Texto com a fonte `size`. maxWidth > 0 corta o texto com "..." para caber (pixels).
  void text(Pixel at, std::string s, float size, Color col, bool centered = false, float maxWidth = 0.f) {
    add({ShapeKind::Text, at, {}, {}, maxWidth, 0.f, 0.f, size, centered, col, std::move(s)});
  }
  // Texto com o centro vertical em at.y: a linha fica centrada na altura medida, qualquer que seja a fonte.
  void textMiddle(Pixel at, std::string s, float size, Color col, bool centered = false, float maxWidth = 0.f) {
    add({ShapeKind::Text, at, {}, {}, maxWidth, 0.f, 0.f, size, centered, col, std::move(s), 0, true});
  }

  // Acrescenta as formas de outra lista, no fim (ordem de pintura preservada).
  void append(const ShapeList& other) { shapes_.insert(shapes_.end(), other.shapes_.begin(), other.shapes_.end()); }
  [[nodiscard]] const std::vector<Shape>& shapes() const { return shapes_; }

private:
  void add(Shape s) { shapes_.push_back(std::move(s)); }
  std::vector<Shape> shapes_;
};

}  // namespace editor::view
