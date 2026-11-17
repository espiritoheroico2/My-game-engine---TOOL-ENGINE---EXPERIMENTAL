#include "SpriteUi/Elements/Checkbox.hpp"

#include "SpriteUi/Elements/Paint.hpp"
#include "SpriteUi/Rendering/Primitives.hpp"

#include <algorithm>
#include <utility>

namespace ToolEngine::SpriteUi::Elements {

namespace {

CheckboxEvents ToggleValue(Checkbox& checkbox, const GestureResult& gesture) noexcept {
    CheckboxEvents events;
    events.pressStarted = gesture.pressStarted;
    events.pressEnded = gesture.pressEnded;
    events.clicked = gesture.clicked;
    if (gesture.clicked) {
        checkbox.checked = !checkbox.checked;
        events.valueChanged = true;
    }
    return events;
}

Vec4 BoxColor(const Checkbox& checkbox, const CheckboxStyle& style) noexcept {
    if (checkbox.states.disabled) return style.disabledBackground;
    if (checkbox.checked) return style.checkedBackground;
    if (checkbox.states.hovered) return LightenColor(style.boxBackground, style.hoverAmount);
    return style.boxBackground;
}

// Marca em V dentro da caixa: dois segmentos com pontas em fracoes da caixa.
void AppendMark(UIHandle owner, const Rect& box, const CheckboxStyle& style, std::uint64_t& paint,
                std::vector<Rendering::UIRenderItem>& out) {
    const Vec2 a{box.position.x + 0.25f * box.size.x, box.position.y + 0.52f * box.size.y};
    const Vec2 b{box.position.x + 0.43f * box.size.x, box.position.y + 0.72f * box.size.y};
    const Vec2 c{box.position.x + 0.76f * box.size.x, box.position.y + 0.30f * box.size.y};
    for (const auto& [from, to] : {std::pair{a, b}, std::pair{b, c}}) {
        if (auto segment = Rendering::MakeLine(owner, from, to, style.markThickness, style.mark,
                                               Rendering::NoClipIndex, 0.0f, paint++)) {
            out.push_back(*segment);
        }
    }
}

} // namespace

CheckboxEvents Checkbox::OnPointer(const PointerEvent& event) noexcept {
    return ToggleValue(*this, gesture_.Apply(states, event));
}

CheckboxEvents Checkbox::ToggleByKey() noexcept {
    CheckboxEvents events;
    if (!states.focused || states.disabled) return events;
    checked = !checked;
    events.valueChanged = true;
    return events;
}

Insets CheckboxLabelPadding(const CheckboxStyle& style) noexcept {
    Insets insets;
    insets.left = style.boxSize + style.boxGap;
    return insets;
}

Rect CheckboxBox(const Checkbox& checkbox, const CheckboxStyle& style) noexcept {
    const Rect content = checkbox.ContentRect();
    const float size = std::min(style.boxSize, content.size.y);
    return {{content.position.x, content.position.y + 0.5f * (content.size.y - size)}, {size, size}};
}

std::vector<Rendering::UIRenderItem> BuildCheckboxItems(const Checkbox& checkbox, const CheckboxStyle& style,
                                                        float zOrder, std::uint64_t paintOrder) {
    std::vector<Rendering::UIRenderItem> out;
    const Rect box = CheckboxBox(checkbox, style);
    if (!IsFinite(box.position) || !IsFinite(box.size)) return out;

    std::uint64_t paint = paintOrder;
    out.push_back(Rendering::MakeSolidRect(checkbox.handle, box, BoxColor(checkbox, style),
                                           Rendering::NoClipIndex, zOrder, paint++));
    if (checkbox.checked) AppendMark(checkbox.handle, box, style, paint, out);
    if (checkbox.states.focused && !checkbox.states.disabled) {
        AppendOutlineItems(checkbox.handle, box, style.focusRingWidth, style.focusRing, zOrder, paint, out);
    }
    return out;
}

} // namespace ToolEngine::SpriteUi::Elements
