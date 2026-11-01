// ToolEngine - upscaling e antialiasing por SOFTWARE (CPU), so com matematica. Sem GPU, sem dependencias.
//
// upscaleRgba(): reamostra uma imagem RGBA8 de (sw, sh) para (dw, dh). Modos:
//   Bilinear  - 2x2 taps, o mais barato; borra bordas.
//   Bicubic   - Catmull-Rom (a = -0.5), 4x4 taps; bordas mais nitidas sem anel forte.
//   Lanczos2  - janela sinc de 2 lobos, 4x4 taps; o mais nitido dos tres, com leve anel em bordas fortes.
// Os pesos sao calculados UMA vez por coluna e por linha de destino (separaveis) e normalizados (soma 1 -> brilho preservado).
//
// fxaaRgba(): antialiasing estilo FXAA (versao simples, de Timothy Lottes, dominio publico na descricao do algoritmo).
//   Detecta bordas pelo contraste de luma em 3x3, escolhe a direcao do degrau pelo gradiente e mistura ao longo dela.
//   Sem edge detection por geometria: funciona em qualquer imagem final (inclusive a que veio do upscaling).
//
// Ambos rodam em faixas de linhas no pool de threads persistente (tool::parallelFor).
#pragma once
#include "tool/core/parallel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

namespace tool {

enum class UpscaleFilter : int { Bilinear = 0, Bicubic = 1, Lanczos2 = 2 };

namespace upscale_detail {

inline int clampi(int v, int n) { return v < 0 ? 0 : (v >= n ? n - 1 : v); }

inline float catmullRom(float x) {          // a = -0.5
  x = std::fabs(x);
  if (x < 1.f) return ((1.5f * x - 2.5f) * x) * x + 1.f;
  if (x < 2.f) return (((-0.5f * x + 2.5f) * x - 4.f) * x + 2.f);
  return 0.f;
}
inline float sinc(float x) { if (std::fabs(x) < 1e-6f) return 1.f; const float p = 3.14159265f * x; return std::sin(p) / p; }
inline float lanczos2(float x) { x = std::fabs(x); return x < 2.f ? sinc(x) * sinc(x * 0.5f) : 0.f; }

// Pesos de uma dimensao: para cada destino, 4 indices de origem JA com clamp (borda) e 4 pesos normalizados.
struct Axis { std::vector<std::array<int, 4>> idx; std::vector<std::array<float, 4>> w; };
inline Axis makeAxis(int srcN, int dstN, UpscaleFilter f) {
  Axis ax; ax.idx.resize(static_cast<size_t>(dstN)); ax.w.resize(static_cast<size_t>(dstN));
  const float scale = static_cast<float>(srcN) / static_cast<float>(dstN);
  for (int d = 0; d < dstN; ++d) {
    const float s = (static_cast<float>(d) + 0.5f) * scale - 0.5f;   // centro do pixel
    const int base = static_cast<int>(std::floor(s));
    const float t = s - static_cast<float>(base);                    // 0..1 entre base e base+1
    std::array<float, 4> w{0, 0, 0, 0};
    // k = 0..3 -> base-1..base+2. Bilinear usa so k=1 (base) e k=2 (base+1); os outros tem peso zero.
    if (f == UpscaleFilter::Bilinear) { w[1] = 1.f - t; w[2] = t; }
    else if (f == UpscaleFilter::Bicubic) { for (int k = 0; k < 4; ++k) w[static_cast<size_t>(k)] = catmullRom(t - static_cast<float>(k - 1)); }
    else { for (int k = 0; k < 4; ++k) w[static_cast<size_t>(k)] = lanczos2(t - static_cast<float>(k - 1)); }
    const float sum = w[0] + w[1] + w[2] + w[3];
    if (std::fabs(sum) > 1e-6f) for (float& v : w) v /= sum;
    std::array<int, 4> ix{};
    for (int k = 0; k < 4; ++k) ix[static_cast<size_t>(k)] = clampi(base - 1 + k, srcN);
    ax.idx[static_cast<size_t>(d)] = ix;
    ax.w[static_cast<size_t>(d)] = w;
  }
  return ax;
}

inline float lumaOf(const uint8_t* p) { return 0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2]; }

} // namespace upscale_detail

// Reamostra RGBA8. dst precisa ter dw*dh*4 bytes. Nao altera src.
inline void upscaleRgba(const uint8_t* src, int sw, int sh, uint8_t* dst, int dw, int dh, UpscaleFilter f) {
  if (!src || !dst || sw < 1 || sh < 1 || dw < 1 || dh < 1) return;
  using namespace upscale_detail;
  const Axis ax = makeAxis(sw, dw, f), ay = makeAxis(sh, dh, f);
  const size_t dstRow = static_cast<size_t>(dw) * 4, srcRow = static_cast<size_t>(sw) * 4;
  const int k0 = f == UpscaleFilter::Bilinear ? 1 : 0, k1 = f == UpscaleFilter::Bilinear ? 3 : 4;
  parallelFor(static_cast<size_t>(dh), 8, [&](size_t y0, size_t y1) {
    for (size_t y = y0; y < y1; ++y) {
      const auto& wy = ay.w[y];
      const uint8_t* rows[4];
      for (int k = 0; k < 4; ++k) rows[k] = src + static_cast<size_t>(ay.idx[y][static_cast<size_t>(k)]) * srcRow;
      uint8_t* out = dst + y * dstRow;
      for (int x = 0; x < dw; ++x) {
        const auto& wx = ax.w[static_cast<size_t>(x)];
        const auto& cx = ax.idx[static_cast<size_t>(x)];
        float acc0 = 0, acc1 = 0, acc2 = 0, acc3 = 0;
        for (int ky = k0; ky < k1; ++ky) {       // bilinear: so 2x2 taps (k = 1..2); filtros de 4 taps: 4x4
          const float wyk = wy[static_cast<size_t>(ky)];
          if (wyk == 0.f) continue;
          const uint8_t* row = rows[ky];
          for (int kx = k0; kx < k1; ++kx) {
            const float wgt = wyk * wx[static_cast<size_t>(kx)];
            if (wgt == 0.f) continue;
            const uint8_t* p = row + static_cast<size_t>(cx[static_cast<size_t>(kx)]) * 4;
            acc0 += wgt * p[0]; acc1 += wgt * p[1]; acc2 += wgt * p[2]; acc3 += wgt * p[3];
          }
        }
        uint8_t* o = out + static_cast<size_t>(x) * 4;
        o[0] = static_cast<uint8_t>(std::clamp(acc0 + 0.5f, 0.f, 255.f));
        o[1] = static_cast<uint8_t>(std::clamp(acc1 + 0.5f, 0.f, 255.f));
        o[2] = static_cast<uint8_t>(std::clamp(acc2 + 0.5f, 0.f, 255.f));
        o[3] = static_cast<uint8_t>(std::clamp(acc3 + 0.5f, 0.f, 255.f));
      }
    }
  });
}

// Antialiasing FXAA (versao simplificada do algoritmo de Timothy Lottes). IN PLACE.
// scratch = copia da imagem + buffer de luma (reutilizados entre quadros, sem alocar a cada frame).
inline void fxaaRgba(uint8_t* rgba, int w, int h, std::vector<uint8_t>& scratch) {
  if (!rgba || w < 3 || h < 3) return;
  using namespace upscale_detail;
  const size_t n = static_cast<size_t>(w) * h;
  scratch.resize(n * 4 + n * sizeof(float));
  uint8_t* copy = scratch.data();
  std::memcpy(copy, rgba, n * 4);
  float* lum = reinterpret_cast<float*>(scratch.data() + n * 4);   // luma 0..1 de cada pixel (calculada UMA vez)
  parallelFor(static_cast<size_t>(h), 16, [&](size_t y0, size_t y1) {
    for (size_t y = y0; y < y1; ++y) for (int x = 0; x < w; ++x) lum[y * w + x] = lumaOf(copy + (y * w + x) * 4) * (1.f / 255.f);
  });
  auto L = [&](int x, int y) { return lum[static_cast<size_t>(clampi(y, h)) * w + clampi(x, w)]; };
  auto S = [&](int x, int y) { return copy + (static_cast<size_t>(clampi(y, h)) * w + clampi(x, w)) * 4; };
  // Amostra bilinear da imagem original em coordenadas de pixel (fracionarias).
  auto sample = [&](float fx, float fy, float out[3]) {
    fx = std::clamp(fx, 0.f, static_cast<float>(w - 1)); fy = std::clamp(fy, 0.f, static_cast<float>(h - 1));
    const int x0 = static_cast<int>(fx), y0 = static_cast<int>(fy);
    const float tx = fx - static_cast<float>(x0), ty = fy - static_cast<float>(y0);
    const uint8_t* a = S(x0, y0); const uint8_t* b = S(x0 + 1, y0); const uint8_t* c = S(x0, y0 + 1); const uint8_t* d = S(x0 + 1, y0 + 1);
    for (int k = 0; k < 3; ++k) out[k] = (a[k] * (1 - tx) + b[k] * tx) * (1 - ty) + (c[k] * (1 - tx) + d[k] * tx) * ty;
  };
  constexpr float kReduceMul = 1.f / 8.f, kReduceMin = 1.f / 128.f, kSpanMax = 8.f;   // constantes do FXAA
  constexpr float kEdgeThreshold = 1.f / 8.f, kEdgeThresholdMin = 1.f / 32.f;          // contraste relativo / absoluto
  parallelFor(static_cast<size_t>(h), 16, [&](size_t yy0, size_t yy1) {
    for (size_t yy = yy0; yy < yy1; ++yy) {
      const int y = static_cast<int>(yy);
      uint8_t* out = rgba + yy * static_cast<size_t>(w) * 4;
      for (int x = 0; x < w; ++x) {
        uint8_t* o = out + static_cast<size_t>(x) * 4;
        const uint8_t* c = S(x, y);
        const float lM = L(x, y), lN = L(x, y - 1), lS = L(x, y + 1), lW = L(x - 1, y), lE = L(x + 1, y);
        const float lNW = L(x - 1, y - 1), lNE = L(x + 1, y - 1), lSW = L(x - 1, y + 1), lSE = L(x + 1, y + 1);
        const float lumaMin = std::min({lM, lN, lS, lW, lE}), lumaMax = std::max({lM, lN, lS, lW, lE});
        const float range = lumaMax - lumaMin;
        if (range < std::max(kEdgeThresholdMin, lumaMax * kEdgeThreshold)) { std::memcpy(o, c, 4); continue; }   // sem borda: nao mexe
        // direcao da borda em pixels (reduzida e limitada), como no FXAA de referencia
        float dirX = -((lNW + lNE) - (lSW + lSE)), dirY = (lNW + lSW) - (lNE + lSE);
        const float dirReduce = std::max((lNW + lNE + lSW + lSE) * (0.25f * kReduceMul), kReduceMin);
        const float rcpDirMin = 1.f / (std::min(std::fabs(dirX), std::fabs(dirY)) + dirReduce);
        dirX = std::clamp(dirX * rcpDirMin, -kSpanMax, kSpanMax);
        dirY = std::clamp(dirY * rcpDirMin, -kSpanMax, kSpanMax);
        float a[3], b[3], n1[3], n2[3], rgbA[3], rgbB[3];
        sample(x + dirX * (1.f / 3.f - 0.5f), y + dirY * (1.f / 3.f - 0.5f), a);
        sample(x + dirX * (2.f / 3.f - 0.5f), y + dirY * (2.f / 3.f - 0.5f), b);
        for (int k = 0; k < 3; ++k) rgbA[k] = 0.5f * (a[k] + b[k]);
        sample(x + dirX * -0.5f, y + dirY * -0.5f, n1);
        sample(x + dirX * 0.5f, y + dirY * 0.5f, n2);
        for (int k = 0; k < 3; ++k) rgbB[k] = rgbA[k] * 0.5f + 0.25f * (n1[k] + n2[k]);
        // a mistura larga so vale se nao sair do intervalo de luma da vizinhanca (senao usa a curta)
        const float lumaB = (0.299f * rgbB[0] + 0.587f * rgbB[1] + 0.114f * rgbB[2]) / 255.f;
        const float* res = (lumaB < lumaMin || lumaB > lumaMax) ? rgbA : rgbB;
        for (int k = 0; k < 3; ++k) o[k] = static_cast<uint8_t>(std::clamp(res[k] + 0.5f, 0.f, 255.f));
        o[3] = c[3];
      }
    }
  });
}

} // namespace tool
