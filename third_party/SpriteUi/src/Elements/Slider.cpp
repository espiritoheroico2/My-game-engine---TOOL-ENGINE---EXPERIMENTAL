#include "SpriteUi/Elements/Slider.hpp"

#include "SpriteUi/Elements/Paint.hpp"
#include "SpriteUi/Rendering/Primitives.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ToolEngine::SpriteUi::Elements {

namespace {

struct Bounds {
    float low;
    float high;
};

// Intervalo usavel: limites nao finitos viram zero e o maximo nunca fica abaixo do minimo.
Bounds UsableBounds(const SliderRange& range) noexcept {
    const float low = IsFinite(range.minimum) ? range.minimum : 0.0f;
    const float high = IsFinite(range.maximum) ? std::max(low, range.maximum) : low;
    return {low, high};
}

// Limita ao intervalo e aplica o degrau (ou o arredondamento inteiro).
float Quantize(float value, const SliderRange& range) noexcept {
    const Bounds bounds = UsableBounds(range);
    float quantized = std::clamp(value, bounds.low, bounds.high);
    if (range.integer) {
        quantized = std::round(quantized);
    } else if (IsFinite(range.step) && range.step > 0.0f) {
        quantized = bounds.low + std::round((quantized - bounds.low) / range.step) * range.step;
    }
    return std::clamp(quantized, bounds.low, bounds.high);
}

float StepSize(const SliderRange& range) noexcept {
    if (range.integer) return 1.0f;
    const Bounds bounds = UsableBounds(range);
    if (IsFinite(range.step) && range.step > 0.0f) return range.step;
    return (bounds.high - bounds.low) / 100.0f;
}

} // namespace

float Slider::Value() const noexcept {
    return Quantize(value_, range);
}

bool Slider::SetValue(float value) noexcept {
    if (!IsFinite(value)) return false;
    const float before = Value();
    value_ = Quantize(value, range);
    return value_ != before;
}

bool Slider::StepBy(int steps) noexcept {
    if (steps == 0) return false;
    return SetValue(Value() + static_cast<float>(steps) * StepSize(range));
}

bool Slider::SetToMinimum() noexcept {
    return SetValue(UsableBounds(range).low);
}

bool Slider::SetToMaximum() noexcept {
    return SetValue(UsableBounds(range).high);
}

float Slider::Ratio() const noexcept {
    const Bounds bounds = UsableBounds(range);
    const float span = bounds.high - bounds.low;
    if (!(span > 0.0f)) return 0.0f;
    return std::clamp((Value() - bounds.low) / span, 0.0f, 1.0f);
}

SliderEvents Slider::OnPointer(const PointerEvent& event) noexcept {
    const GestureResult gesture = gesture_.Apply(states, event);
    SliderEvents events;
    events.pressStarted = gesture.pressStarted;
    events.pressEnded = gesture.pressEnded;
    const bool dragging = event.phase == PointerPhase::Move && states.pressed;
    if (gesture.pressStarted || dragging) {
        events.valueChanged = SetValue(ValueAtPosition(event.position));
    }
    return events;
}

float Slider::ValueAtPosition(Vec2 position) const noexcept {
    const Rect content = ContentRect();
    const float travel = content.size.x - thumbSize;
    const float ratio = travel > 0.0f ? (position.x - (content.position.x + 0.5f * thumbSize)) / travel : 0.0f;
    const Bounds bounds = UsableBounds(range);
    return bounds.low + std::clamp(ratio, 0.0f, 1.0f) * (bounds.high - bounds.low);
}

std::string SliderValueText(const Slider& slider) {
    char buffer[32];
    if (slider.range.integer) {
        std::snprintf(buffer, sizeof buffer, "%lld", static_cast<long long>(std::llround(slider.Value())));
    } else {
        std::snprintf(buffer, sizeof buffer, "%.2f", static_cast<double>(slider.Value()));
    }
    return buffer;
}

std::vector<Rendering::UIRenderItem> BuildSliderItems(const Slider& slider, const SliderStyle& style, float zOrder,
                                                      std::uint64_t paintOrder) {
    std::vector<Rendering::UIRenderItem> out;
    const Rect content = slider.ContentRect();
    if (!IsFinite(content.position) || !IsFinite(content.size)) return out;

    const float half = 0.5f * slider.thumbSize;
    const float trackY = content.position.y + 0.5f * (content.size.y - style.trackHeight);
    const Rect track{{content.position.x, trackY}, {content.size.x, style.trackHeight}};
    const float thumbCenterX = content.position.x + half +
                               slider.Ratio() * std::max(0.0f, content.size.x - slider.thumbSize);
    const Rect filled{{track.position.x, trackY}, {std::max(0.0f, thumbCenterX - track.position.x), style.trackHeight}};
    const Rect thumb{{thumbCenterX - half, content.position.y + 0.5f * (content.size.y - slider.thumbSize)},
                     {slider.thumbSize, slider.thumbSize}};

    const bool disabled = slider.states.disabled;
    const Vec4 fillColor = disabled ? style.disabled : style.filled;
    Vec4 thumbColor = disabled ? style.disabled : style.thumb;
    if (slider.states.hovered && !disabled) thumbColor = LightenColor(thumbColor, style.hoverAmount);

    std::uint64_t paint = paintOrder;
    auto pill = [&](Rect rect, float radius, Vec4 color) {
        if (auto rounded = Rendering::MakeRoundedRect(slider.handle, rect, radius, color, Rendering::NoClipIndex,
                                                      zOrder, paint++)) {
            out.push_back(*rounded);
        }
    };
    pill(track, 0.5f * style.trackHeight, style.track);
    pill(filled, 0.5f * style.trackHeight, fillColor);
    pill(thumb, half, thumbColor);
    if (slider.states.focused && !disabled) {
        AppendOutlineItems(slider.handle, thumb, style.focusRingWidth, style.focusRing, zOrder, paint, out);
    }
    return out;
}

} // namespace ToolEngine::SpriteUi::Elements
