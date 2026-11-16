#pragma once

#include "SpriteUi/Rendering/Icon.hpp"

#include <algorithm>
#include <vector>

namespace ToolEngine::SpriteUi::Elements {

enum class ImageFit {
    Stretch,
    Contain,
    Cover
};

[[nodiscard]] inline Rect ImageDrawRect(Rect content, Vec2 sourceSize, ImageFit fit) noexcept {
    if (!IsFinite(content.position) || !IsFinite(content.size) || !IsFinite(sourceSize) ||
        content.size.x <= 0.0f || content.size.y <= 0.0f || sourceSize.x <= 0.0f || sourceSize.y <= 0.0f ||
        fit != ImageFit::Contain) return content;

    const float sourceAspect = sourceSize.x / sourceSize.y;
    const float contentAspect = content.size.x / content.size.y;
    if (sourceAspect > contentAspect) {
        const float height = content.size.x / sourceAspect;
        content.position.y += (content.size.y - height) * 0.5f;
        content.size.y = height;
    } else {
        const float width = content.size.y * sourceAspect;
        content.position.x += (content.size.x - width) * 0.5f;
        content.size.x = width;
    }
    return content;
}

struct ImageSource {
    std::uint32_t textureSlice = Rendering::WhiteTextureSlice;
    Vec2 textureSize{};
    bool loaded = false;
    ImageFit fit = ImageFit::Stretch;
};

struct ImageStyle {
    Vec4 tint{1.0f, 1.0f, 1.0f, 1.0f};
};

[[nodiscard]] inline std::vector<Rendering::UIRenderItem> BuildImageSourceItems(
    UIHandle owner,
    Rect bounds,
    const ImageSource& source,
    Vec4 tint,
    const ImageStyle& style,
    std::uint32_t clipIndex,
    float zOrder,
    std::uint64_t paintOrder)
{
    if (!source.loaded || !IsFinite(source.textureSize) || source.textureSize.x <= 0.0f ||
        source.textureSize.y <= 0.0f || !IsFinite(bounds.position) || !IsFinite(bounds.size) ||
        bounds.size.x <= 0.0f || bounds.size.y <= 0.0f) return {};

    Rendering::UIIcon icon;
    icon.textureSlice = source.textureSlice;
    icon.sourceSize = source.textureSize;
    Rect imageBounds = ImageDrawRect(bounds, source.textureSize, source.fit);
    const float sourceAspect = source.textureSize.x / source.textureSize.y;
    const float targetAspect = bounds.size.x / bounds.size.y;
    if (source.fit == ImageFit::Cover) {
        if (sourceAspect > targetAspect) {
            const float width = targetAspect / sourceAspect;
            icon.uvRect = {(1.0f - width) * 0.5f, 0.0f, width, 1.0f};
        } else {
            const float height = sourceAspect / targetAspect;
            icon.uvRect = {0.0f, (1.0f - height) * 0.5f, 1.0f, height};
        }
    }

    const Vec4 finalTint{tint.x * style.tint.x, tint.y * style.tint.y, tint.z * style.tint.z, tint.w * style.tint.w};
    auto item = Rendering::MakeIconRenderItem(icon, imageBounds, finalTint, owner, clipIndex, zOrder, paintOrder);
    return item ? std::vector<Rendering::UIRenderItem>{*item} : std::vector<Rendering::UIRenderItem>{};
}

} // namespace ToolEngine::SpriteUi::Elements
