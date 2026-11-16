#pragma once

#include "SpriteUi/Rendering/Icon.hpp"

#include <array>
#include <optional>

namespace ToolEngine::SpriteUi::Rendering {

struct UISprite {
    std::uint32_t atlasId = WhiteTextureSlice;
    std::array<float, 4> uvRect{0.0f, 0.0f, 1.0f, 1.0f};
    Vec2 sizePixels{};
};

[[nodiscard]] inline std::optional<UIRenderItem> MakeSpriteItem(
    UIHandle owner,
    const UISprite& sprite,
    Vec2 center,
    float rotationRadians = 0.0f,
    std::uint32_t clipIndex = NoClipIndex,
    float zOrder = 0.0f,
    std::uint64_t paintOrder = 0) noexcept
{
    const UIIcon icon{sprite.atlasId, sprite.uvRect, sprite.sizePixels};
    const Rect bounds{center - sprite.sizePixels * 0.5f, sprite.sizePixels};
    auto item = MakeIconRenderItem(icon, bounds, Vec4{1.0f, 1.0f, 1.0f, 1.0f}, owner, clipIndex, zOrder, paintOrder);
    if (!item || !IsFinite(rotationRadians)) return std::nullopt;
    item->transform.rotationRadians = rotationRadians;
    return item;
}

} // namespace ToolEngine::SpriteUi::Rendering
