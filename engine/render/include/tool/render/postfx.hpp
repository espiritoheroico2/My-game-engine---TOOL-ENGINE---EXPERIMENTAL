// ToolEngine - pos-processamento de imagem (global e local por camera).
//
// Roda sobre a imagem final RGBA8 (depois do 3D), em CPU, em ponto fixo, dividido em faixas de linhas num pool de threads persistente.
// Em vez de varias passadas, tudo e combinado em UMA:
//   - exposicao + contraste viram uma tabela de 256 entradas por canal (sem pow() por pixel);
//   - saturacao e tint usam o brilho do pixel; vinheta usa uma tabela de x^2 por coluna + y^2 por linha (sem sqrt);
//   - sharpen (nitidez) e a unica passada extra e so roda se o valor for > 0.
// Se nada estiver ativo, applyPostFx() nem toca na imagem (custo zero).
//
// GLOBAL = valores da cena (valem para todas as cameras). LOCAL = valores guardados no objeto Camera.
// Local "Somar" combina com o global (exposicao soma, contraste/saturacao multiplicam); "Substituir" ignora o global.
#pragma once
#include "tool/core/parallel.hpp"
#include "tool/math/math.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace tool {

struct PostFx {
  bool enabled = false;
  float exposure = 0.f;      // em "stops" (EV): +1 dobra o brilho
  float contrast = 1.f;      // 1 = igual
  float saturation = 1.f;    // 0 = preto e branco; 1 = igual; 2 = vivo
  float tint[3] = {1.f, 1.f, 1.f};
  float tintStrength = 0.f;  // 0..1: quanto misturar a cor de tint
  float vignette = 0.f;      // 0..1: escurece os cantos
  float vignetteSize = 0.75f;// raio (0..1.5) onde a vinheta comeca
  float sharpen = 0.f;       // 0..1: nitidez
  bool additive = true;      // so para o LOCAL: true = soma ao global; false = substitui
  bool active() const {
    return enabled && (std::abs(exposure) > 1e-3f || std::abs(contrast - 1.f) > 1e-3f || std::abs(saturation - 1.f) > 1e-3f || tintStrength > 1e-3f || vignette > 1e-3f || sharpen > 1e-3f);
  }
};

// Resultado final para uma camera: global + local (ver cabecalho).
inline PostFx combinePostFx(const PostFx& global, const PostFx& local) {
  if (!local.enabled) return global;
  if (!local.additive || !global.enabled) return local;
  PostFx r = global;
  r.exposure += local.exposure; r.contrast *= local.contrast; r.saturation *= local.saturation;
  if (local.tintStrength > 0.f) {
    const float a = local.tintStrength / std::max(1e-3f, local.tintStrength + global.tintStrength);
    for (int i = 0; i < 3; ++i) r.tint[i] = global.tint[i] * (1.f - a) + local.tint[i] * a;
    r.tintStrength = std::min(1.f, global.tintStrength + local.tintStrength);
  }
  r.vignette = std::max(global.vignette, local.vignette); r.vignetteSize = std::min(global.vignetteSize, local.vignetteSize);
  r.sharpen = std::min(1.f, global.sharpen + local.sharpen);
  return r;
}

// Tudo em inteiros (ponto fixo) e numa unica passada: LUT (exposicao+contraste) -> matriz 3x3 (saturacao*tint) -> vinheta por tabela.
// As faixas de linhas rodam no pool de threads persistente (tool::parallelFor). A nitidez usa so 3 linhas de buffer por faixa (sem copiar a imagem).
inline void applyPostFx(u8* rgba, int w, int h, const PostFx& fx) {
  if (!rgba || w < 2 || h < 2 || !fx.active()) return;
  u8 lut[256];
  const float gain = std::exp2(fx.exposure);
  for (int i = 0; i < 256; ++i) {
    float v = static_cast<float>(i) / 255.f * gain;
    v = (v - 0.5f) * fx.contrast + 0.5f;
    lut[i] = static_cast<u8>(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f);
  }
  const bool doSat = std::abs(fx.saturation - 1.f) > 1e-3f, doTint = fx.tintStrength > 1e-3f, doVig = fx.vignette > 1e-3f, doMat = doSat || doTint;
  // matriz de cor Q12: out_c = tint_c * ((1-s)*luma + s*c)
  int M[3][3] = {};
  if (doMat) {
    const float wl[3] = {0.299f, 0.587f, 0.114f}, s = fx.saturation, k = fx.tintStrength;
    for (int c = 0; c < 3; ++c) {
      const float tc = doTint ? (1.f - k + fx.tint[c] * k) : 1.f;
      for (int j = 0; j < 3; ++j) M[c][j] = static_cast<int>(std::lround(tc * ((1.f - s) * wl[j] + (j == c ? s : 0.f)) * 4096.f));
    }
  }
  // vinheta: fator Q12 por r^2 quantizado (1024 passos entre a borda interna e a externa); r^2 = dx2[x] + dy2 em Q16
  std::vector<int> dx2(static_cast<size_t>(w)); int vlut[1025];
  const float v0 = fx.vignetteSize * fx.vignetteSize, v1 = v0 + 1.2f;
  const int v0q = static_cast<int>(v0 * 65536.f), spanQ = std::max(1, static_cast<int>((v1 - v0) * 65536.f));
  const long long invSpan = (1024ll << 16) / spanQ; // 1024 passos / span, em Q16 (evita divisao por pixel)
  if (doVig) {
    for (int x = 0; x < w; ++x) { const float nx = (static_cast<float>(x) + 0.5f) / static_cast<float>(w) * 2.f - 1.f; dx2[static_cast<size_t>(x)] = static_cast<int>(nx * nx * 65536.f); }
    for (int i = 0; i <= 1024; ++i) { float t = static_cast<float>(i) / 1024.f; t = t * t * (3.f - 2.f * t); vlut[i] = static_cast<int>((1.f - fx.vignette * t) * 4096.f); }
  }
  const size_t grain = static_cast<size_t>(std::max(8, 65536 / std::max(1, w))); // ~64k pixels por bloco
  parallelFor(static_cast<size_t>(h), grain, [&](size_t y0, size_t y1) {
    for (size_t y = y0; y < y1; ++y) {
      const float ny = (static_cast<float>(y) + 0.5f) / static_cast<float>(h) * 2.f - 1.f; const int dy2 = doVig ? static_cast<int>(ny * ny * 65536.f) : 0;
      u8* p = rgba + y * static_cast<size_t>(w) * 4;
      for (int x = 0; x < w; ++x, p += 4) {
        int r = lut[p[0]], g = lut[p[1]], b = lut[p[2]];
        if (doMat) {
          const int nr = (M[0][0] * r + M[0][1] * g + M[0][2] * b) >> 12, ng = (M[1][0] * r + M[1][1] * g + M[1][2] * b) >> 12, nb = (M[2][0] * r + M[2][1] * g + M[2][2] * b) >> 12;
          r = nr; g = ng; b = nb;
        }
        if (doVig) {
          const int d = dx2[static_cast<size_t>(x)] + dy2 - v0q;
          if (d > 0) { const int idx = std::min(1024, static_cast<int>((static_cast<long long>(d) * invSpan) >> 16)); const int k = vlut[idx]; r = (r * k) >> 12; g = (g * k) >> 12; b = (b * k) >> 12; }
        }
        p[0] = static_cast<u8>(std::clamp(r, 0, 255)); p[1] = static_cast<u8>(std::clamp(g, 0, 255)); p[2] = static_cast<u8>(std::clamp(b, 0, 255));
      }
    }
  });
  // nitidez (unsharp 3x3): out = c + (4c - soma_vizinhos) * k/2. Cada faixa guarda copias das linhas ORIGINAIS que vai sobrescrever/vizinhas.
  if (fx.sharpen > 1e-3f) {
    const int kq = static_cast<int>(std::lround(fx.sharpen * 0.8f * 256.f)); // (c - nb)*k*2 com nb = soma/4  ->  (4c - soma) * k/2 ; k = sharpen*0.8
    const size_t rw = static_cast<size_t>(w) * 4;
    const size_t nb = std::min<size_t>(ParallelPool::get().threads(), static_cast<size_t>(h) / 16 + 1);
    std::vector<std::vector<u8>> edgeTop(nb), edgeBot(nb); // linha acima da faixa / primeira linha da faixa seguinte (originais)
    auto bandStart = [&](size_t i) { return static_cast<size_t>(h) * i / nb; };
    for (size_t i = 0; i < nb; ++i) {
      const size_t y0 = bandStart(i), y1 = bandStart(i + 1);
      if (y0 > 0) edgeTop[i].assign(rgba + (y0 - 1) * rw, rgba + y0 * rw);
      if (y1 < static_cast<size_t>(h)) edgeBot[i].assign(rgba + y1 * rw, rgba + (y1 + 1) * rw);
    }
    parallelFor(nb, 1, [&](size_t b0, size_t b1) {
      std::vector<u8> prev(rw), cur(rw), next(rw);
      for (size_t bi = b0; bi < b1; ++bi) {
        const size_t y0 = bandStart(bi), y1 = bandStart(bi + 1);
        if (y0 > 0) prev = edgeTop[bi];
        std::copy(rgba + y0 * rw, rgba + (y0 + 1) * rw, cur.begin());
        for (size_t y = y0; y < y1; ++y) {
          if (y + 1 < y1) std::copy(rgba + (y + 1) * rw, rgba + (y + 2) * rw, next.begin()); else if (!edgeBot[bi].empty()) next = edgeBot[bi];
          if (y >= 1 && y + 1 < static_cast<size_t>(h)) {
            u8* o = rgba + y * rw;
            for (int x = 1; x < w - 1; ++x) {
              const size_t i = static_cast<size_t>(x) * 4;
              for (size_t c = 0; c < 3; ++c) {
                const int ce = cur[i + c], sum = cur[i + c - 4] + cur[i + c + 4] + prev[i + c] + next[i + c];
                o[i + c] = static_cast<u8>(std::clamp(ce + (((4 * ce - sum) * kq) >> 8), 0, 255));
              }
            }
          }
          std::swap(prev, cur); std::swap(cur, next);
        }
      }
    });
  }
}

} // namespace tool
