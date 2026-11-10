// Tone mapping + sRGB. Espelha include/LightPrism/Reference/Tonemap.hpp.
// Se o render target final já é *_SRGB, o hardware converte: use só ACESFilm3().
#ifndef LP_TONEMAP_HLSLI
#define LP_TONEMAP_HLSLI

float3 ACESFilm3(float3 x) {
    const float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float3 LinearToSRGB(float3 c) {
    c = saturate(c);
    float3 lo = 12.92f * c;
    float3 hi = 1.055f * pow(c, 1.0f / 2.4f) - 0.055f;
    return lerp(hi, lo, step(c, 0.0031308f));
}

float3 TonemapToSRGB(float3 hdr) {
    return LinearToSRGB(ACESFilm3(hdr));
}

#endif
