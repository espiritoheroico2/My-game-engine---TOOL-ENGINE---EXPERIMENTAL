#include "SpriteUi/Input/MultiPointerRouter.hpp"

#include <algorithm>
#include <unordered_set>

namespace ToolEngine::SpriteUi::Input {

UIPointerFrameResult UIMultiPointerEventRouter::ProcessFrame(
    std::uint64_t frameId,
    std::span<const UIInputSnapshot> pointers,
    std::span<const HitTarget> targets)
{
    UIPointerFrameResult result;
    std::unordered_set<std::uint32_t> seen;
    seen.reserve(pointers.size());
    for (const UIInputSnapshot& input : pointers) {
        if (input.frameId != frameId || !seen.insert(input.pointerId).second) {
            result.valid = false;
            return result;
        }
    }

    if (hasProcessedFrame_ && frameId == lastFrameId_) {
        result.valid = false;
        return result;
    }
    hasProcessedFrame_ = true;
    lastFrameId_ = frameId;

    for (const UIInputSnapshot& input : pointers) {
        PointerState& state = pointers_[input.pointerId];
        state.lastInput = input;
        std::vector<UIEvent> pointerEvents = state.router.Process(input, targets);
        result.events.insert(result.events.end(), pointerEvents.begin(), pointerEvents.end());
    }

    std::vector<std::uint32_t> expired;
    expired.reserve(pointers_.size());
    for (const auto& [pointerId, state] : pointers_) {
        (void)state;
        if (!seen.contains(pointerId)) expired.push_back(pointerId);
    }
    std::sort(expired.begin(), expired.end());
    for (const std::uint32_t pointerId : expired) {
        auto it = pointers_.find(pointerId);
        if (it == pointers_.end()) continue;
        UIInputSnapshot cancelled = it->second.lastInput;
        cancelled.frameId = frameId;
        cancelled.primaryDown = false;
        cancelled.primaryPressed = false;
        cancelled.primaryReleased = false;
        cancelled.pointerDeltaLogical = {};
        cancelled.wheelDelta = {};
        cancelled.windowFocused = false;
        std::vector<UIEvent> pointerEvents = it->second.router.Process(cancelled, targets);
        result.events.insert(result.events.end(), pointerEvents.begin(), pointerEvents.end());
        pointers_.erase(it);
    }
    return result;
}

const UIPointerRoutingState* UIMultiPointerEventRouter::GetState(
    std::uint32_t pointerId) const noexcept {
    const auto it = pointers_.find(pointerId);
    return it == pointers_.end() ? nullptr : &it->second.router.GetState();
}

void UIMultiPointerEventRouter::Reset() noexcept {
    pointers_.clear();
    lastFrameId_ = 0;
    hasProcessedFrame_ = false;
}

} // namespace ToolEngine::SpriteUi::Input
