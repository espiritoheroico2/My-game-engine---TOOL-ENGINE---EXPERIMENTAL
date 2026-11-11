#pragma once

#include "SpriteUi/Core/Math.hpp"

#include <optional>

namespace ToolEngine::SpriteUi {

struct UIRectTransform {
    Vec2 anchorMin{0.0f, 0.0f};
    Vec2 anchorMax{0.0f, 0.0f};
    Vec2 offsetMin{0.0f, 0.0f};
    Vec2 offsetMax{100.0f, 40.0f};
    Vec2 pivot{0.5f, 0.5f};
    Vec2 scale{1.0f, 1.0f};
    float rotationRadians = 0.0f;
    float depth = 0.0f;
};

struct UILayoutRect {
    Vec2 resolvedPosition{};
    Vec2 resolvedSize{};
    bool visible = true;
    bool dirty = true;
};

[[nodiscard]] inline std::optional<Rect> ResolveAnchoredRect(
    const Rect& parent,
    const UIRectTransform& transform) noexcept
{
    if (!IsFinite(parent.position) || !IsFinite(parent.size) ||
        !IsFinite(transform.anchorMin) || !IsFinite(transform.anchorMax) ||
        !IsFinite(transform.offsetMin) || !IsFinite(transform.offsetMax) ||
        parent.size.x < 0.0f || parent.size.y < 0.0f) {
        return std::nullopt;
    }

    constexpr float epsilon = 1.0e-5f;
    const auto validAnchor = [](float value) {
        return value >= -epsilon && value <= 1.0f + epsilon;
    };

    if (!validAnchor(transform.anchorMin.x) || !validAnchor(transform.anchorMin.y) ||
        !validAnchor(transform.anchorMax.x) || !validAnchor(transform.anchorMax.y)) {
        return std::nullopt;
    }
    if (transform.anchorMin.x > transform.anchorMax.x ||
        transform.anchorMin.y > transform.anchorMax.y) {
        return std::nullopt;
    }

    const Vec2 minAnchor{
        parent.position.x + parent.size.x * transform.anchorMin.x,
        parent.position.y + parent.size.y * transform.anchorMin.y
    };
    const Vec2 maxAnchor{
        parent.position.x + parent.size.x * transform.anchorMax.x,
        parent.position.y + parent.size.y * transform.anchorMax.y
    };

    const Vec2 minimum = minAnchor + transform.offsetMin;
    const Vec2 maximum = maxAnchor + transform.offsetMax;
    const Vec2 size{
        std::max(0.0f, maximum.x - minimum.x),
        std::max(0.0f, maximum.y - minimum.y)
    };

    if (!IsFinite(minimum) || !IsFinite(size)) return std::nullopt;
    return Rect{minimum, size};
}

// Maps a unit quad [0,1]x[0,1] to the resolved rectangle, applying scale/rotation
// around the normalized pivot. This is the exact 2x3 transform packed into GPU rows.
[[nodiscard]] inline std::optional<Affine2D> BuildUnitQuadTransform(
    const UILayoutRect& layout,
    const UIRectTransform& transform) noexcept
{
    if (!layout.visible || !IsFinite(layout.resolvedPosition) ||
        !IsFinite(layout.resolvedSize) || !IsFinite(transform.pivot) ||
        !IsFinite(transform.scale) || !IsFinite(transform.rotationRadians) ||
        !IsFinite(transform.depth) || layout.resolvedSize.x < 0.0f ||
        layout.resolvedSize.y < 0.0f) {
        return std::nullopt;
    }

    const float c = std::cos(transform.rotationRadians);
    const float s = std::sin(transform.rotationRadians);

    Affine2D result;
    result.m00 = c * transform.scale.x * layout.resolvedSize.x;
    result.m01 = -s * transform.scale.y * layout.resolvedSize.y;
    result.m10 = s * transform.scale.x * layout.resolvedSize.x;
    result.m11 = c * transform.scale.y * layout.resolvedSize.y;

    const Vec2 pivotLocal = transform.pivot;
    const Vec2 pivotWorld{
        layout.resolvedPosition.x + transform.pivot.x * layout.resolvedSize.x,
        layout.resolvedPosition.y + transform.pivot.y * layout.resolvedSize.y
    };

    result.tx = pivotWorld.x - result.m00 * pivotLocal.x - result.m01 * pivotLocal.y;
    result.ty = pivotWorld.y - result.m10 * pivotLocal.x - result.m11 * pivotLocal.y;

    if (!IsFinite(Vec2{result.m00, result.m01}) ||
        !IsFinite(Vec2{result.tx, result.ty}) ||
        !IsFinite(Vec2{result.m10, result.m11})) {
        return std::nullopt;
    }
    return result;
}

// Maps a pixel-space local rectangle [0,width]x[0,height] to screen space.
// Use this transform for HitTarget::localToScreen so localSize remains in logical pixels.
[[nodiscard]] inline std::optional<Affine2D> BuildHitTestTransform(
    const UILayoutRect& layout,
    const UIRectTransform& transform) noexcept
{
    if (!layout.visible || !IsFinite(layout.resolvedPosition) ||
        !IsFinite(layout.resolvedSize) || !IsFinite(transform.pivot) ||
        !IsFinite(transform.scale) || !IsFinite(transform.rotationRadians) ||
        layout.resolvedSize.x <= 0.0f || layout.resolvedSize.y <= 0.0f) {
        return std::nullopt;
    }

    const float c = std::cos(transform.rotationRadians);
    const float s = std::sin(transform.rotationRadians);
    Affine2D result;
    result.m00 = c * transform.scale.x;
    result.m01 = -s * transform.scale.y;
    result.m10 = s * transform.scale.x;
    result.m11 = c * transform.scale.y;

    const Vec2 pivotPixels{
        transform.pivot.x * layout.resolvedSize.x,
        transform.pivot.y * layout.resolvedSize.y
    };
    const Vec2 pivotScreen = layout.resolvedPosition + pivotPixels;
    result.tx = pivotScreen.x - result.m00 * pivotPixels.x - result.m01 * pivotPixels.y;
    result.ty = pivotScreen.y - result.m10 * pivotPixels.x - result.m11 * pivotPixels.y;

    if (!result.IsFinite() || !IsFinite(transform.depth)) return std::nullopt;
    return result;
}

} // namespace ToolEngine::SpriteUi
