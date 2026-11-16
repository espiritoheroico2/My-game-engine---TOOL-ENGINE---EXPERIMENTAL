#pragma once

#include "SpriteUi/Core/Handle.hpp"
#include "SpriteUi/Core/Math.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ToolEngine::SpriteUi::Widgets {

enum class DragPhase {
    Idle,
    Pressed,
    Dragging
};

struct DragPayload {
    std::string type;
    std::string data;
};

struct DropTarget {
    UIHandle handle{};
    Rect bounds{};
    std::string acceptedType;
};

struct DropResult {
    UIHandle target{};
    DragPayload payload;
};

class DragDropControl {
public:
    explicit DragDropControl(float dragThreshold = 6.0f) noexcept
        : dragThreshold_{IsFinite(dragThreshold) ? std::max(0.0f, dragThreshold) : 6.0f} {}

    void PointerDown(UIHandle source, Vec2 point, DragPayload payload) {
        if (!source.IsValid() || !IsFinite(point)) return;
        source_ = source;
        pressPoint_ = point;
        payload_ = std::move(payload);
        phase_ = DragPhase::Pressed;
        hovered_.reset();
    }

    [[nodiscard]] DragPhase Phase() const noexcept { return phase_; }
    [[nodiscard]] const UIHandle* HoveredTarget() const noexcept {
        return hovered_ ? &*hovered_ : nullptr;
    }

    bool PointerMove(Vec2 point, const std::vector<DropTarget>& targets) {
        if ((phase_ != DragPhase::Pressed && phase_ != DragPhase::Dragging) || !IsFinite(point)) return false;
        const float dx = point.x - pressPoint_.x;
        const float dy = point.y - pressPoint_.y;
        if (phase_ == DragPhase::Pressed && dx * dx + dy * dy < dragThreshold_ * dragThreshold_) return false;
        phase_ = DragPhase::Dragging;
        hovered_.reset();
        for (auto target = targets.rbegin(); target != targets.rend(); ++target) {
            if (target->handle.IsValid() && target->acceptedType == payload_.type && target->bounds.Contains(point)) {
                hovered_ = target->handle;
                break;
            }
        }
        return true;
    }

    [[nodiscard]] std::optional<DropResult> PointerUp(Vec2 point, const std::vector<DropTarget>& targets) {
        if (phase_ == DragPhase::Pressed) {
            Reset();
            return std::nullopt;
        }
        if (phase_ != DragPhase::Dragging) return std::nullopt;
        (void)PointerMove(point, targets);
        std::optional<DropResult> result;
        if (hovered_) result = DropResult{*hovered_, payload_};
        Reset();
        return result;
    }

    void Cancel() noexcept { Reset(); }

private:
    void Reset() noexcept {
        source_ = InvalidUIHandle;
        phase_ = DragPhase::Idle;
        hovered_.reset();
        payload_ = {};
    }

    float dragThreshold_ = 6.0f;
    UIHandle source_{};
    Vec2 pressPoint_{};
    DragPhase phase_ = DragPhase::Idle;
    DragPayload payload_{};
    std::optional<UIHandle> hovered_;
};

} // namespace ToolEngine::SpriteUi::Widgets
