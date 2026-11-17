#include "SpriteUi/Widgets/Modal.hpp"

namespace ToolEngine::SpriteUi::Widgets {

void ModalControl::Open(Rect dialog, std::size_t focusCount, std::size_t defaultIndex,
                        std::uint64_t returnFocusTo) noexcept {
    dialog_ = dialog;
    focusCount_ = focusCount;
    focus_ = defaultIndex < focusCount ? defaultIndex : 0;
    activated_ = NoControlIndex;
    returnFocusTo_ = returnFocusTo;
    open_ = true;
}

void ModalControl::Close() noexcept {
    open_ = false;
}

ModalPointer ModalControl::Classify(Vec2 pointer) const noexcept {
    return dialog_.Contains(pointer) ? ModalPointer::Dialog : ModalPointer::Blocked;
}

ModalPointer ModalControl::PointerDown(Vec2 pointer) noexcept {
    if (!open_) return ModalPointer::Pass;
    const ModalPointer where = Classify(pointer);
    if (where == ModalPointer::Blocked && config_.closeOnOutsideClick) {
        Close();
        return ModalPointer::Cancelled;
    }
    return where;
}

ModalPointer ModalControl::PointerMove(Vec2 pointer) const noexcept {
    return open_ ? Classify(pointer) : ModalPointer::Pass;
}

ModalEvent ModalControl::Key(ModalKey key) noexcept {
    if (!open_) return ModalEvent::None;
    switch (key) {
    case ModalKey::Tab:
        return CycleFocus(true);
    case ModalKey::BackTab:
        return CycleFocus(false);
    case ModalKey::Enter:
        activated_ = focusCount_ > 0 ? focus_ : NoControlIndex;
        Close();
        return ModalEvent::Confirmed;
    case ModalKey::Escape:
        Close();
        return ModalEvent::Cancelled;
    }
    return ModalEvent::None;
}

ModalEvent ModalControl::CycleFocus(bool forward) noexcept {
    if (focusCount_ < 2) return ModalEvent::None;
    focus_ = forward ? (focus_ + 1) % focusCount_ : (focus_ + focusCount_ - 1) % focusCount_;
    return ModalEvent::FocusMoved;
}

} // namespace ToolEngine::SpriteUi::Widgets
