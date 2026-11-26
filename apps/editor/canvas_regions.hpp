#pragma once

// Regioes do Canvas (etapa 3): as linhas de divisao cortam o canvas em regioes, e cada regiao tem um numero. Sem janela.
//
// Regras (definidas pelo usuario):
//   - a numeracao segue a ordem de criacao: a regiao nova ganha o proximo numero, nunca reaproveitado;
//   - quando regioes se fundem, a fusao assume o menor numero (fusao da 4 com a 7 = 4); o 7 deixa de existir;
//   - quando uma regiao se divide, a parte que era maior mantem o numero e a outra ganha um numero novo.
//
// Como funciona: o canvas vira uma grade de kGrid x kGrid celulas. Cada linha marca as celulas que cruza como parede.
// As celulas livres formam regioes (componentes conexas). A cada mudanca, as regioes novas recebem numero a partir das
// celulas antigas que elas ja tinham (refresh). A precisao e de uma celula (1/kGrid do canvas), suficiente para
// numerar e escolher a regiao de um elemento.
//
// Exemplo: canvas com uma linha vertical no meio: regiao 1 a esquerda, regiao 2 a direita. Remover a linha funde as
// duas e a regiao vira 1.

#include "canvas_division.hpp"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <map>
#include <vector>

namespace editor::canvas_regions {

inline constexpr int kGrid = 32;   // celulas por lado do canvas

struct Regions {
  std::vector<std::uint16_t> cell = std::vector<std::uint16_t>(kGrid * kGrid, 0);   // numero da regiao, 0 = parede/sem regiao
  int next = 1;                                                                     // proximo numero a criar
};

// Celulas cruzadas por um segmento (fracoes do canvas), marcadas como parede.
inline void markWalls(std::vector<bool>& wall, float ax, float ay, float bx, float by) {
  const float dx = bx - ax, dy = by - ay;
  const int steps = std::max(1, static_cast<int>(std::ceil(std::sqrt(dx * dx + dy * dy) * kGrid * 4.f)));
  for (int i = 0; i <= steps; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(steps);
    const int cx = std::clamp(static_cast<int>(std::floor((ax + dx * t) * kGrid)), 0, kGrid - 1);
    const int cy = std::clamp(static_cast<int>(std::floor((ay + dy * t) * kGrid)), 0, kGrid - 1);
    wall[static_cast<std::size_t>(cy * kGrid + cx)] = true;
  }
}

// Recalcula as regioes depois de uma mudanca de pontos ou linhas. Mantem os numeros que ainda fazem sentido.
inline void refresh(Regions& r, const std::vector<canvas_division::Point>& points, const std::vector<canvas_division::Line>& lines) {
  std::vector<bool> wall(kGrid * kGrid, false);
  for (const canvas_division::Line& l : lines) {
    if (l.a >= points.size() || l.b >= points.size()) continue;
    markWalls(wall, points[l.a].x, points[l.a].y, points[l.b].x, points[l.b].y);
  }

  // Componentes conexas (4-vizinhanca) das celulas livres.
  std::vector<int> comp(kGrid * kGrid, -1);
  std::vector<std::vector<std::size_t>> comps;
  for (std::size_t s = 0; s < comp.size(); ++s) {
    if (wall[s] || comp[s] >= 0) continue;
    const int id = static_cast<int>(comps.size());
    comps.emplace_back();
    std::vector<std::size_t> stack{s};
    comp[s] = id;
    while (!stack.empty()) {
      const std::size_t c = stack.back();
      stack.pop_back();
      comps[static_cast<std::size_t>(id)].push_back(c);
      const int x = static_cast<int>(c) % kGrid, y = static_cast<int>(c) / kGrid;
      const int nx[4] = {x - 1, x + 1, x, x};
      const int ny[4] = {y, y, y - 1, y + 1};
      for (int k = 0; k < 4; ++k) {
        if (nx[k] < 0 || nx[k] >= kGrid || ny[k] < 0 || ny[k] >= kGrid) continue;
        const std::size_t n = static_cast<std::size_t>(ny[k] * kGrid + nx[k]);
        if (wall[n] || comp[n] >= 0) continue;
        comp[n] = id;
        stack.push_back(n);
      }
    }
  }

  // Ordem de decisao: a componente maior escolhe primeiro (a parte maior de uma divisao mantem o numero).
  std::vector<std::size_t> order(comps.size());
  for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
  std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return comps[a].size() > comps[b].size(); });

  std::vector<std::uint16_t> assigned(comps.size(), 0);
  std::map<std::uint16_t, bool> claimed;
  for (std::size_t idx : order) {
    // Numeros antigos presentes nesta componente.
    std::vector<std::uint16_t> old;
    for (std::size_t c : comps[idx]) {
      if (r.cell[c] != 0) old.push_back(r.cell[c]);
    }
    std::sort(old.begin(), old.end());
    old.erase(std::unique(old.begin(), old.end()), old.end());
    std::uint16_t pick = 0;
    for (std::uint16_t n : old) {
      if (!claimed[n]) { pick = n; break; }   // o menor numero ainda livre
    }
    if (pick == 0) pick = static_cast<std::uint16_t>(r.next++);
    claimed[pick] = true;
    assigned[idx] = pick;
  }

  // Grava as celulas com o numero da sua regiao (paredes e celulas fora de qualquer regiao viram 0).
  for (std::size_t c = 0; c < r.cell.size(); ++c) {
    r.cell[c] = comp[c] >= 0 ? assigned[static_cast<std::size_t>(comp[c])] : 0;
  }
}

// Numero da regiao sob a fracao (x, y) do canvas, ou 0 (parede).
[[nodiscard]] inline int regionAt(const Regions& r, float x, float y) {
  const int cx = std::clamp(static_cast<int>(std::floor(x * kGrid)), 0, kGrid - 1);
  const int cy = std::clamp(static_cast<int>(std::floor(y * kGrid)), 0, kGrid - 1);
  return r.cell[static_cast<std::size_t>(cy * kGrid + cx)];
}

// Regiao de um elemento: a do centro do retangulo (x, y, w, h em fracao do canvas). 0 se o centro cai numa parede.
[[nodiscard]] inline int regionOfBox(const Regions& r, float x, float y, float w, float h) {
  return regionAt(r, x + w * 0.5f, y + h * 0.5f);
}

// Centro (fracao do canvas) de uma regiao, para desenhar o numero. Falso quando a regiao nao existe.
[[nodiscard]] inline bool centerOf(const Regions& r, int id, float& outX, float& outY) {
  double sx = 0.0, sy = 0.0;
  std::size_t n = 0;
  for (std::size_t c = 0; c < r.cell.size(); ++c) {
    if (r.cell[c] != id) continue;
    sx += (static_cast<double>(c % kGrid) + 0.5) / kGrid;
    sy += (static_cast<double>(c / kGrid) + 0.5) / kGrid;
    ++n;
  }
  if (n == 0) return false;
  outX = static_cast<float>(sx / static_cast<double>(n));
  outY = static_cast<float>(sy / static_cast<double>(n));
  return true;
}

}  // namespace editor::canvas_regions
