#include "SpriteUi/Elements/Element.hpp"

#include <algorithm>

namespace ToolEngine::SpriteUi::Elements {

namespace {

float FiniteOrZero(float value) noexcept {
    return IsFinite(value) ? value : 0.0f;
}

float ClampAxis(float value, float minimum, float maximum) noexcept {
    if (!IsFinite(value)) return 0.0f;
    const float low = FiniteOrZero(minimum);
    const float high = IsFinite(maximum) ? maximum : NoMaxSize;
    return std::max(0.0f, std::min(std::max(value, low), high));
}

} // namespace

Rect ShrinkRect(Rect rect, Insets insets) noexcept {
    if (!IsFinite(rect.position) || !IsFinite(rect.size)) return {};
    const float left = std::max(0.0f, FiniteOrZero(insets.left));
    const float right = std::max(0.0f, FiniteOrZero(insets.right));
    const float top = std::max(0.0f, FiniteOrZero(insets.top));
    const float bottom = std::max(0.0f, FiniteOrZero(insets.bottom));
    const float width = std::max(0.0f, std::max(0.0f, rect.size.x) - left - right);
    const float height = std::max(0.0f, std::max(0.0f, rect.size.y) - top - bottom);
    return {{rect.position.x + left, rect.position.y + top}, {width, height}};
}

Vec2 ClampSize(Vec2 size, Vec2 minSize, Vec2 maxSize) noexcept {
    return {ClampAxis(size.x, minSize.x, maxSize.x), ClampAxis(size.y, minSize.y, maxSize.y)};
}

Rect Element::ArrangeIn(Rect parentContent) const noexcept {
    const Rect area = ShrinkRect(parentContent, margin);
    const std::optional<Rect> resolved = ResolveAnchoredRect(area, transform);
    if (!resolved) return {};
    return {resolved->position, ClampSize(resolved->size, minSize, maxSize)};
}

} // namespace ToolEngine::SpriteUi::Elements
