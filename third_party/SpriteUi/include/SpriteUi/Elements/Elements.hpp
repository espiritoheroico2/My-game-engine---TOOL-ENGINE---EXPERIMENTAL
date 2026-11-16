#pragma once

#include "SpriteUi/Core/Math.hpp"
#include "SpriteUi/Rendering/DrawList.hpp"
#include "SpriteUi/Rendering/Primitives.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace ToolEngine::SpriteUi::Elements {

inline constexpr std::size_t NoControlIndex = std::numeric_limits<std::size_t>::max();
using RadioOptionId = std::uint32_t;
inline constexpr RadioOptionId NoRadioOption = std::numeric_limits<RadioOptionId>::max();

enum class PointerPhase : std::uint8_t { Down, Up, Move };

struct PointerEvent {
    PointerPhase phase = PointerPhase::Move;
    Vec2 position{};
    std::uint32_t pointerId = 0;
    bool inside = false;
};

namespace detail {

inline Rendering::UIRenderItem Solid(Rect bounds, Vec4 color, std::uint32_t clip, float z, std::uint64_t paint) {
    Rendering::UIRenderItem item;
    item.handle = UIHandle{0, 1};
    item.layout.resolvedPosition = bounds.position;
    item.layout.resolvedSize = bounds.size;
    item.tintPremultipliedLinear = color;
    item.textureSlice = Rendering::WhiteTextureSlice;
    item.clipIndex = clip;
    item.zOrder = z;
    item.paintOrder = paint;
    return item;
}

inline std::vector<Rendering::UIRenderItem> One(Rect rect, Vec4 color, std::uint32_t clip, float z, std::uint64_t paint) {
    if (!IsFinite(rect.position) || !IsFinite(rect.size) || rect.size.x <= 0.0f || rect.size.y <= 0.0f) return {};
    return {Solid(rect, color, clip, z, paint)};
}

} // namespace detail

struct Panel {
    UIHandle handle{0, 1};
    Rect border{};
};

struct PanelStyle {
    Vec4 background{0.055f, 0.075f, 0.11f, 1.0f};
    Vec4 border{0.20f, 0.24f, 0.30f, 1.0f};
    float borderWidth = 1.0f;
    float cornerRadius = 0.0f;
};

[[nodiscard]] inline std::vector<Rendering::UIRenderItem> BuildPanelItems(
    const Panel& panel, const PanelStyle& style, float zOrder, std::uint64_t paintOrder)
{
    const UIHandle owner = panel.handle.IsValid() ? panel.handle : UIHandle{0, 1};
    auto background = Rendering::MakeRoundedRect(owner, panel.border, style.cornerRadius, style.background,
                                                  Rendering::NoClipIndex, zOrder, paintOrder++);
    std::vector<Rendering::UIRenderItem> items;
    if (background) items.push_back(*background);
    const float edge = std::max(0.0f, style.borderWidth);
    if (edge > 0.0f && panel.border.size.x > 0.0f && panel.border.size.y > 0.0f) {
        const Rect r = panel.border;
        const Rect edges[] = {
            {r.position, {r.size.x, edge}},
            {{r.position.x, r.position.y + r.size.y - edge}, {r.size.x, edge}},
            {r.position, {edge, r.size.y}},
            {{r.position.x + r.size.x - edge, r.position.y}, {edge, r.size.y}},
        };
        for (const Rect& e : edges) {
            Rendering::UIRenderItem item = detail::Solid(e, style.border, Rendering::NoClipIndex, zOrder, paintOrder++);
            item.handle = owner;
            items.push_back(item);
        }
    }
    return items;
}

struct ButtonState {
    bool hovered = false;
    bool pressed = false;
    bool disabled = false;
};

struct ButtonEvents {
    bool pressStarted = false;
    bool clicked = false;
};

struct Button {
    Rect border{};
    ButtonState states{};
    bool pressed_ = false;

    [[nodiscard]] ButtonEvents OnPointer(const PointerEvent& event) noexcept {
        ButtonEvents result;
        if (event.phase == PointerPhase::Down && event.inside && !states.disabled) {
            pressed_ = true;
            states.pressed = true;
            result.pressStarted = true;
        } else if (event.phase == PointerPhase::Up) {
            result.clicked = pressed_ && event.inside && !states.disabled;
            pressed_ = false;
            states.pressed = false;
        }
        states.hovered = event.inside;
        return result;
    }
};

struct ButtonStyle {
    Vec4 background{0.13f, 0.16f, 0.21f, 1.0f};
    Vec4 hover{0.19f, 0.25f, 0.34f, 1.0f};
    Vec4 pressed{0.10f, 0.22f, 0.38f, 1.0f};
    Vec4 disabled{0.10f, 0.11f, 0.13f, 1.0f};
};

[[nodiscard]] inline std::vector<Rendering::UIRenderItem> BuildButtonBackground(
    const Button& button, const ButtonStyle& style, float zOrder, std::uint64_t paintOrder)
{
    const Vec4 color = button.states.disabled ? style.disabled :
                       button.states.pressed ? style.pressed :
                       button.states.hovered ? style.hover : style.background;
    return detail::One(button.border, color, Rendering::NoClipIndex, zOrder, paintOrder);
}

struct CheckboxEvents {
    bool pressStarted = false;
    bool clicked = false;
    bool valueChanged = false;
};

struct Checkbox {
    Rect border{};
    ButtonState states{};
    bool checked = false;
    bool pressed_ = false;

    [[nodiscard]] CheckboxEvents OnPointer(const PointerEvent& event) noexcept {
        CheckboxEvents result;
        if (event.phase == PointerPhase::Down && event.inside && !states.disabled) {
            pressed_ = true;
            states.pressed = true;
            result.pressStarted = true;
        } else if (event.phase == PointerPhase::Up) {
            result.clicked = pressed_ && event.inside && !states.disabled;
            if (result.clicked) {
                checked = !checked;
                result.valueChanged = true;
            }
            pressed_ = false;
            states.pressed = false;
        }
        states.hovered = event.inside;
        return result;
    }
};

struct CheckboxStyle {
    Vec4 background{0.10f, 0.12f, 0.16f, 1.0f};
    Vec4 checked{0.20f, 0.48f, 0.82f, 1.0f};
    Vec4 border{0.32f, 0.36f, 0.42f, 1.0f};
};

[[nodiscard]] inline std::vector<Rendering::UIRenderItem> BuildCheckboxItems(
    const Checkbox& checkbox, const CheckboxStyle& style, float zOrder, std::uint64_t paintOrder)
{
    std::vector<Rendering::UIRenderItem> items = detail::One(checkbox.border, style.background, Rendering::NoClipIndex, zOrder, paintOrder++);
    if (checkbox.checked && checkbox.border.size.x > 6.0f && checkbox.border.size.y > 6.0f) {
        const float inset = std::min(4.0f, std::min(checkbox.border.size.x, checkbox.border.size.y) * 0.2f);
        items.push_back(detail::Solid({checkbox.border.position + Vec2{inset, inset},
                                      checkbox.border.size - Vec2{2.0f * inset, 2.0f * inset}},
                                      style.checked, Rendering::NoClipIndex, zOrder + 0.01f, paintOrder));
    }
    return items;
}

struct SliderRange {
    float minimum = 0.0f;
    float maximum = 1.0f;
    float step = 0.0f;
    bool integer = false;
};

struct SliderEvents {
    bool valueChanged = false;
    bool pressStarted = false;
    bool pressEnded = false;
};

struct Slider {
    Rect border{};
    ButtonState states{};
    SliderRange range{};
    float value = 0.5f;
    bool dragging_ = false;

    [[nodiscard]] bool SetValue(float candidate) noexcept {
        if (!IsFinite(candidate) || !IsFinite(range.minimum) || !IsFinite(range.maximum) ||
            range.maximum < range.minimum || !IsFinite(range.step) || range.step < 0.0f) return false;
        value = std::clamp(candidate, range.minimum, range.maximum);
        if (range.step > 0.0f) value = std::clamp(range.minimum + std::round((value - range.minimum) / range.step) * range.step,
                                                  range.minimum, range.maximum);
        if (range.integer) value = std::round(value);
        return true;
    }
    [[nodiscard]] float Value() const noexcept { return value; }
    [[nodiscard]] float Normalized() const noexcept {
        const float span = range.maximum - range.minimum;
        return IsFinite(span) && span > 0.0f ? Clamp01((value - range.minimum) / span) : 0.0f;
    }
    [[nodiscard]] SliderEvents OnPointer(const PointerEvent& event) noexcept {
        SliderEvents result;
        if (event.phase == PointerPhase::Down && event.inside && !states.disabled) {
            dragging_ = true;
            states.pressed = true;
            result.pressStarted = true;
        } else if (event.phase == PointerPhase::Up) {
            result.pressEnded = dragging_;
            dragging_ = false;
            states.pressed = false;
        }
        states.hovered = event.inside;
        if ((dragging_ || (event.phase == PointerPhase::Down && event.inside)) && !states.disabled && border.size.x > 0.0f) {
            const float old = value;
            (void)SetValue(range.minimum + Clamp01((event.position.x - border.position.x) / border.size.x) *
                                         (range.maximum - range.minimum));
            result.valueChanged = value != old;
        }
        return result;
    }
};

struct SliderStyle {
    Vec4 track{0.08f, 0.10f, 0.14f, 1.0f};
    Vec4 fill{0.20f, 0.48f, 0.82f, 1.0f};
    Vec4 thumb{0.86f, 0.88f, 0.92f, 1.0f};
};

[[nodiscard]] inline std::vector<Rendering::UIRenderItem> BuildSliderItems(
    const Slider& slider, const SliderStyle& style, float zOrder, std::uint64_t paintOrder)
{
    std::vector<Rendering::UIRenderItem> items = detail::One(slider.border, style.track, Rendering::NoClipIndex, zOrder, paintOrder++);
    const float fillWidth = slider.border.size.x * slider.Normalized();
    if (fillWidth > 0.0f) {
        items.push_back(detail::Solid({slider.border.position, {fillWidth, slider.border.size.y}}, style.fill,
                                      Rendering::NoClipIndex, zOrder + 0.01f, paintOrder++));
    }
    const float thumbWidth = std::min(8.0f, slider.border.size.x);
    const float x = slider.border.position.x + fillWidth - thumbWidth * 0.5f;
    items.push_back(detail::Solid({{std::clamp(x, slider.border.position.x, slider.border.position.x + slider.border.size.x - thumbWidth),
                                    slider.border.position.y},
                                   {thumbWidth, slider.border.size.y}},
                                  style.thumb, Rendering::NoClipIndex, zOrder + 0.02f, paintOrder));
    return items;
}

struct DropdownControl {
    std::vector<std::string> options;
    std::size_t selectedIndex = NoControlIndex;
    bool disabled = false;
    bool open = false;
    void Close() noexcept { open = false; }
};

struct DropdownEvents {
    bool opened = false;
    bool closed = false;
    bool valueChanged = false;
};

struct Dropdown {
    Rect border{};
    ButtonState states{};
    DropdownControl control{};
    float rowHeight = 24.0f;
    Rect popup{};

    void Layout(Rect viewport) noexcept {
        const float h = std::max(0.0f, std::isfinite(rowHeight) ? rowHeight : 0.0f) * static_cast<float>(control.options.size());
        const float yBelow = border.position.y + border.size.y;
        const float y = yBelow + h <= viewport.position.y + viewport.size.y
                            ? yBelow
                            : std::max(viewport.position.y, border.position.y - h);
        popup = {{border.position.x, y}, {border.size.x, std::min(h, std::max(0.0f, viewport.size.y))}};
    }

    [[nodiscard]] Rect Popup() const noexcept { return popup; }

    [[nodiscard]] DropdownEvents OnPointer(const PointerEvent& event, Rect viewport) {
        DropdownEvents result;
        if (event.phase != PointerPhase::Down) return result;
        if (event.inside && !control.disabled) {
            Layout(viewport);
            control.open = !control.open;
            result.opened = control.open;
            result.closed = !control.open;
        } else if (control.open) {
            control.Close();
            result.closed = true;
        }
        return result;
    }

    [[nodiscard]] DropdownEvents ChooseAt(Vec2 point, Rect viewport) {
        DropdownEvents result;
        Layout(viewport);
        if (!control.open || !popup.Contains(point) || rowHeight <= 0.0f || control.options.empty()) return result;
        const std::size_t index = static_cast<std::size_t>((point.y - popup.position.y) / rowHeight);
        if (index < control.options.size()) {
            result.valueChanged = control.selectedIndex != index;
            control.selectedIndex = index;
            control.Close();
            result.closed = true;
        }
        return result;
    }
};

struct DropdownStyle {
    Vec4 background{0.12f, 0.14f, 0.18f, 1.0f};
    Vec4 popup{0.07f, 0.09f, 0.12f, 1.0f};
    Vec4 highlight{0.12f, 0.30f, 0.60f, 1.0f};
    Vec4 border{0.20f, 0.24f, 0.30f, 1.0f};
};

[[nodiscard]] inline Rect DropdownRowRect(const Dropdown& dropdown, std::size_t row) noexcept {
    if (row >= dropdown.control.options.size()) return {};
    return {{dropdown.popup.position.x, dropdown.popup.position.y + dropdown.rowHeight * static_cast<float>(row)},
            {dropdown.popup.size.x, dropdown.rowHeight}};
}

[[nodiscard]] inline std::vector<Rendering::UIRenderItem> BuildDropdownButtonItems(
    const Dropdown& dropdown, const DropdownStyle& style, float zOrder, std::uint64_t paintOrder)
{
    return detail::One(dropdown.border, dropdown.control.disabled ? style.border : style.background,
                       Rendering::NoClipIndex, zOrder, paintOrder);
}

[[nodiscard]] inline std::vector<Rendering::UIRenderItem> BuildDropdownPopupItems(
    const Dropdown& dropdown, const DropdownStyle& style, std::uint32_t clipIndex, float zOrder, std::uint64_t paintOrder)
{
    return detail::One(dropdown.Popup(), style.popup, clipIndex, zOrder, paintOrder);
}

struct TabBarControl {
    std::vector<std::string> labels;
    std::size_t activeIndex = NoControlIndex;
    explicit TabBarControl(std::vector<std::string> values = {}) : labels{std::move(values)} {
        if (!labels.empty()) activeIndex = 0;
    }
    bool Select(std::size_t index) noexcept {
        if (index >= labels.size() || index == activeIndex) return false;
        activeIndex = index;
        return true;
    }
};

struct TabEvents { bool changed = false; };

struct TabBar {
    Rect border{};
    TabBarControl control{};
    float tabWidth = 92.0f;
    float tabGap = 2.0f;
    float stripPadding = 8.0f;
    float scrollX = 0.0f;

    [[nodiscard]] TabEvents OnPointer(const PointerEvent& event) noexcept {
        if (event.phase != PointerPhase::Up || !border.Contains(event.position) || tabWidth + tabGap <= 0.0f) return {};
        const float x = event.position.x - border.position.x - stripPadding + scrollX;
        if (x < 0.0f) return {};
        const std::size_t index = static_cast<std::size_t>(x / (tabWidth + tabGap));
        if (index >= control.labels.size() || x - static_cast<float>(index) * (tabWidth + tabGap) > tabWidth) return {};
        return {control.Select(index)};
    }
};

struct TabBarStyle {
    Vec4 background{0.055f, 0.075f, 0.11f, 1.0f};
    Vec4 active{0.12f, 0.18f, 0.26f, 1.0f};
    Vec4 underline{0.2f, 0.48f, 0.82f, 1.0f};
};

[[nodiscard]] inline Rect TabRect(const TabBar& tabs, std::size_t index) noexcept {
    if (index >= tabs.control.labels.size()) return {};
    const float stride = tabs.tabWidth + tabs.tabGap;
    return {{tabs.border.position.x + tabs.stripPadding + static_cast<float>(index) * stride - tabs.scrollX,
             tabs.border.position.y},
            {tabs.tabWidth, tabs.border.size.y}};
}

[[nodiscard]] inline std::vector<Rendering::UIRenderItem> BuildTabBarItems(
    const TabBar& tabs, const TabBarStyle& style, std::uint32_t clipIndex, float zOrder, std::uint64_t paintOrder)
{
    std::vector<Rendering::UIRenderItem> items = detail::One(tabs.border, style.background, clipIndex, zOrder, paintOrder++);
    if (tabs.control.activeIndex < tabs.control.labels.size()) {
        const Rect active = TabRect(tabs, tabs.control.activeIndex);
        items.push_back(detail::Solid({{active.position.x, active.position.y + active.size.y - 2.0f},
                                       {active.size.x, 2.0f}},
                                      style.underline, clipIndex, zOrder + 0.01f, paintOrder));
    }
    return items;
}

struct RadioOption {
    RadioOptionId id = NoRadioOption;
    std::string label;
    bool disabled = false;
};

struct RadioEvents { bool valueChanged = false; };

struct RadioGroup {
    Rect border{};
    std::vector<RadioOption> options;
    RadioOptionId selectedId = NoRadioOption;
    float rowHeight = 22.0f;

    [[nodiscard]] bool AddOption(RadioOptionId id, std::string label, bool disabled = false) {
        if (id == NoRadioOption) return false;
        options.push_back({id, std::move(label), disabled});
        return true;
    }
    [[nodiscard]] RadioEvents OnPointer(const PointerEvent& event) noexcept {
        if (event.phase != PointerPhase::Up || !border.Contains(event.position) || rowHeight <= 0.0f) return {};
        const std::size_t index = static_cast<std::size_t>((event.position.y - border.position.y) / rowHeight);
        if (index >= options.size() || options[index].disabled || selectedId == options[index].id) return {};
        selectedId = options[index].id;
        return {true};
    }
};

struct RadioStyle {
    Vec4 background{0.12f, 0.14f, 0.18f, 1.0f};
    Vec4 selected{0.20f, 0.48f, 0.82f, 1.0f};
    float labelWidth = 130.0f;
};

[[nodiscard]] inline Rect RadioLabelRect(const RadioGroup& group, const RadioStyle& style, std::size_t index) noexcept {
    if (index >= group.options.size()) return {};
    return {{group.border.position.x + 22.0f, group.border.position.y + static_cast<float>(index) * group.rowHeight},
            {style.labelWidth, group.rowHeight}};
}

[[nodiscard]] inline std::vector<Rendering::UIRenderItem> BuildRadioGroupItems(
    const RadioGroup& group, const RadioStyle& style, float zOrder, std::uint64_t paintOrder)
{
    std::vector<Rendering::UIRenderItem> items;
    for (std::size_t i = 0; i < group.options.size(); ++i) {
        const Rect r{{group.border.position.x, group.border.position.y + static_cast<float>(i) * group.rowHeight},
                     {16.0f, 16.0f}};
        items.push_back(detail::Solid(r, group.options[i].id == group.selectedId ? style.selected : style.background,
                                      Rendering::NoClipIndex, zOrder, paintOrder++));
    }
    return items;
}

} // namespace ToolEngine::SpriteUi::Elements
