#pragma once

#include "SpriteUi/Input/InputRouter.hpp"

#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace ToolEngine::SpriteUi::Input {

struct UIPointerFrameResult {
    bool valid = true;
    std::vector<UIEvent> events;
};

class UIMultiPointerEventRouter {
public:
    // Supply one snapshot per currently active pointer, all with the same frameId.
    // Omitted pointers are cancelled and removed; duplicate IDs reject the whole frame.
    [[nodiscard]] UIPointerFrameResult ProcessFrame(
        std::uint64_t frameId,
        std::span<const UIInputSnapshot> pointers,
        std::span<const HitTarget> targets);

    [[nodiscard]] const UIPointerRoutingState* GetState(
        std::uint32_t pointerId) const noexcept;
    [[nodiscard]] std::size_t PointerCount() const noexcept { return pointers_.size(); }
    void Reset() noexcept;

private:
    struct PointerState {
        UIEventRouter router;
        UIInputSnapshot lastInput{};
    };

    std::unordered_map<std::uint32_t, PointerState> pointers_;
    std::uint64_t lastFrameId_ = 0;
    bool hasProcessedFrame_ = false;
};

} // namespace ToolEngine::SpriteUi::Input
