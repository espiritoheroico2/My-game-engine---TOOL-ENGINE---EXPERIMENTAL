#include "SpriteUi/Elements/Button.hpp"

#include "SpriteUi/Elements/Paint.hpp"
#include "SpriteUi/Rendering/Primitives.hpp"

#include <algorithm>

namespace ToolEngine::SpriteUi::Elements {

namespace {

Vec4 StateColor(const Button& button, const ButtonStyle& style) noexcept {
    if (button.states.disabled) return style.disabledBackground;
    if (button.states.pressed) return DarkenColor(style.background, style.pressedAmount);
    if (button.states.hovered) return LightenColor(style.background, style.hoverAmount);
    return style.background;
}

float Finite(float value) noexcept {
    return IsFinite(value) ? value : 0.0f;
}

} // namespace

ButtonEvents Button::OnPointer(const PointerEvent& event) noexcept {
    const GestureResult gesture = gesture_.Apply(states, event);
    ButtonEvents events;
    events.pressStarted = gesture.pressStarted;
    events.pressEnded = gesture.pressEnded;
    events.clicked = gesture.clicked;
    return events;
}

ButtonEvents Button::ActivateByKey() noexcept {
    ButtonEvents events;
    events.activated = states.focused && !states.disabled;
    return events;
}

void Button::Advance(float frameSeconds) noexcept {
    gesture_.Advance(states, frameSeconds);
}

bool Button::IsHeld() const noexcept {
    return gesture_.IsHeld(states, holdSeconds);
}

Vec2 PreferredButtonSize(const Text::TextLayoutResult& labelLayout, Insets padding) noexcept {
    const float width = Finite(labelLayout.size.x) + Finite(padding.left) + Finite(padding.right);
    const float height = Finite(labelLayout.lineHeight) + Finite(padding.top) + Finite(padding.bottom);
    return {std::max(0.0f, width), std::max(0.0f, height)};
}

Vec2 ButtonPressOffset(const Button& button, const ButtonStyle& style) noexcept {
    const bool pressedVisually = button.states.pressed && !button.states.disabled;
    return {0.0f, pressedVisually ? style.pressOffsetY : 0.0f};
}

std::vector<Rendering::UIRenderItem> BuildButtonBackground(const Button& button, const ButtonStyle& style,
                                                           float zOrder, std::uint64_t paintOrder) {
    std::vector<Rendering::UIRenderItem> out;
    if (!IsFinite(button.border.position) || !IsFinite(button.border.size)) return out;

    const Vec2 offset = ButtonPressOffset(button, style);
    const Rect box{button.border.position + offset, button.border.size};
    const Vec4 color = StateColor(button, style);
    std::uint64_t paint = paintOrder;

    // O anel de foco segue a caixa (inclusive o deslocamento do pressionado) e acompanha os cantos arredondados.
    if (button.states.focused && !button.states.disabled) {
        AppendBox(button.handle, box, button.cornerRadius, style.focusRingWidth, color, style.focusRing,
                  Rendering::NoClipIndex, zOrder, paint, out);
    } else {
        AppendFill(button.handle, box, button.cornerRadius, color, Rendering::NoClipIndex, zOrder, paint, out);
    }
    return out;
}

} // namespace ToolEngine::SpriteUi::Elements
