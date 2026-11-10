#pragma once
// Shared sampling utilities: quasi-random sequences used by IBL, SSAO, and ray tracing.
#include <cmath>
#include <cstdint>

#include "LightPrism/Math.hpp"

namespace LightPrism {

inline float RadicalInverse(uint32_t bits) noexcept {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10f;
}

// Orthonormal basis from a single normal (Frisvad / Pixar 2017 revised).
inline void MakeTangentBasis(const Vec3& n, Vec3& t, Vec3& b) noexcept {
    const Vec3 up = std::fabs(n.z) < 0.999f ? Vec3{0, 0, 1} : Vec3{1, 0, 0};
    t = Normalize(Cross(up, n));
    b = Cross(n, t);
}

} // namespace LightPrism
