#include "SpriteUi/Input/InputRouter.hpp"

#include <algorithm>
#include <cmath>

namespace ToolEngine::SpriteUi::Input {

const HitTarget* UIEventRouter::FindTarget(
    std::span<const HitTarget> targets, UIHandle handle) noexcept
{
    if (!handle.IsValid()) return nullptr;
    for (const HitTarget& target : targets) {
        if (target.handle == handle && target.visible && target.enabled &&
            IsFinite(target.localSize) && target.localSize.x > 0.0f &&
            target.localSize.y > 0.0f && target.localToScreen.IsFinite()) {
            return &target;
        }
    }
    return nullptr;
}

const HitTarget* UIEventRouter::HitTest(
    std::span<const HitTarget> targets, Vec2 screenPosition, Vec2* outLocal) noexcept
{
    const HitTarget* best = nullptr;
    Vec2 bestLocal{};
    DrawOrderKey bestKey{};

    if (!IsFinite(screenPosition)) return nullptr;

    for (const HitTarget& target : targets) {
        if (!target.visible || !target.handle.IsValid() ||
            !IsFinite(target.localSize) || target.localSize.x <= 0.0f ||
            target.localSize.y <= 0.0f || !target.localToScreen.IsFinite()) {
            continue;
        }
        if (target.clipEnabled && !target.clipRectLogical.Contains(screenPosition)) continue;
        const auto screenToLocal = target.localToScreen.Inverse();
        if (!screenToLocal) continue;

        const Vec2 local = screenToLocal->TransformPoint(screenPosition);
        if (local.x < 0.0f || local.y < 0.0f ||
            local.x >= target.localSize.x || local.y >= target.localSize.y) {
            continue;
        }

        const DrawOrderKey key{target.layer, target.zOrder, target.paintOrder, target.handle.Pack()};
        if (!best || DrawsAbove(key, bestKey)) {
            best = &target;
            bestKey = key;
            bestLocal = local;
        }
    }

    if (best && outLocal) *outLocal = bestLocal;
    return best;
}

void UIEventRouter::PushEvent(
    std::vector<UIEvent>& events,
    UIEventType type,
    const HitTarget& target,
    const UIInputSnapshot& input,
    Vec2 localPosition,
    Vec2 delta,
    bool inside)
{
    events.push_back(UIEvent{
        type, target.handle, input.pointerPositionLogical, localPosition,
        delta, input.pointerId, input.frameId, inside
    });
}

bool UIEventRouter::BlocksInput(
    const HitTarget& target, std::span<const HitTarget> targets) noexcept
{
    const HitTarget* current = &target;
    // A depth cap prevents malformed cyclic parent relationships from looping forever.
    for (std::size_t depth = 0; current && depth <= targets.size(); ++depth) {
        if (current->blocksInput) return true;
        if (!current->parent.IsValid()) break;
        current = FindTarget(targets, current->parent);
    }
    return false;
}

bool UIEventRouter::PushBubbledEvent(
    std::vector<UIEvent>& events,
    UIEventType type,
    const HitTarget& target,
    const UIInputSnapshot& input,
    Vec2 delta,
    std::span<const HitTarget> targets)
{
    const HitTarget* current = &target;
    bool consumed = false;
    for (std::size_t depth = 0; current && depth <= targets.size(); ++depth) {
        Vec2 local{};
        bool inside = false;
        if (const auto inverse = current->localToScreen.Inverse()) {
            local = inverse->TransformPoint(input.pointerPositionLogical);
            inside = IsFinite(local) && local.x >= 0.0f && local.y >= 0.0f &&
                     local.x < current->localSize.x && local.y < current->localSize.y;
            if (inside && current->clipEnabled)
                inside = current->clipRectLogical.Contains(input.pointerPositionLogical);
        }
        PushEvent(events, type, *current, input, local, delta, inside);
        consumed = consumed || current->blocksInput;
        if (!current->parent.IsValid()) break;
        current = FindTarget(targets, current->parent);
    }
    return consumed;
}

std::vector<UIEvent> UIEventRouter::Process(
    const UIInputSnapshot& input,
    std::span<const HitTarget> targets)
{
    std::vector<UIEvent> events;

    if (!IsFinite(input.pointerPositionLogical) ||
        !IsFinite(input.pointerDeltaLogical) || !IsFinite(input.wheelDelta)) {
        UIInputSnapshot cancelled = input;
        cancelled.pointerPositionLogical = {};
        cancelled.pointerDeltaLogical = {};
        cancelled.wheelDelta = {};
        cancelled.primaryDown = false;
        cancelled.primaryPressed = false;
        cancelled.primaryReleased = false;
        cancelled.pointerCancelled = true;
        cancelled.windowFocused = false;
        return Process(cancelled, targets);
    }

    if (hasProcessedFrame_ && input.frameId == lastFrameId_) return events;
    lastFrameId_ = input.frameId;
    hasProcessedFrame_ = true;
    state_.consumedThisFrame = false;

    const HitTarget* oldHover = FindTarget(targets, state_.hoveredEntity);
    const HitTarget* capture = FindTarget(targets, state_.capturedEntity);
    const HitTarget* oldFocus = FindTarget(targets, state_.focusedEntity);

    // If a target was destroyed while captured/focused, do not keep a stale handle.
    if (state_.capturedEntity.IsValid() && !capture) {
        events.push_back(UIEvent{UIEventType::Cancel, state_.capturedEntity,
            input.pointerPositionLogical, {}, {}, input.pointerId, input.frameId, false});
        state_.capturedEntity = InvalidUIHandle;
    }
    if (state_.focusedEntity.IsValid() && !oldFocus) {
        events.push_back(UIEvent{UIEventType::FocusLost, state_.focusedEntity,
            input.pointerPositionLogical, {}, {}, input.pointerId, input.frameId, false});
        state_.focusedEntity = InvalidUIHandle;
    }

    if (!input.windowFocused) {
        if (capture) {
            PushEvent(events, UIEventType::Cancel, *capture, input, {});
        }
        if (oldHover) {
            PushEvent(events, UIEventType::PointerLeave, *oldHover, input, {});
        }
        if (oldFocus) {
            PushEvent(events, UIEventType::FocusLost, *oldFocus, input, {});
        }
        state_ = {};
        state_.capturedEntity = InvalidUIHandle;
        state_.focusedEntity = InvalidUIHandle;
        state_.hoveredEntity = InvalidUIHandle;
        return events;
    }

    if (input.pointerCancelled) {
        if (capture) PushEvent(events, UIEventType::Cancel, *capture, input, {});
        if (oldHover) PushEvent(events, UIEventType::PointerLeave, *oldHover, input, {});
        if (oldFocus) PushEvent(events, UIEventType::FocusLost, *oldFocus, input, {});
        state_ = {};
        state_.capturedEntity = InvalidUIHandle;
        state_.focusedEntity = InvalidUIHandle;
        state_.hoveredEntity = InvalidUIHandle;
        return events;
    }

    Vec2 hoveredLocal{};
    const HitTarget* topmost = HitTest(targets, input.pointerPositionLogical, &hoveredLocal);
    // A disabled target covers the pointer but neither receives nor passes events on.
    const bool coveredByDisabled = topmost && !topmost->enabled;
    const HitTarget* hovered = coveredByDisabled ? nullptr : topmost;
    const UIHandle newHovered = hovered ? hovered->handle : InvalidUIHandle;

    if (newHovered != state_.hoveredEntity) {
        if (oldHover) PushEvent(events, UIEventType::PointerLeave, *oldHover, input, {});
        if (hovered) PushEvent(events, UIEventType::PointerEnter, *hovered, input, hoveredLocal, {}, true);
        state_.hoveredEntity = newHovered;
    }
    state_.consumedThisFrame = coveredByDisabled || (hovered && BlocksInput(*hovered, targets));

    if (input.primaryPressed) {
        if (hovered) {
            state_.capturedEntity = hovered->handle;
            pressTimeSeconds_ = input.frameTimeSeconds;
            if (hovered->focusable && state_.focusedEntity != hovered->handle) {
                if (oldFocus) PushEvent(events, UIEventType::FocusLost, *oldFocus, input, {});
                state_.focusedEntity = hovered->handle;
                PushEvent(events, UIEventType::FocusGained, *hovered, input, hoveredLocal);
            }
            state_.consumedThisFrame = PushBubbledEvent(
                events, UIEventType::PointerDown, *hovered, input, {}, targets) ||
                state_.consumedThisFrame;
        } else if (oldFocus) {
            PushEvent(events, UIEventType::FocusLost, *oldFocus, input, {});
            state_.focusedEntity = InvalidUIHandle;
        }
    }

    capture = FindTarget(targets, state_.capturedEntity);
    const bool pointerMoved = input.pointerDeltaLogical.x != 0.0f ||
                              input.pointerDeltaLogical.y != 0.0f;
    if (capture && input.primaryDown && pointerMoved) {
        state_.consumedThisFrame = PushBubbledEvent(
            events, UIEventType::PointerMove, *capture, input,
            input.pointerDeltaLogical, targets) || state_.consumedThisFrame;
    } else if (!capture && hovered && pointerMoved) {
        state_.consumedThisFrame = PushBubbledEvent(
            events, UIEventType::PointerMove, *hovered, input,
            input.pointerDeltaLogical, targets) || state_.consumedThisFrame;
    }

    if (input.primaryReleased && capture) {
        bool inside = false;
        if (const auto inverse = capture->localToScreen.Inverse()) {
            const Vec2 local = inverse->TransformPoint(input.pointerPositionLogical);
            inside = IsFinite(local) && local.x >= 0.0f && local.y >= 0.0f &&
                     local.x < capture->localSize.x && local.y < capture->localSize.y;
        }

        state_.consumedThisFrame = PushBubbledEvent(
            events, UIEventType::PointerUp, *capture, input, {}, targets) ||
            state_.consumedThisFrame;
        if (inside) {
            state_.consumedThisFrame = PushBubbledEvent(
                events, UIEventType::Click, *capture, input, {}, targets) ||
                state_.consumedThisFrame;
        }
        state_.capturedEntity = InvalidUIHandle;
    }

    const bool pressed = state_.capturedEntity.IsValid() && input.primaryDown;
    state_.holdActive = pressed && std::isfinite(input.frameTimeSeconds) &&
                        input.frameTimeSeconds - pressTimeSeconds_ >= kHoldDurationSeconds;

    if ((input.wheelDelta.x != 0.0f || input.wheelDelta.y != 0.0f) && hovered) {
        state_.consumedThisFrame = PushBubbledEvent(
            events, UIEventType::Wheel, *hovered, input, input.wheelDelta, targets) ||
            state_.consumedThisFrame;
    }

    return events;
}

void UIEventRouter::Reset() noexcept {
    pressTimeSeconds_ = 0.0;
    state_.holdActive = false;
    state_.capturedEntity = InvalidUIHandle;
    state_.focusedEntity = InvalidUIHandle;
    state_.hoveredEntity = InvalidUIHandle;
    state_.consumedThisFrame = false;
    lastFrameId_ = 0;
    hasProcessedFrame_ = false;
}

} // namespace ToolEngine::SpriteUi::Input
