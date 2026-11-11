#pragma once

#include "SpriteUi/Core/Math.hpp"

#include <cmath>

namespace ToolEngine::SpriteUi::Text {

struct FontMetrics {
    float fontSize = 16.0f;
    float ascent = 12.0f;
    float descent = -4.0f;
    float lineGap = 4.0f;
    float unitsPerEm = 1000.0f;
    float pxRange = 4.0f;
    float atlasWidth = 512.0f;
    float atlasHeight = 512.0f;

    [[nodiscard]] constexpr float GetLineHeight() const noexcept {
        return (ascent - descent) + lineGap;
    }

    [[nodiscard]] constexpr float GetScale() const noexcept {
        if (unitsPerEm <= 0.0f) return 1.0f;
        return fontSize / unitsPerEm;
    }

    [[nodiscard]] bool IsValid() const noexcept {
        return IsFinite(fontSize) && fontSize > 0.0f &&
               IsFinite(ascent) && IsFinite(descent) && ascent >= descent &&
               IsFinite(lineGap) && lineGap >= 0.0f &&
               IsFinite(unitsPerEm) && unitsPerEm > 0.0f &&
               IsFinite(pxRange) && pxRange > 0.0f &&
               IsFinite(atlasWidth) && atlasWidth > 0.0f &&
               IsFinite(atlasHeight) && atlasHeight > 0.0f;
    }
};

} // namespace ToolEngine::SpriteUi::Text

