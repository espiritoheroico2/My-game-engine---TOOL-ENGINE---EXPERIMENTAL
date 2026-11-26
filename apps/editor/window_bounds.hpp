#pragma once

// Limites de uma janela solta (flutuante) dentro da area do editor, sem janela. Regra: o cabecalho inteiro fica
// visivel na vertical e uma faixa de `grab` pixels da janela fica visivel na horizontal, para ela sempre poder ser
// pega pelo grip ou pela borda. Assim uma janela solta nunca some pela borda da tela.
//
// Exemplo: area de 1240 x 840 com origem (0, 0). Cabecalho de 28, janela solta em (1500, -50), grab 80: vira
// (1160, 0). A janela pode sair pela direita so ate deixar `grab` pixels visiveis, e o cabecalho nao sobe da area.

#include <algorithm>

namespace editor::window_bounds {

struct Point {
  float x = 0.f, y = 0.f;
};
struct Box {
  float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
};

// Canto superior esquerdo `at` da janela (largura w) corrigido para a area. header: altura da barra do cabecalho.
// grab: pixels da janela que ficam visiveis na horizontal quando ela esta quase toda fora.
[[nodiscard]] inline Point keepInside(Point at, float w, Box area, float header, float grab) {
  const float g = std::min(grab, w);                       // nunca exige mais largura do que a janela tem
  const float minX = area.x - w + g;
  const float maxX = area.x + area.w - g;
  const float minY = area.y;                               // o cabecalho nunca sobe da area
  const float maxY = area.y + std::max(0.f, area.h - header);
  Point p;
  p.x = std::clamp(at.x, std::min(minX, maxX), std::max(minX, maxX));
  p.y = std::clamp(at.y, std::min(minY, maxY), std::max(minY, maxY));
  return p;
}

}  // namespace editor::window_bounds
