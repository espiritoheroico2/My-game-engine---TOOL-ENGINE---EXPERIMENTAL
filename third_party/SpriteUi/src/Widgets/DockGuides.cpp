#include "SpriteUi/Widgets/DockGuides.hpp"

#include "SpriteUi/Core/DrawOrder.hpp"
#include "SpriteUi/Rendering/Primitives.hpp"

#include <algorithm>

namespace ToolEngine::SpriteUi::Widgets {

namespace {

bool UsableStyle(const DockGuideStyle& s) noexcept {
    return IsFinite(s.iconSize) && s.iconSize > 0.0f && IsFinite(s.edgeInset) && s.edgeInset >= 0.0f &&
           IsFinite(s.idleAlpha) && IsFinite(s.highlightAlpha);
}

float ClampAlpha(float a) noexcept {
    return std::clamp(a, 0.0f, 1.0f);
}

// Cor premultiplicada (RGB * alfa) e alfa, como os demais itens do core.
Vec4 Premultiplied(Vec4 rgb, float alpha) noexcept {
    return {rgb.x * alpha, rgb.y * alpha, rgb.z * alpha, alpha};
}

Rect GuideRect(Rect leaf, DockSlot slot, const DockGuideStyle& s) noexcept {
    const float size = s.iconSize;
    const float cx = leaf.position.x + 0.5f * leaf.size.x - 0.5f * size;
    const float cy = leaf.position.y + 0.5f * leaf.size.y - 0.5f * size;
    switch (slot) {
    case DockSlot::Left: return {{leaf.position.x + s.edgeInset, cy}, {size, size}};
    case DockSlot::Right: return {{leaf.position.x + leaf.size.x - s.edgeInset - size, cy}, {size, size}};
    case DockSlot::Top: return {{cx, leaf.position.y + s.edgeInset}, {size, size}};
    case DockSlot::Bottom: return {{cx, leaf.position.y + leaf.size.y - s.edgeInset - size}, {size, size}};
    case DockSlot::Center:
    case DockSlot::None:
        break;
    }
    return {{cx, cy}, {size, size}};
}

} // namespace

std::vector<Rendering::UIRenderItem> BuildDockGuideItems(UIHandle owner, Rect leaf, DockSlot highlighted,
                                                         const DockGuideStyle& style, std::uint64_t paintOrder) {
    std::vector<Rendering::UIRenderItem> out;
    if (!owner.IsValid() || !IsFinite(leaf.position) || !IsFinite(leaf.size) || !(leaf.size.x > 0.0f) ||
        !(leaf.size.y > 0.0f) || !UsableStyle(style)) {
        return out;
    }

    constexpr DockSlot kOrder[] = {DockSlot::Left, DockSlot::Right, DockSlot::Top, DockSlot::Bottom, DockSlot::Center};
    const auto layer = static_cast<std::int32_t>(UILayer::DragPreview);
    std::uint64_t paint = paintOrder;
    for (const DockSlot slot : kOrder) {
        const bool hot = slot == highlighted;
        const float alpha = ClampAlpha(hot ? style.highlightAlpha : style.idleAlpha);
        const Vec4 rgb = hot ? style.highlightColor : style.color;
        Rendering::UIRenderItem item = Rendering::MakeSolidRect(owner, GuideRect(leaf, slot, style),
                                                                Premultiplied(rgb, alpha), Rendering::NoClipIndex,
                                                                0.0f, paint++);
        item.layer = layer;
        out.push_back(item);
    }
    return out;
}

} // namespace ToolEngine::SpriteUi::Widgets
