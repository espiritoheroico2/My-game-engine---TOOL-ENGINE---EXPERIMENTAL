#include "SpriteUi/Elements/RadioButton.hpp"

#include "SpriteUi/Elements/Paint.hpp"
#include "SpriteUi/Rendering/Primitives.hpp"

#include <algorithm>
#include <utility>

namespace ToolEngine::SpriteUi::Elements {

namespace {

// Ponto central da opcao escolhida: metade do circulo, centrado.
Rect DotRect(Rect circle) noexcept {
    const Vec2 size{0.5f * circle.size.x, 0.5f * circle.size.y};
    return {{circle.position.x + 0.25f * circle.size.x, circle.position.y + 0.25f * circle.size.y}, size};
}

} // namespace

bool RadioGroup::AddOption(RadioOptionId id, std::string label) {
    if (id == NoRadioOption) return false;
    const bool repeated = std::any_of(options.begin(), options.end(),
                                      [id](const RadioOption& option) { return option.id == id; });
    if (repeated) return false;
    options.push_back(RadioOption{id, std::move(label)});
    return true;
}

bool RadioGroup::Select(RadioOptionId id) noexcept {
    if (id == selectedId) return false;
    const bool known = std::any_of(options.begin(), options.end(),
                                   [id](const RadioOption& option) { return option.id == id; });
    if (!known) return false;
    selectedId = id;
    return true;
}

std::size_t RadioGroup::SelectedIndex() const noexcept {
    for (std::size_t i = 0; i < options.size(); ++i) {
        if (options[i].id == selectedId) return i;
    }
    return NoRadioIndex;
}

std::size_t RadioGroup::RowAt(Vec2 point) const noexcept {
    const Rect content = ContentRect();
    if (!IsFinite(point) || rowHeight <= 0.0f) return NoRadioIndex;
    const bool insideX = point.x >= content.position.x && point.x <= content.position.x + content.size.x;
    const float offset = point.y - content.position.y;
    if (!insideX || offset < 0.0f) return NoRadioIndex;
    const float row = offset / rowHeight;
    if (row >= static_cast<float>(options.size())) return NoRadioIndex;
    return static_cast<std::size_t>(row);
}

RadioEvents RadioGroup::OnPointer(const PointerEvent& event) noexcept {
    const GestureResult gesture = gesture_.Apply(states, event);
    RadioEvents events;
    events.pressStarted = gesture.pressStarted;
    events.pressEnded = gesture.pressEnded;
    events.clicked = gesture.clicked;
    if (!gesture.clicked) return events;

    const std::size_t row = RowAt(event.position);
    if (row == NoRadioIndex) return events;
    focusedIndex = row;
    events.valueChanged = Select(options[row].id);
    return events;
}

bool RadioGroup::MoveSelection(int delta) noexcept {
    if (states.disabled || options.empty()) return false;
    std::size_t current = focusedIndex;
    if (current >= options.size()) current = SelectedIndex();
    if (current >= options.size()) current = 0;
    const long long last = static_cast<long long>(options.size()) - 1;
    const long long target = std::clamp(static_cast<long long>(current) + delta, 0LL, last);
    focusedIndex = static_cast<std::size_t>(target);
    return Select(options[focusedIndex].id);
}

bool RadioGroup::SelectFocused() noexcept {
    if (states.disabled || focusedIndex >= options.size()) return false;
    return Select(options[focusedIndex].id);
}

void RadioGroup::FocusSelected() noexcept {
    const std::size_t selected = SelectedIndex();
    if (selected < options.size()) {
        focusedIndex = selected;
    } else {
        focusedIndex = options.empty() ? NoRadioIndex : 0;
    }
}

Rect RadioRowRect(const RadioGroup& group, std::size_t index) noexcept {
    if (index >= group.options.size()) return {};
    const Rect content = group.ContentRect();
    const float top = content.position.y + static_cast<float>(index) * group.rowHeight;
    return {{content.position.x, top}, {content.size.x, group.rowHeight}};
}

Rect RadioCircleRect(const RadioGroup& group, const RadioStyle& style, std::size_t index) noexcept {
    const Rect row = RadioRowRect(group, index);
    const float size = std::min(style.circleSize, row.size.y);
    return {{row.position.x, row.position.y + 0.5f * (row.size.y - size)}, {size, size}};
}

Rect RadioLabelRect(const RadioGroup& group, const RadioStyle& style, std::size_t index) noexcept {
    const Rect row = RadioRowRect(group, index);
    const float left = style.circleSize + style.labelGap;
    return {{row.position.x + left, row.position.y}, {std::max(0.0f, row.size.x - left), row.size.y}};
}

std::vector<Rendering::UIRenderItem> BuildRadioGroupItems(const RadioGroup& group, const RadioStyle& style,
                                                          float zOrder, std::uint64_t paintOrder) {
    std::vector<Rendering::UIRenderItem> out;
    std::uint64_t paint = paintOrder;
    const std::size_t selected = group.SelectedIndex();
    const Vec4 background = group.states.disabled ? style.disabledBackground : style.circleBackground;
    // Anel de foco: circulo maior, pintado antes dos circulos. O circulo focado por cima deixa so a borda.
    if (group.focusedIndex < group.options.size() && !group.states.disabled) {
        const Rect focused = RadioCircleRect(group, style, group.focusedIndex);
        AppendRoundedItem(group.handle, GrowRect(focused, style.focusRingWidth), style.focusRing, zOrder, paint, out);
    }
    for (std::size_t i = 0; i < group.options.size(); ++i) {
        const Rect circle = RadioCircleRect(group, style, i);
        if (!IsFinite(circle.position) || !IsFinite(circle.size)) continue;
        AppendRoundedItem(group.handle, circle, background, zOrder, paint, out);
        if (i == selected) AppendRoundedItem(group.handle, DotRect(circle), style.dotColor, zOrder, paint, out);
    }
    return out;
}

} // namespace ToolEngine::SpriteUi::Elements
