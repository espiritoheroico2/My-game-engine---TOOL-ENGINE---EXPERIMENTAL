#include "SpriteUi/Widgets/DragDrop.hpp"

#include <algorithm>
#include <utility>

namespace ToolEngine::SpriteUi::Widgets {

bool DragDropControl::PointerDown(UIHandle source, Vec2 pointer, DragPayload payload) {
    if (phase_ != DragPhase::Idle || !source.IsValid() || !IsFinite(pointer)) return false;
    phase_ = DragPhase::Pending;
    source_ = source;
    payload_ = std::move(payload);
    pressPoint_ = pointer;
    pointer_ = pointer;
    return true;
}

bool DragDropControl::PointerMove(Vec2 pointer, std::span<const DropTarget> targets) {
    if (phase_ == DragPhase::Idle || !IsFinite(pointer)) return false;
    pointer_ = pointer;

    if (phase_ == DragPhase::Pending) {
        if (!PastThreshold(pointer)) return false;
        phase_ = DragPhase::Dragging;
        hovered_ = FindTarget(pointer, targets);
        return true;
    }

    const std::optional<UIHandle> next = FindTarget(pointer, targets);
    const bool changed = next != hovered_;
    hovered_ = next;
    return changed;
}

std::optional<DropResult> DragDropControl::PointerUp(Vec2 pointer, std::span<const DropTarget> targets) {
    if (phase_ == DragPhase::Idle) return std::nullopt;

    std::optional<DropResult> result;
    if (phase_ == DragPhase::Dragging) {
        const Vec2 at = IsFinite(pointer) ? pointer : pointer_;
        if (const std::optional<UIHandle> target = FindTarget(at, targets)) {
            result = DropResult{source_, *target, payload_};
        }
    }
    Reset();
    return result;
}

bool DragDropControl::Cancel() noexcept {
    if (phase_ == DragPhase::Idle) return false;
    Reset();
    return true;
}

bool DragDropControl::Accepts(const DropTarget& target, const DragPayload& payload) noexcept {
    return target.acceptedType.empty() || target.acceptedType == payload.type;
}

std::optional<UIHandle> DragDropControl::FindTarget(Vec2 pointer,
                                                    std::span<const DropTarget> targets) const noexcept {
    // Do fim para o inicio: o ultimo alvo da lista e o que esta por cima.
    for (auto it = targets.rbegin(); it != targets.rend(); ++it) {
        if (it->handle.IsValid() && it->bounds.Contains(pointer) && Accepts(*it, payload_)) {
            return it->handle;
        }
    }
    return std::nullopt;
}

bool DragDropControl::PastThreshold(Vec2 pointer) const noexcept {
    const float dx = pointer.x - pressPoint_.x;
    const float dy = pointer.y - pressPoint_.y;
    const float limit = std::max(0.0f, threshold);
    return dx * dx + dy * dy >= limit * limit;
}

void DragDropControl::Reset() noexcept {
    phase_ = DragPhase::Idle;
    source_ = {};
    payload_ = {};
    hovered_.reset();
}

} // namespace ToolEngine::SpriteUi::Widgets
