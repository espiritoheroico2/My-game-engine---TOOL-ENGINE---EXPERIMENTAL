#include "SpriteUi/Widgets/DockGuides.hpp"

#include "SpriteUi/Core/DrawOrder.hpp"
#include "SpriteUi/Rendering/Primitives.hpp"

#include <algorithm>

namespace ToolEngine::SpriteUi::Widgets {

namespace {

bool IsNormalizedColor(Vec4 color) noexcept {
    return IsFinite(color) && color.x >= 0.0f && color.x <= 1.0f && color.y >= 0.0f && color.y <= 1.0f &&
           color.z >= 0.0f && color.z <= 1.0f && color.w >= 0.0f && color.w <= 1.0f;
}

bool UsableStyle(const DockGuideStyle& s) noexcept {
    return IsFinite(s.iconSize) && s.iconSize > 0.0f && IsFinite(s.edgeInset) && s.edgeInset >= 0.0f &&
           IsFinite(s.idleAlpha) && IsFinite(s.highlightAlpha) && IsNormalizedColor(s.color) &&
           IsNormalizedColor(s.highlightColor);
}

float ClampAlpha(float a) noexcept {
    return std::clamp(a, 0.0f, 1.0f);
}

// Cor premultiplicada (RGB * alfa) e alfa, como os demais itens do core.
Vec4 Premultiplied(Vec4 rgb, float alpha) noexcept {
    return {rgb.x * alpha, rgb.y * alpha, rgb.z * alpha, alpha};
}

Rect GuideRect(Rect leaf, DockSlot slot, float size, float insetX, float insetY) noexcept {
    const float cx = leaf.position.x + 0.5f * leaf.size.x - 0.5f * size;
    const float cy = leaf.position.y + 0.5f * leaf.size.y - 0.5f * size;
    switch (slot) {
    case DockSlot::Left: return {{leaf.position.x + insetX, cy}, {size, size}};
    case DockSlot::Right: return {{leaf.position.x + leaf.size.x - insetX - size, cy}, {size, size}};
    case DockSlot::Top: return {{cx, leaf.position.y + insetY}, {size, size}};
    case DockSlot::Bottom: return {{cx, leaf.position.y + leaf.size.y - insetY - size}, {size, size}};
    case DockSlot::Center:
    case DockSlot::None:
        break;
    }
    return {{cx, cy}, {size, size}};
}

void AppendIcon(std::vector<Rendering::UIRenderItem>& out, UIHandle owner, Rect icon, DockSlot slot,
                float alpha, Vec4 baseColor, std::uint64_t& paint) {
    const Rect inner{{icon.position.x + icon.size.x * 0.22f, icon.position.y + icon.size.y * 0.22f},
                     {icon.size.x * 0.56f, icon.size.y * 0.56f}};
    const Vec4 dark = Premultiplied({0.06f, 0.08f, 0.12f, 1.0f}, std::min(1.0f, alpha * 1.8f));
    const Vec4 base = Premultiplied(baseColor, alpha);
    auto add = [&](Rect rect, Vec4 color) {
        Rendering::UIRenderItem item =
            Rendering::MakeSolidRect(owner, rect, color, Rendering::NoClipIndex, 0.0f, paint++);
        item.layer = static_cast<std::int32_t>(UILayer::DragPreview);
        out.push_back(item);
    };

    add(icon, base);
    add(inner, dark);

    Rect active = inner;
    switch (slot) {
    case DockSlot::Left:
        active.size.x *= 0.32f;
        break;
    case DockSlot::Right:
        active.size.x *= 0.32f;
        active.position.x = inner.position.x + inner.size.x - active.size.x;
        break;
    case DockSlot::Top:
        active.size.y *= 0.32f;
        break;
    case DockSlot::Bottom:
        active.size.y *= 0.32f;
        active.position.y = inner.position.y + inner.size.y - active.size.y;
        break;
    case DockSlot::Center:
        active.size = {inner.size.x * 0.5f, inner.size.y * 0.5f};
        active.position = {inner.position.x + (inner.size.x - active.size.x) * 0.5f,
                           inner.position.y + (inner.size.y - active.size.y) * 0.5f};
        break;
    case DockSlot::None:
        return;
    }
    add(active, base);
}

} // namespace

std::vector<Rendering::UIRenderItem> BuildDockGuideItems(UIHandle owner, Rect leaf, DockSlot highlighted,
                                                         const DockGuideStyle& style, std::uint64_t paintOrder) {
    std::vector<Rendering::UIRenderItem> out;
    if (!owner.IsValid() || !IsFinite(leaf.position) || !IsFinite(leaf.size) || !(leaf.size.x > 0.0f) ||
        !(leaf.size.y > 0.0f) || !IsFinite(leaf.Max()) || !UsableStyle(style)) {
        return out;
    }

    const float size = std::min(style.iconSize, std::min(leaf.size.x, leaf.size.y));
    const float insetX = std::min(style.edgeInset, std::max(0.0f, (leaf.size.x - size) * 0.5f));
    const float insetY = std::min(style.edgeInset, std::max(0.0f, (leaf.size.y - size) * 0.5f));
    constexpr DockSlot kOrder[] = {DockSlot::Left, DockSlot::Right, DockSlot::Top, DockSlot::Bottom, DockSlot::Center};
    std::uint64_t paint = paintOrder;
    for (const DockSlot slot : kOrder) {
        const bool hot = slot == highlighted;
        const float alpha = ClampAlpha(hot ? style.highlightAlpha : style.idleAlpha);
        const Vec4 rgb = hot ? style.highlightColor : style.color;
        AppendIcon(out, owner, GuideRect(leaf, slot, size, insetX, insetY), slot, alpha, rgb, paint);
    }
    return out;
}

} // namespace ToolEngine::SpriteUi::Widgets
