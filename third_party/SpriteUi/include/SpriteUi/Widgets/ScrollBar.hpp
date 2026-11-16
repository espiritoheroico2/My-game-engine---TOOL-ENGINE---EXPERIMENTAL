#pragma once

#include "SpriteUi/Widgets/Widgets.hpp"

#include <algorithm>
#include <cmath>

namespace ToolEngine::SpriteUi::Widgets {

class ScrollBarControl {
public:
    void SetTrack(Rect track) noexcept { track_ = track; }
    [[nodiscard]] Rect Track() const noexcept { return track_; }
    [[nodiscard]] bool HasOverflow(const UIScrollViewComponent& view) const noexcept {
        return view.contentSize.y > view.viewportSize.y + 0.5f;
    }

    [[nodiscard]] Rect ThumbRect(const UIScrollViewComponent& view) const noexcept {
        if (track_.size.y <= 0.0f || view.contentSize.y <= 0.0f) return {};
        const float thumbHeight = std::min(track_.size.y, std::max(24.0f, track_.size.y * view.viewportSize.y / view.contentSize.y));
        const float maxScroll = std::max(0.0f, view.contentSize.y - view.viewportSize.y);
        const float travel = track_.size.y - thumbHeight;
        const float t = maxScroll > 0.0f ? Clamp01(view.scrollOffset.y / maxScroll) : 0.0f;
        return {{track_.position.x, track_.position.y + travel * t}, {track_.size.x, thumbHeight}};
    }

    [[nodiscard]] bool Wheel(float delta, UIScrollViewComponent& view) const noexcept {
        if (!IsFinite(delta)) return false;
        const float before = view.scrollOffset.y;
        view.ScrollBy({0.0f, delta});
        return view.scrollOffset.y != before;
    }

    [[nodiscard]] bool PointerDown(Vec2 point, UIScrollViewComponent& view) noexcept {
        const Rect thumb = ThumbRect(view);
        if (!track_.Contains(point) || !HasOverflow(view)) return false;
        dragging_ = true;
        grabOffset_ = thumb.Contains(point) ? point.y - thumb.position.y : thumb.size.y * 0.5f;
        if (!thumb.Contains(point)) update(point.y, view);
        return true;
    }

    [[nodiscard]] bool PointerMove(Vec2 point, UIScrollViewComponent& view) noexcept {
        if (!dragging_ || !IsFinite(point)) return false;
        update(point.y, view);
        return true;
    }

    void PointerUp() noexcept { dragging_ = false; }
    [[nodiscard]] bool Dragging() const noexcept { return dragging_; }

private:
    void update(float pointerY, UIScrollViewComponent& view) noexcept {
        const float thumbHeight = ThumbRect(view).size.y;
        const float travel = std::max(0.0f, track_.size.y - thumbHeight);
        const float maxScroll = std::max(0.0f, view.contentSize.y - view.viewportSize.y);
        const float ratio = travel > 0.0f ? Clamp01((pointerY - track_.position.y - grabOffset_) / travel) : 0.0f;
        view.scrollOffset.y = ratio * maxScroll;
        view.ClampScroll();
    }

    Rect track_{};
    float grabOffset_ = 0.0f;
    bool dragging_ = false;
};

} // namespace ToolEngine::SpriteUi::Widgets
