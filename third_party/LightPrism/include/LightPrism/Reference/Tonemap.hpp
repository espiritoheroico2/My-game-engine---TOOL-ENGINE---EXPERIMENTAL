#pragma once
// Referência CPU de tone mapping + conversão sRGB. Espelha shaders/Common/Tonemap.hlsli.
// A exposição já vem aplicada nas intensidades (preExposure), não é reaplicada aqui.
#include <cmath>

#include "LightPrism/Math.hpp"

namespace LightPrism::Reference {

// ACES Film (aproximação de Narkowicz). Entrada linear HDR, saída linear [0,1].
inline float ACESFilm(float x) {
    const float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
    return Saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// Conversão exata linear -> sRGB (não usa o pow(1/2.2) aproximado).
inline float LinearToSRGB(float x) {
    x = Saturate(x);
    return x <= 0.0031308f ? 12.92f * x : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
}

inline Vec3 TonemapToSRGB(const Vec3& hdr) {
    return {LinearToSRGB(ACESFilm(hdr.x)), LinearToSRGB(ACESFilm(hdr.y)), LinearToSRGB(ACESFilm(hdr.z))};
}

} // namespace LightPrism::Reference
