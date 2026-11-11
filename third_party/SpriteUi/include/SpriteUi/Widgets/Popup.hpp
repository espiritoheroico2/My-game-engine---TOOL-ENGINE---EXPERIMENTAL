#pragma once

#include "SpriteUi/Core/Handle.hpp"
#include "SpriteUi/Core/Math.hpp"

#include <cstdint>

namespace ToolEngine::SpriteUi::Widgets {

enum class PopupInputResult : std::uint8_t {
    Unhandled,
    InsidePopup,
    ClosedDueToOutsideClick
};

// Global context menu and dropdown popup manager (Section 25.7).
// Enforces barrier hit-testing and automatic cascade collapse on outside clicks.
class UIPopupManager {
public:
    UIPopupManager() = default;

    void OpenPopup(UIHandle popupRoot, Rect bounds, bool blockBackground = true) noexcept;
    void ClosePopup() noexcept;

    [[nodiscard]] bool IsPopupOpen() const noexcept;
    [[nodiscard]] UIHandle GetActivePopup() const noexcept { return activePopupRoot_; }
    [[nodiscard]] Rect GetActiveBounds() const noexcept { return activePopupBounds_; }
    [[nodiscard]] bool BlocksBackground() const noexcept { return blockBackground_; }

    // Evaluates a pointer click against the active popup barrier.
    // Automatically closes the popup if the click is outside.
    [[nodiscard]] PopupInputResult HandlePointerDown(Vec2 pointerPosLogical) noexcept;

private:
    UIHandle activePopupRoot_ = InvalidUIHandle;
    Rect activePopupBounds_{};
    bool blockBackground_ = true;
};

} // namespace ToolEngine::SpriteUi::Widgets

