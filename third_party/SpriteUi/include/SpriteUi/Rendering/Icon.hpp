#pragma once

#include "SpriteUi/Rendering/DrawList.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace ToolEngine::SpriteUi::Rendering {

struct UIIcon {
    std::uint32_t textureSlice = WhiteTextureSlice;
    std::array<float, 4> uvRect{0.0f, 0.0f, 1.0f, 1.0f};
    Vec2 sourceSize{};

    [[nodiscard]] bool IsValid() const noexcept {
        if (!IsFinite(sourceSize) || sourceSize.x <= 0.0f || sourceSize.y <= 0.0f) {
            return false;
        }
        for (const float uv : uvRect) {
            if (!IsFinite(uv)) return false;
        }
        return uvRect[2] > 0.0f && uvRect[3] > 0.0f &&
               uvRect[0] >= 0.0f && uvRect[1] >= 0.0f &&
               uvRect[0] + uvRect[2] <= 1.0f &&
               uvRect[1] + uvRect[3] <= 1.0f;
    }
};

[[nodiscard]] inline std::optional<UIRenderItem> MakeIconRenderItem(
    const UIIcon& icon,
    const Rect& bounds,
    Vec4 tintPremultipliedLinear,
    UIHandle owner,
    std::uint32_t clipIndex = NoClipIndex,
    float zOrder = 0.0f,
    std::uint64_t paintOrder = 0) noexcept {
    if (!icon.IsValid() || !owner.IsValid() || !IsFinite(bounds.position) ||
        !IsFinite(bounds.size) || bounds.size.x <= 0.0f || bounds.size.y <= 0.0f ||
        !IsValidPremultipliedTint(tintPremultipliedLinear) || !IsFinite(zOrder)) {
        return std::nullopt;
    }

    UIRenderItem item;
    item.handle = owner;
    item.layout = {bounds.position, bounds.size, true, false};
    item.transform.pivot = {0.0f, 0.0f};
    item.transform.offsetMax = bounds.size;
    std::copy(icon.uvRect.begin(), icon.uvRect.end(), item.uvRect);
    item.tintPremultipliedLinear = tintPremultipliedLinear;
    item.textureSlice = icon.textureSlice;
    item.clipIndex = clipIndex;
    item.zOrder = zOrder;
    item.paintOrder = paintOrder;
    return item;
}

} // namespace ToolEngine::SpriteUi::Rendering
