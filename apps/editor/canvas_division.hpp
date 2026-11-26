#pragma once

// Pontos de divisao do Canvas (etapa 3): marcas em forma de cruz que o usuario coloca sobre o canvas para dividi-lo.
// Sem janela: so posicao, nome e acerto de clique. Cada ponto e guardado em fracao do canvas (0..1 nos dois eixos),
// entao continua certo em qualquer tamanho e escala. O nome do ponto e o "viewpoint" da divisao.
//
// Exemplo: canvas de 400 x 300 px na tela. Um ponto em (0.5, 0.25) fica em (200, 75) px, dentro do canvas. Clicar a
// 4 px dele com raio de 8 px acerta o ponto; clicar a 20 px nao acerta.
//
// Como funciona:
//   add:      cria um ponto em fracao (x, y), limitado ao canvas, com nome padrao (P1, P2, ...).
//   remove:   apaga o ponto de indice i.
//   move:     muda a posicao em fracao, limitada ao canvas.
//   hitAt:    indice do ponto sob o clique (em pixels de tela), ou -1. O primeiro que casa ganha.
//   toPixels: posicao de um ponto na tela, dado o retangulo do canvas.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace editor::canvas_division {

inline constexpr float kHitRadiusPx = 8.f;   // raio de acerto do clique, em pixels de tela

struct Point {
  float x = 0.5f;   // fracao do canvas, 0 = esquerda/topo, 1 = direita/base
  float y = 0.5f;
  std::string name; // viewpoint; vazio = usa o padrao "P<n>"
};

struct Rect {
  float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
};

[[nodiscard]] inline float clamp01(float v) { return std::clamp(v, 0.f, 1.f); }

// Nome exibido: o nome dado pelo usuario, ou "P<posicao+1>" quando vazio.
[[nodiscard]] inline std::string displayName(const Point& p, std::size_t index) {
  return p.name.empty() ? "P" + std::to_string(index + 1) : p.name;
}

// Cria um ponto na fracao (x, y), limitada ao canvas. Devolve o indice do novo ponto.
inline std::size_t add(std::vector<Point>& points, float x, float y) {
  Point p;
  p.x = clamp01(x);
  p.y = clamp01(y);
  points.push_back(p);
  return points.size() - 1;
}

// Apaga o ponto de indice i. Indice invalido: nada muda.
inline void remove(std::vector<Point>& points, std::size_t i) {
  if (i < points.size()) points.erase(points.begin() + static_cast<std::ptrdiff_t>(i));
}

// Move o ponto i para a fracao (x, y), limitada ao canvas.
inline void move(std::vector<Point>& points, std::size_t i, float x, float y) {
  if (i >= points.size()) return;
  points[i].x = clamp01(x);
  points[i].y = clamp01(y);
}

// Posicao do ponto em pixels de tela, dado o retangulo do canvas (em coordenadas da mesma origem).
[[nodiscard]] inline void toPixels(const Point& p, Rect canvas, float& outX, float& outY) {
  outX = canvas.x + p.x * canvas.w;
  outY = canvas.y + p.y * canvas.h;
}

// Indice do ponto sob o clique (px, py) em pixels de tela, ou -1. Raio de acerto em kHitRadiusPx.
[[nodiscard]] inline int hitAt(const std::vector<Point>& points, Rect canvas, float px, float py) {
  for (std::size_t i = 0; i < points.size(); ++i) {
    float sx = 0.f, sy = 0.f;
    toPixels(points[i], canvas, sx, sy);
    if (std::fabs(px - sx) <= kHitRadiusPx && std::fabs(py - sy) <= kHitRadiusPx) return static_cast<int>(i);
  }
  return -1;
}

}  // namespace editor::canvas_division
