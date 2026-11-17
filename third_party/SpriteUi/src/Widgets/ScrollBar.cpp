#include "SpriteUi/Widgets/ScrollBar.hpp"

#include <algorithm>

namespace ToolEngine::SpriteUi::Widgets {

bool ScrollBarControl::HasOverflow(const UIScrollViewComponent& view) const noexcept {
    const bool allowed = axis == ScrollAxis::Vertical ? view.allowScrollY : view.allowScrollX;
    return allowed && Viewport(view) > 0.0f && Content(view) > Viewport(view);
}

Rect ScrollBarControl::ThumbRect(const UIScrollViewComponent& view) const noexcept {
    const float start = ThumbStart(view);
    const float length = ThumbLength(view);
    if (axis == ScrollAxis::Vertical) {
        return {{track_.position.x, track_.position.y + start}, {track_.size.x, length}};
    }
    return {{track_.position.x + start, track_.position.y}, {length, track_.size.y}};
}

bool ScrollBarControl::PointerDown(Vec2 pointer, UIScrollViewComponent& view) noexcept {
    if (disabled || !HasOverflow(view) || !IsFinite(pointer)) return false;

    const float along = AlongPointer(pointer);
    const float start = ThumbStart(view);
    const float length = ThumbLength(view);
    if (along >= start && along <= start + length) {
        dragging_ = true;
        grabOffset_ = along - start;
        return false;
    }
    // Trilho: uma pagina na direcao do clique.
    const float page = Viewport(view);
    return Scroll(along < start ? -page : page, view);
}

bool ScrollBarControl::PointerMove(Vec2 pointer, UIScrollViewComponent& view) noexcept {
    if (!dragging_ || disabled || !IsFinite(pointer)) return false;
    return ScrollToThumbStart(AlongPointer(pointer) - grabOffset_, view);
}

bool ScrollBarControl::Wheel(float delta, UIScrollViewComponent& view) noexcept {
    if (disabled || !HasOverflow(view) || !IsFinite(delta)) return false;
    return Scroll(delta, view);
}

float ScrollBarControl::AlongPointer(Vec2 pointer) const noexcept {
    return axis == ScrollAxis::Vertical ? pointer.y : pointer.x;
}

float ScrollBarControl::TrackStart() const noexcept {
    return axis == ScrollAxis::Vertical ? track_.position.y : track_.position.x;
}

float ScrollBarControl::TrackLength() const noexcept {
    return axis == ScrollAxis::Vertical ? track_.size.y : track_.size.x;
}

float ScrollBarControl::Viewport(const UIScrollViewComponent& view) const noexcept {
    return axis == ScrollAxis::Vertical ? view.viewportSize.y : view.viewportSize.x;
}

float ScrollBarControl::Content(const UIScrollViewComponent& view) const noexcept {
    return axis == ScrollAxis::Vertical ? view.contentSize.y : view.contentSize.x;
}

float ScrollBarControl::Offset(const UIScrollViewComponent& view) const noexcept {
    return axis == ScrollAxis::Vertical ? view.scrollOffset.y : view.scrollOffset.x;
}

float ScrollBarControl::ThumbLength(const UIScrollViewComponent& view) const noexcept {
    const float track = std::max(0.0f, TrackLength());
    if (!HasOverflow(view)) return track;
    const float proportional = track * Viewport(view) / Content(view);
    const float floor = std::max(kMinimumThumbLength, minThumbLength);
    return std::min(track, std::max(floor, proportional));
}

float ScrollBarControl::ThumbStart(const UIScrollViewComponent& view) const noexcept {
    const float room = std::max(0.0f, TrackLength() - ThumbLength(view));
    const float maxOffset = Content(view) - Viewport(view);
    if (!HasOverflow(view) || room <= 0.0f || maxOffset <= 0.0f) return 0.0f;
    return room * std::clamp(Offset(view) / maxOffset, 0.0f, 1.0f);
}

void ScrollBarControl::SetOffset(UIScrollViewComponent& view, float offset) const noexcept {
    if (axis == ScrollAxis::Vertical) {
        view.scrollOffset.y = offset;
    } else {
        view.scrollOffset.x = offset;
    }
    view.ClampScroll();
}

bool ScrollBarControl::Scroll(float delta, UIScrollViewComponent& view) const noexcept {
    const float before = Offset(view);
    SetOffset(view, before + delta);
    return Offset(view) != before;
}

bool ScrollBarControl::ScrollToThumbStart(float thumbStart, UIScrollViewComponent& view) const noexcept {
    const float room = std::max(0.0f, TrackLength() - ThumbLength(view));
    const float maxOffset = Content(view) - Viewport(view);
    if (room <= 0.0f || maxOffset <= 0.0f) return false;

    const float fraction = std::clamp((thumbStart - TrackStart()) / room, 0.0f, 1.0f);
    const float before = Offset(view);
    SetOffset(view, fraction * maxOffset);
    return Offset(view) != before;
}

} // namespace ToolEngine::SpriteUi::Widgets
