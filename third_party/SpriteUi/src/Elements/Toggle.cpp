#include "SpriteUi/Elements/Toggle.hpp"

#include "SpriteUi/Elements/Paint.hpp"
#include "SpriteUi/Rendering/Primitives.hpp"

#include <algorithm>

namespace ToolEngine::SpriteUi::Elements {

namespace {

Vec4 TrackColor(const Toggle& toggle, const ToggleStyle& style) noexcept {
    if (toggle.states.disabled) return style.disabledTrack;
    if (toggle.on) return style.trackOn;
    if (toggle.states.hovered) return LightenColor(style.trackOff, style.hoverAmount);
    return style.trackOff;
}

} // namespace

ToggleEvents Toggle::OnPointer(const PointerEvent& event, const ToggleStyle& style) noexcept {
    // A trilha e uma pilula: o raio e metade da altura (limitado pelo helper). Fora dela, nada aciona.
    const Rect track = ToggleTrackRect(*this, style);
    const GestureResult gesture = gesture_.Apply(states, WithinDrawn(event, track, 0.5f * track.size.y));
    ToggleEvents events;
    events.pressStarted = gesture.pressStarted;
    events.pressEnded = gesture.pressEnded;
    events.clicked = gesture.clicked;
    if (gesture.clicked) {
        on = !on;
        events.valueChanged = true;
    }
    return events;
}

ToggleEvents Toggle::ToggleByKey() noexcept {
    ToggleEvents events;
    if (!states.focused || states.disabled) return events;
    on = !on;
    events.valueChanged = true;
    return events;
}

Rect ToggleTrackRect(const Toggle& toggle, const ToggleStyle& style) noexcept {
    const Rect content = toggle.ContentRect();
    const Vec2 size{std::min(style.trackWidth, content.size.x), std::min(style.trackHeight, content.size.y)};
    return {{content.position.x, content.position.y + 0.5f * (content.size.y - size.y)}, size};
}

Rect ToggleKnobRect(const Toggle& toggle, const ToggleStyle& style) noexcept {
    const Rect track = ToggleTrackRect(toggle, style);
    const float diameter = std::max(0.0f, track.size.y - 2.0f * style.knobInset);
    const float left = toggle.on ? track.position.x + track.size.x - style.knobInset - diameter
                                 : track.position.x + style.knobInset;
    return {{left, track.position.y + style.knobInset}, {diameter, diameter}};
}

std::vector<Rendering::UIRenderItem> BuildToggleItems(const Toggle& toggle, const ToggleStyle& style, float zOrder,
                                                      std::uint64_t paintOrder) {
    std::vector<Rendering::UIRenderItem> out;
    const Rect track = ToggleTrackRect(toggle, style);
    if (!IsFinite(track.position) || !IsFinite(track.size)) return out;

    std::uint64_t paint = paintOrder;
    // O anel de foco e uma pilula maior, pintada antes da trilha: a trilha por cima deixa so a borda visivel.
    if (toggle.states.focused && !toggle.states.disabled) {
        AppendRoundedItem(toggle.handle, GrowRect(track, style.focusRingWidth), style.focusRing, zOrder, paint, out);
    }
    AppendRoundedItem(toggle.handle, track, TrackColor(toggle, style), zOrder, paint, out);
    const Rect knob = ToggleKnobRect(toggle, style);
    if (knob.size.x > 0.0f) AppendRoundedItem(toggle.handle, knob, style.knob, zOrder, paint, out);
    return out;
}

} // namespace ToolEngine::SpriteUi::Elements
