#include "SpriteUi/Elements/PressGesture.hpp"

#include "SpriteUi/Core/Math.hpp"

namespace ToolEngine::SpriteUi::Elements {

GestureResult PressGesture::Apply(ElementStates& states, const PointerEvent& event) noexcept {
    GestureResult result;
    if (states.disabled) {
        states.hovered = false;
        EndPress(states);
        return result;
    }

    switch (event.phase) {
    case PointerPhase::Enter:
        states.hovered = true;
        break;
    case PointerPhase::Leave:
        states.hovered = false;
        break;
    case PointerPhase::Down:
        if (event.inside && !states.pressed) {
            states.pressed = true;
            pointer_ = event.pointerId;
            pressedSeconds_ = 0.0f;
            result.pressStarted = true;
        }
        break;
    case PointerPhase::Up:
        if (states.pressed && event.pointerId == pointer_) {
            result.pressEnded = true;
            result.clicked = event.inside;
            EndPress(states);
        }
        break;
    case PointerPhase::Cancel:
        if (states.pressed && event.pointerId == pointer_) EndPress(states);
        break;
    case PointerPhase::Move:
        break;
    }
    return result;
}

void PressGesture::Advance(const ElementStates& states, float frameSeconds) noexcept {
    if (!states.pressed || !IsFinite(frameSeconds) || frameSeconds <= 0.0f) return;
    pressedSeconds_ += frameSeconds;
}

bool PressGesture::IsHeld(const ElementStates& states, float holdSeconds) const noexcept {
    return states.pressed && pressedSeconds_ >= holdSeconds;
}

void PressGesture::EndPress(ElementStates& states) noexcept {
    states.pressed = false;
    pressedSeconds_ = 0.0f;
}

} // namespace ToolEngine::SpriteUi::Elements
