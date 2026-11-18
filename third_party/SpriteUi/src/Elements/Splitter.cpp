#include "SpriteUi/Elements/Splitter.hpp"

#include "SpriteUi/Rendering/Primitives.hpp"

#include <algorithm>

namespace ToolEngine::SpriteUi::Elements {

namespace {

float Finite(float value) noexcept {
    return IsFinite(value) ? value : 0.0f;
}

} // namespace

float Splitter::ClampedLeading(float wanted) const noexcept {
    if (!IsFinite(wanted) || !IsFinite(leadingSize)) return IsFinite(leadingSize) ? leadingSize : 0.0f;
    float lo = std::max(0.0f, Finite(minLeading));
    float hi = IsFinite(maxLeading) ? maxLeading : NoMaxSize;
    if (totalSize > 0.0f && IsFinite(totalSize)) {
        // O painel de tras precisa caber entre o seu minimo e o seu maximo.
        lo = std::max(lo, totalSize - (IsFinite(maxTrailing) ? maxTrailing : NoMaxSize));
        hi = std::min(hi, totalSize - std::max(0.0f, Finite(minTrailing)));
    }
    if (lo > hi) return leadingSize;   // limites incompativeis: o divisor fica onde esta
    return std::clamp(wanted, lo, hi);
}

SplitterEvents Splitter::OnPointer(const PointerEvent& event) noexcept {
    SplitterEvents events;
    if (states.disabled || !IsFinite(event.position)) {
        if (dragging_) {
            dragging_ = false;
            events.dragEnded = true;
        }
        return events;
    }
    switch (event.phase) {
    case PointerPhase::Enter:
        states.hovered = true;
        break;
    case PointerPhase::Leave:
        states.hovered = false;
        break;
    case PointerPhase::Down:
        if (!event.inside) break;
        dragging_ = true;
        grab_ = event.position;
        startLeading_ = leadingSize;
        events.dragStarted = true;
        break;
    case PointerPhase::Move:
        if (!dragging_) break;
        {
            const float delta = axis == SplitAxis::Vertical ? event.position.x - grab_.x : event.position.y - grab_.y;
            const float next = ClampedLeading(startLeading_ + delta);
            if (next != leadingSize) {
                leadingSize = next;
                events.resized = true;
            }
        }
        break;
    case PointerPhase::Up:
        if (dragging_) {
            dragging_ = false;
            events.dragEnded = true;
        }
        break;
    case PointerPhase::Cancel:
        if (dragging_) {
            dragging_ = false;
            if (leadingSize != startLeading_) {
                leadingSize = startLeading_;   // cancelar volta ao tamanho de antes do arraste
                events.resized = true;
            }
            events.dragEnded = true;
        }
        break;
    }
    events.leadingSize = leadingSize;
    return events;
}

Rect Splitter::LineRect(const SplitterStyle& style) const noexcept {
    const Rect box = border;
    const float t = std::clamp(Finite(style.lineThickness), 0.0f, std::max(0.0f, box.size.x));
    if (axis == SplitAxis::Vertical) {
        return {{box.position.x + 0.5f * (box.size.x - t), box.position.y}, {t, box.size.y}};
    }
    const float h = std::clamp(Finite(style.lineThickness), 0.0f, std::max(0.0f, box.size.y));
    return {{box.position.x, box.position.y + 0.5f * (box.size.y - h)}, {box.size.x, h}};
}

Rect Splitter::HitRect() const noexcept {
    const float hit = std::max(Finite(hitThickness), 0.0f);
    const Rect box = border;
    if (axis == SplitAxis::Vertical) {
        const float cx = box.position.x + 0.5f * box.size.x;
        const float w = std::max(hit, box.size.x);
        return {{cx - 0.5f * w, box.position.y}, {w, box.size.y}};
    }
    const float cy = box.position.y + 0.5f * box.size.y;
    const float h = std::max(hit, box.size.y);
    return {{box.position.x, cy - 0.5f * h}, {box.size.x, h}};
}

SplitterCursor Splitter::Cursor() const noexcept {
    if (!dragging_ && !states.hovered) return SplitterCursor::Arrow;
    return axis == SplitAxis::Vertical ? SplitterCursor::ResizeHorizontal : SplitterCursor::ResizeVertical;
}

std::vector<Rendering::UIRenderItem> BuildSplitterItems(const Splitter& splitter, const SplitterStyle& style,
                                                        float zOrder, std::uint64_t paintOrder) {
    std::vector<Rendering::UIRenderItem> out;
    const Rect line = splitter.LineRect(style);
    if (!IsFinite(line.position) || !IsFinite(line.size) || !(line.size.x > 0.0f) || !(line.size.y > 0.0f)) {
        return out;
    }
    const bool hot = splitter.Dragging() || splitter.states.hovered;
    const Vec4 c = hot ? style.lineHot : style.line;
    out.push_back(Rendering::MakeSolidRect(splitter.handle, line, c, Rendering::NoClipIndex, zOrder, paintOrder));
    return out;
}

} // namespace ToolEngine::SpriteUi::Elements
