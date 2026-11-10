#pragma once
// Shared math/bit utilities used across multiple modules.
#include <cstdint>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace LightPrism {

inline uint64_t NextPow2(uint64_t v) noexcept {
    uint64_t p = 16;
    while (p < v) p <<= 1;
    return p;
}

inline uint32_t DivUp(uint32_t a, uint32_t b) noexcept { return (a + b - 1) / b; }

// Cross-platform population count for 32-bit integers.
inline int PopCount32(uint32_t v) noexcept {
#if defined(_MSC_VER)
    return static_cast<int>(__popcnt(v));
#else
    return __builtin_popcount(v);
#endif
}

// Cross-platform count-leading-zeros for 32-bit integers.
inline int Clz32(uint32_t v) noexcept {
    if (v == 0) return 32;
#if defined(_MSC_VER)
    unsigned long idx;
    _BitScanReverse(&idx, v);
    return 31 - static_cast<int>(idx);
#else
    return __builtin_clz(v);
#endif
}

} // namespace LightPrism
