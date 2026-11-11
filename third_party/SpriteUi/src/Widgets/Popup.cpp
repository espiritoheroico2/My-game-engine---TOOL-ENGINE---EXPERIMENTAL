#include "SpriteUi/Widgets/Popup.hpp"

namespace ToolEngine::SpriteUi::Widgets {

void UIPopupManager::OpenPopup(UIHandle popupRoot, Rect bounds, bool blockBackground) noexcept {
    activePopupRoot_ = popupRoot;
    activePopupBounds_ = bounds;
    blockBackground_ = blockBackground;
}

void UIPopupManager::ClosePopup() noexcept {
    activePopupRoot_ = InvalidUIHandle;
    activePopupBounds_ = Rect{};
}

bool UIPopupManager::IsPopupOpen() const noexcept {
    return activePopupRoot_.IsValid();
}

PopupInputResult UIPopupManager::HandlePointerDown(Vec2 pointerPosLogical) noexcept {
    if (!IsPopupOpen()) {
        return PopupInputResult::Unhandled;
    }

    if (activePopupBounds_.Contains(pointerPosLogical)) {
        return PopupInputResult::InsidePopup;
    }

    // Click was outside the popup: collapse active popup tree
    ClosePopup();
    return PopupInputResult::ClosedDueToOutsideClick;
}

} // namespace ToolEngine::SpriteUi::Widgets

