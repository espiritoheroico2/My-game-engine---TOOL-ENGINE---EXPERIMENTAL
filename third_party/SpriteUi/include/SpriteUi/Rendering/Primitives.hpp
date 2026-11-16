#pragma once

#include "SpriteUi/Rendering/PanelSdf.hpp"
#include "SpriteUi/Rendering/RenderFlags.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace ToolEngine::SpriteUi::Rendering {

[[nodiscard]] inline UIRenderItem MakeSolidRect(
    UIHandle owner, Rect bounds, Vec4 color, std::uint32_t clipIndex = NoClipIndex,
    float zOrder = 0.0f, std::uint64_t paintOrder = 0) noexcept
{
    UIRenderItem item;
    item.handle = owner;
    item.layout.resolvedPosition = bounds.position;
    item.layout.resolvedSize = bounds.size;
    item.tintPremultipliedLinear = color;
    item.textureSlice = WhiteTextureSlice;
    item.clipIndex = clipIndex;
    item.zOrder = zOrder;
    item.paintOrder = paintOrder;
    return item;
}

[[nodiscard]] inline std::optional<UIRenderItem> MakeRoundedRect(
    UIHandle owner, Rect bounds, float radius, Vec4 color, std::uint32_t clipIndex = NoClipIndex,
    float zOrder = 0.0f, std::uint64_t paintOrder = 0) noexcept
{
    if (!owner.IsValid() || !IsFinite(bounds.position) || !IsFinite(bounds.size) ||
        bounds.size.x <= 0.0f || bounds.size.y <= 0.0f || !IsFinite(radius) || !IsFinite(zOrder)) return std::nullopt;
    UIRenderItem item = MakeSolidRect(owner, bounds, color, clipIndex, zOrder, paintOrder);
    item.flags |= FlagRoundedPanel;
    item.cornerRadius = std::max(0.0f, radius);
    return item;
}

[[nodiscard]] inline std::optional<UIRenderItem> MakeLine(
    UIHandle owner, Vec2 from, Vec2 to, float thickness, Vec4 color,
    std::uint32_t clipIndex = NoClipIndex, float zOrder = 0.0f, std::uint64_t paintOrder = 0) noexcept
{
    if (!owner.IsValid() || !IsFinite(from) || !IsFinite(to) || !IsFinite(thickness) ||
        thickness <= 0.0f || !IsFinite(zOrder)) return std::nullopt;
    const Vec2 delta = to - from;
    const float length = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    if (!IsFinite(length) || length <= 1.0e-6f) return std::nullopt;
    const Vec2 center = (from + to) * 0.5f;
    UIRenderItem item = MakeSolidRect(owner, {{center.x - length * 0.5f, center.y - thickness * 0.5f}, {length, thickness}},
                                      color, clipIndex, zOrder, paintOrder);
    item.transform.rotationRadians = std::atan2(delta.y, delta.x);
    return item;
}

} // namespace ToolEngine::SpriteUi::Rendering
