#pragma once

#include <array>
#include <cmath>
#include <cstdint>

namespace ToolEngine::SpriteUi::Text {

// Coverage atlases are rasterized above their logical size so linear filtering stays crisp on HiDPI displays.
[[nodiscard]] inline std::uint32_t ChooseBitmapRasterPixelSize(float logicalFontSize, float displayScale) noexcept {
    constexpr std::array<std::uint32_t, 8> kBuckets{24, 32, 48, 64, 96, 128, 192, 256};
    if (!std::isfinite(logicalFontSize) || logicalFontSize <= 0.0f) logicalFontSize = 16.0f;
    if (!std::isfinite(displayScale) || displayScale <= 0.0f) displayScale = 1.0f;
    const float desired = logicalFontSize * displayScale * 1.25f;
    for (const std::uint32_t bucket : kBuckets) {
        if (desired <= static_cast<float>(bucket)) return bucket;
    }
    return kBuckets.back();
}

} // namespace ToolEngine::SpriteUi::Text
