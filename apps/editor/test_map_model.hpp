#pragma once

// Modelo da janela "Test map of chunks" (SpriteUI): limites do lado do mapa, contagens e aviso de mapa grande.
// Nao usa SDL nem desenho; roda no ctest (tests/test_test_map_model.cpp).

#include <algorithm>
#include <cmath>

namespace editor::testmap {

inline constexpr int kMinSide = 1;
inline constexpr int kMaxSide = 256;
inline constexpr long long kLargeChunks = 4096;   // acima disso a janela avisa que a geracao e pesada
inline constexpr int kBlocksPerChunk = 16;        // um chunk de terreno tem 16 x 16 x 16 blocos

struct Summary {
  long long chunks = 0;
  long long blocks = 0;   // lado do mapa em blocos
  bool large = false;
};

inline int ClampSide(int side) { return std::clamp(side, kMinSide, kMaxSide); }

inline Summary Summarize(int side) {
  const long long s = ClampSide(side);
  Summary out;
  out.chunks = s * s;
  out.blocks = s * kBlocksPerChunk;
  out.large = out.chunks > kLargeChunks;
  return out;
}

// Posicao do cursor no trilho (0 = inicio, 1 = fim) para o lado do mapa, e o contrario.
inline float FractionOfSide(int side) {
  return static_cast<float>(ClampSide(side) - kMinSide) / static_cast<float>(kMaxSide - kMinSide);
}

inline int SideOfFraction(float fraction) {
  const float f = std::clamp(fraction, 0.f, 1.f);
  return ClampSide(kMinSide + static_cast<int>(std::lround(f * static_cast<float>(kMaxSide - kMinSide))));
}

}  // namespace editor::testmap
