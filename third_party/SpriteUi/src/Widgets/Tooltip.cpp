#include "SpriteUi/Widgets/Tooltip.hpp"

#include <algorithm>

namespace ToolEngine::SpriteUi::Widgets {

namespace {

// Prende um eixo no intervalo [start, end]. Uma dica maior que o intervalo fica encostada no inicio.
[[nodiscard]] float ClampInside(float value, float start, float end, float size) noexcept {
    const float last = std::max(start, end - size);
    return std::clamp(value, start, last);
}

} // namespace

Rect PlaceTooltip(Vec2 anchor, Vec2 size, Rect canvas, Vec2 offset) noexcept {
    if (!IsFinite(anchor) || !IsFinite(size) || !IsFinite(offset) || !IsFinite(canvas.position) ||
        !IsFinite(canvas.size)) {
        return {canvas.position, {}};
    }

    Vec2 position{anchor.x + offset.x, anchor.y + offset.y};
    if (position.y + size.y > canvas.Max().y) {
        position.y = anchor.y - offset.y - size.y; // Nao cabe embaixo: sobe.
    }
    position.x = ClampInside(position.x, canvas.position.x, canvas.Max().x, size.x);
    position.y = ClampInside(position.y, canvas.position.y, canvas.Max().y, size.y);
    return {position, size};
}

void TooltipControl::Enter(std::uint64_t target, Vec2 pointer, float now) noexcept {
    if (hasTarget_ && target_ == target) return;
    hasTarget_ = true;
    target_ = target;
    anchor_ = pointer;
    hoverStart_ = now;
}

void TooltipControl::Leave() noexcept {
    hasTarget_ = false;
}

bool TooltipControl::Visible(float now) const noexcept {
    return hasTarget_ && now - hoverStart_ >= config_.showDelay;
}

std::optional<float> TooltipControl::ShowAt() const noexcept {
    if (!hasTarget_) return std::nullopt;
    return hoverStart_ + std::max(0.0f, config_.showDelay);
}

bool TooltipControl::Tick(float now) noexcept {
    const bool visible = Visible(now);
    if (visible == shown_) return false;
    shown_ = visible;
    return true;
}

} // namespace ToolEngine::SpriteUi::Widgets
