#pragma once

// Regras puras (sem SDL) dos valores digitados livremente e da cor com alfa:
//   - tamanho de fonte digitado em pixels: aceito de 4 a 200, so prendido por seguranca (o deslizante continua
//     com a faixa curta, usada so para arrastar);
//   - escala do texto da interface digitada: de 0.5 a 4;
//   - cor RGBA lida de dados antigos: sem o quarto numero, o alfa fica 1 (opaco).
// Exemplo: digitar 96 no campo de tamanho vira 96 (o deslizante fica no fim da faixa); digitar 900 vira 200.

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace editor::free_values {

// Limites do tamanho de fonte digitado (px).
inline constexpr float kFontPxMin = 4.f;
inline constexpr float kFontPxMax = 200.f;
// Limites da escala do texto digitada.
inline constexpr float kTextScaleTypedMin = 0.5f;
inline constexpr float kTextScaleTypedMax = 4.f;

// Tamanho de fonte digitado (px): NaN vira `fallback`; o resto e prendido em [4, 200].
[[nodiscard]] inline float ClampFontPx(float value, float fallback = 12.f) noexcept {
    if (std::isnan(value)) value = fallback;
    return std::clamp(value, kFontPxMin, kFontPxMax);
}

// Escala do texto digitada: NaN vira 1; o resto e prendido em [0.5, 4].
[[nodiscard]] inline float ClampTypedTextScale(float value) noexcept {
    if (std::isnan(value)) return 1.f;
    return std::clamp(value, kTextScaleTypedMin, kTextScaleTypedMax);
}

// Le uma cor RGBA de `count` numeros (0 a 3 ou 4 canais) para `rgba`: canais ausentes ficam como estavam, e o alfa
// ausente (dados antigos so com RGB) vira 1. Todos os canais lidos ficam presos em [0, 1].
inline void ReadRgba(const double* values, std::size_t count, float* rgba) noexcept {
    for (std::size_t i = 0; i < 4; ++i) {
        if (i < count && std::isfinite(values[i])) rgba[i] = std::clamp(static_cast<float>(values[i]), 0.f, 1.f);
    }
    if (count < 4) rgba[3] = 1.f;
}

} // namespace editor::free_values
