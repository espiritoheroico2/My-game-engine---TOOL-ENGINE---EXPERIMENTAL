// Bloom no renderizador CPU (o mesmo algoritmo do assets/shaders/bloom.hlsl, para a imagem sair igual nos dois caminhos):
//   1) limiar em linear: so o que passa do limiar entra, em meia resolucao (media 2x2)
//   2) blur gaussiano separavel de 9 taps (horizontal e vertical)
//   3) composicao: cor + halo * forca, com a cor de volta em sRGB de 8 bits
// A imagem de entrada e saida e RGBA8 em sRGB (como o SoftRenderer::pixels()). O alfa nao muda.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace tool {

// rgba aponta para w*h pixels RGBA8 (alterado no lugar).
inline void applyBloomRgba8(std::uint8_t* rgba, unsigned w, unsigned h, float threshold, float strength) {
  if (!rgba || w == 0 || h == 0) return;
  static float toLinear[256];
  static const bool init = [] { for (int i = 0; i < 256; ++i) { const float c = i / 255.f; toLinear[i] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); } return true; }();
  (void)init;
  const unsigned hw = std::max(1u, w / 2), hh = std::max(1u, h / 2);
  auto lum = [](float r, float g, float b) { return 0.2126f * r + 0.7152f * g + 0.0722f * b; };
  // 1) limiar em meia resolucao (media 2x2 da imagem de origem)
  // Buffers reaproveitados entre quadros (alocar ~1,5 MB por quadro custava tempo e tirava o frame time do lugar).
  static thread_local std::vector<float> a, b;
  const std::size_t need = static_cast<std::size_t>(hw) * hh * 3;
  a.assign(need, 0.f);
  for (unsigned y = 0; y < hh; ++y)
    for (unsigned x = 0; x < hw; ++x) {
      float sr = 0.f, sg = 0.f, sb = 0.f;
      for (unsigned dy = 0; dy < 2; ++dy)
        for (unsigned dx = 0; dx < 2; ++dx) {
          const unsigned sx = std::min(x * 2 + dx, w - 1), sy = std::min(y * 2 + dy, h - 1);
          const std::size_t p = (static_cast<std::size_t>(sy) * w + sx) * 4;
          const float r = toLinear[rgba[p]], g = toLinear[rgba[p + 1]], b = toLinear[rgba[p + 2]];
          const float l = lum(r, g, b);
          const float k = std::max(l - threshold, 0.f) / std::max(l, 1e-4f);
          sr += r * k; sg += g * k; sb += b * k;
        }
      const std::size_t q = (static_cast<std::size_t>(y) * hw + x) * 3;
      a[q] = sr * 0.25f; a[q + 1] = sg * 0.25f; a[q + 2] = sb * 0.25f;
    }
  // 2) gaussiana 9 taps: horizontal (a -> b) e vertical (b -> a); mesmos pesos do bloom.hlsl
  static const float W[5] = {0.227027f, 0.194595f, 0.121622f, 0.054054f, 0.016216f};
  b.assign(need, 0.f);
  auto blur = [&](const std::vector<float>& src, std::vector<float>& dst, int dx, int dy) {
    for (int y = 0; y < static_cast<int>(hh); ++y)
      for (int x = 0; x < static_cast<int>(hw); ++x) {
        float acc[3] = {0.f, 0.f, 0.f};
        for (int k = -4; k <= 4; ++k) {
          const float wgt = W[std::abs(k)];
          const int sx = std::clamp(x + dx * k, 0, static_cast<int>(hw) - 1), sy = std::clamp(y + dy * k, 0, static_cast<int>(hh) - 1);
          const std::size_t s = (static_cast<std::size_t>(sy) * hw + sx) * 3;
          acc[0] += src[s] * wgt; acc[1] += src[s + 1] * wgt; acc[2] += src[s + 2] * wgt;
        }
        const std::size_t d = (static_cast<std::size_t>(y) * hw + x) * 3;
        dst[d] = acc[0]; dst[d + 1] = acc[1]; dst[d + 2] = acc[2];
      }
  };
  blur(a, b, 1, 0);
  blur(b, a, 0, 1);
  // 3) composicao em resolucao cheia: cor linear + halo (vizinho mais proximo) * forca, de volta para sRGB
  auto toSrgb = [](float v) {
    v = std::clamp(v, 0.f, 1.f);
    return v <= 0.0031308f ? 12.92f * v : 1.055f * std::pow(v, 1.f / 2.4f) - 0.055f;
  };
  for (unsigned y = 0; y < h; ++y)
    for (unsigned x = 0; x < w; ++x) {
      const unsigned hx = std::min(x / 2, hw - 1), hy = std::min(y / 2, hh - 1);
      const std::size_t q = (static_cast<std::size_t>(hy) * hw + hx) * 3;
      const std::size_t p = (static_cast<std::size_t>(y) * w + x) * 4;
      for (int c = 0; c < 3; ++c) {
        const float v = toLinear[rgba[p + c]] + a[q + c] * strength;
        rgba[p + c] = static_cast<std::uint8_t>(std::lround(toSrgb(v) * 255.f));
      }
    }
}

} // namespace tool
