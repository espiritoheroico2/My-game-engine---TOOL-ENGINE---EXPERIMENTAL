#pragma once

#include "SpriteUi/Core/Math.hpp"

#include <algorithm>
#include <cmath>

namespace ToolEngine::SpriteUi::Rendering {

// CPU reference of the analytic rounded-rectangle SDF evaluated by the pixel shader when
// FlagRoundedPanel is set (specification V4.3, section 7).
//
//   samplePos : pixel position relative to the CENTRE of the panel (logical pixels)
//   halfSize  : half extents of the panel
//   radius    : corner radius, clamped to [0, min(halfSize.x, halfSize.y)]
//
// Returns the signed distance to the edge: negative inside, zero on the edge, positive outside.
//
// Differs from the snippet in the specification only by the radius clamp: without it, a radius
// larger than a half extent makes the distance field degenerate (the panel collapses or inverts).
[[nodiscard]] inline float EvaluatePanelSdf(Vec2 samplePos, Vec2 halfSize, float radius) noexcept {
    const float maxRadius = std::max(0.0f, std::min(halfSize.x, halfSize.y));
    const float r = std::isfinite(radius) ? std::clamp(radius, 0.0f, maxRadius) : 0.0f;
    const float dx = std::abs(samplePos.x) - halfSize.x + r;
    const float dy = std::abs(samplePos.y) - halfSize.y + r;
    const float outside = std::sqrt(std::max(dx, 0.0f) * std::max(dx, 0.0f) + std::max(dy, 0.0f) * std::max(dy, 0.0f));
    return outside + std::min(std::max(dx, dy), 0.0f) - r;
}

// Anti-aliased coverage in [0, 1] from a signed distance and the size of one device pixel in the
// same units (the shader uses fwidth(distance) for it).
[[nodiscard]] inline float PanelCoverage(float signedDistance, float pixelSize) noexcept {
    const float aa = pixelSize > 1.0e-6f ? pixelSize : 1.0e-6f;
    return std::clamp(0.5f - signedDistance / aa, 0.0f, 1.0f);
}

} // namespace ToolEngine::SpriteUi::Rendering
