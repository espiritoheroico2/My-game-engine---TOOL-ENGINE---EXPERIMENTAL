#pragma once

#include "SpriteUi/Core/Math.hpp"
#include "SpriteUi/Rendering/DrawList.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace ToolEngine::SpriteUi::Widgets {

enum class ColorChannel : std::uint8_t { Red, Green, Blue, Alpha };

struct ColorPickerLayout {
    Rect saturationValue{};
    Rect hue{};
    Rect hexField{};
    std::array<Rect, 4> channelFields{};
};

struct ColorPickerStyle {
    float saturationValueHeight = 120.0f;
    float hueHeight = 14.0f;
    float fieldHeight = 24.0f;
    float channelHeight = 20.0f;
    float gap = 8.0f;
};

class ColorPickerControl {
public:
    [[nodiscard]] bool SetColor(Vec4 color) noexcept {
        if (!IsFinite(color)) return false;
        color.x = Clamp01(color.x);
        color.y = Clamp01(color.y);
        color.z = Clamp01(color.z);
        color.w = Clamp01(color.w);
        const float maximum = std::max({color.x, color.y, color.z});
        const float minimum = std::min({color.x, color.y, color.z});
        const float delta = maximum - minimum;
        value_ = maximum;
        saturation_ = maximum <= 0.0f ? 0.0f : delta / maximum;
        if (delta <= 0.0f) hue_ = 0.0f;
        else if (maximum == color.x) hue_ = std::fmod((color.y - color.z) / delta, 6.0f) / 6.0f;
        else if (maximum == color.y) hue_ = ((color.z - color.x) / delta + 2.0f) / 6.0f;
        else hue_ = ((color.x - color.y) / delta + 4.0f) / 6.0f;
        hue_ = hue_ < 0.0f ? hue_ + 1.0f : hue_;
        alpha_ = color.w;
        return true;
    }

    [[nodiscard]] Vec4 Sample() const noexcept {
        return SampleAt(saturation_, value_);
    }

    [[nodiscard]] Vec4 SampleAt(float saturation, float value) const noexcept {
        const float h = hue_ * 6.0f;
        const float c = Clamp01(value) * Clamp01(saturation);
        const float x = c * (1.0f - std::abs(std::fmod(h, 2.0f) - 1.0f));
        const float m = Clamp01(value) - c;
        Vec4 rgb;
        if (h < 1.0f) rgb = {c, x, 0.0f, alpha_};
        else if (h < 2.0f) rgb = {x, c, 0.0f, alpha_};
        else if (h < 3.0f) rgb = {0.0f, c, x, alpha_};
        else if (h < 4.0f) rgb = {0.0f, x, c, alpha_};
        else if (h < 5.0f) rgb = {x, 0.0f, c, alpha_};
        else rgb = {c, 0.0f, x, alpha_};
        rgb.x += m;
        rgb.y += m;
        rgb.z += m;
        return rgb;
    }

    [[nodiscard]] float Hue() const noexcept { return hue_; }
    [[nodiscard]] float Saturation() const noexcept { return saturation_; }
    [[nodiscard]] float Value() const noexcept { return value_; }

    [[nodiscard]] bool DragSaturationValue(float saturation, float value) noexcept {
        if (!IsFinite(saturation) || !IsFinite(value)) return false;
        saturation = Clamp01(saturation);
        value = Clamp01(value);
        const bool changed = saturation_ != saturation || value_ != value;
        saturation_ = saturation;
        value_ = value;
        return changed;
    }

    [[nodiscard]] bool DragHue(float hue) noexcept {
        if (!IsFinite(hue)) return false;
        hue = Clamp01(hue);
        const bool changed = hue_ != hue;
        hue_ = hue;
        return changed;
    }

    [[nodiscard]] std::string ChannelText(ColorChannel channel) const {
        const Vec4 c = Sample();
        float value = 0.0f;
        switch (channel) {
            case ColorChannel::Red: value = c.x; break;
            case ColorChannel::Green: value = c.y; break;
            case ColorChannel::Blue: value = c.z; break;
            case ColorChannel::Alpha: value = c.w; break;
        }
        char text[16];
        std::snprintf(text, sizeof(text), "%3d", static_cast<int>(std::round(Clamp01(value) * 255.0f)));
        return text;
    }

private:
    float hue_ = 0.0f;
    float saturation_ = 0.0f;
    float value_ = 1.0f;
    float alpha_ = 1.0f;
};

[[nodiscard]] inline ColorPickerLayout LayoutColorPicker(Rect bounds, const ColorPickerStyle& style) noexcept {
    const float gap = std::max(0.0f, style.gap);
    const float hueHeight = std::max(0.0f, style.hueHeight);
    const float fieldHeight = std::max(0.0f, style.fieldHeight);
    const float channelHeight = std::max(0.0f, style.channelHeight);
    const float svHeight = std::min(std::max(0.0f, style.saturationValueHeight),
                                    std::max(0.0f, bounds.size.y - hueHeight - fieldHeight - channelHeight - 3.0f * gap));
    ColorPickerLayout result{
        {bounds.position, {bounds.size.x, svHeight}},
        {{bounds.position.x, bounds.position.y + svHeight + gap}, {bounds.size.x, hueHeight}},
        {{bounds.position.x, bounds.position.y + svHeight + gap + hueHeight + gap}, {bounds.size.x, fieldHeight}},
        {}
    };
    const float channelY = result.hexField.position.y + result.hexField.size.y + gap;
    const float channelGap = 4.0f;
    const float channelWidth = std::max(0.0f, (bounds.size.x - 3.0f * channelGap) * 0.25f);
    for (std::size_t i = 0; i < result.channelFields.size(); ++i) {
        result.channelFields[i] = {{bounds.position.x + static_cast<float>(i) * (channelWidth + channelGap), channelY},
                                   {channelWidth, channelHeight}};
    }
    return result;
}

[[nodiscard]] inline std::vector<Rendering::UIRenderItem> BuildColorPickerItems(
    const ColorPickerControl& control,
    const ColorPickerLayout& layout,
    UIHandle owner,
    const ColorPickerStyle&,
    float zOrder,
    std::uint64_t paintOrder)
{
    std::vector<Rendering::UIRenderItem> items;
    const auto solid = [&](Rect bounds, Vec4 tint, std::uint64_t paint) {
        Rendering::UIRenderItem item;
        item.handle = owner;
        item.layout.resolvedPosition = bounds.position;
        item.layout.resolvedSize = bounds.size;
        item.tintPremultipliedLinear = tint;
        item.textureSlice = Rendering::WhiteTextureSlice;
        item.zOrder = zOrder;
        item.paintOrder = paint;
        return item;
    };
    if (layout.saturationValue.size.x > 0.0f && layout.saturationValue.size.y > 0.0f) {
        constexpr std::size_t columns = 32;
        constexpr std::size_t rows = 16;
        for (std::size_t row = 0; row < rows; ++row) {
            const float top = layout.saturationValue.position.y +
                              layout.saturationValue.size.y * static_cast<float>(row) / static_cast<float>(rows);
            const float bottom = layout.saturationValue.position.y +
                                 layout.saturationValue.size.y * static_cast<float>(row + 1) / static_cast<float>(rows);
            const float value = 1.0f - (static_cast<float>(row) + 0.5f) / static_cast<float>(rows);
            for (std::size_t column = 0; column < columns; ++column) {
                const float left = layout.saturationValue.position.x +
                                   layout.saturationValue.size.x * static_cast<float>(column) / static_cast<float>(columns);
                const float right = layout.saturationValue.position.x +
                                    layout.saturationValue.size.x * static_cast<float>(column + 1) / static_cast<float>(columns);
                const float saturation = (static_cast<float>(column) + 0.5f) / static_cast<float>(columns);
                items.push_back(solid({{left, top}, {right - left, bottom - top}},
                                      control.SampleAt(saturation, value), paintOrder++));
            }
        }
        const float markerX = layout.saturationValue.position.x + layout.saturationValue.size.x * control.Saturation();
        const float markerY = layout.saturationValue.position.y + layout.saturationValue.size.y * (1.0f - control.Value());
        const float markerSize = 5.0f;
        items.push_back(solid({{markerX - markerSize, markerY}, {markerSize * 2.0f, 1.0f}}, {0, 0, 0, 1}, paintOrder++));
        items.push_back(solid({{markerX - markerSize + 1.0f, markerY + 1.0f}, {markerSize * 2.0f - 2.0f, 1.0f}},
                              {1, 1, 1, 1}, paintOrder++));
    }
    if (layout.hue.size.x > 0.0f && layout.hue.size.y > 0.0f) {
        constexpr std::size_t segments = 24;
        for (std::size_t i = 0; i < segments; ++i) {
            const float x = layout.hue.position.x + layout.hue.size.x * static_cast<float>(i) / static_cast<float>(segments);
            const float next = layout.hue.position.x + layout.hue.size.x * static_cast<float>(i + 1) / static_cast<float>(segments);
            const float h = static_cast<float>(i) / static_cast<float>(segments);
            ColorPickerControl hueOnly;
            (void)hueOnly.SetColor({1, 0, 0, 1});
            (void)hueOnly.DragHue(h);
            items.push_back(solid({{x, layout.hue.position.y}, {next - x, layout.hue.size.y}}, hueOnly.Sample(), paintOrder++));
        }
        const float hueX = layout.hue.position.x + layout.hue.size.x * control.Hue();
        items.push_back(solid({{hueX - 1.0f, layout.hue.position.y - 1.0f}, {2.0f, layout.hue.size.y + 2.0f}},
                              {1, 1, 1, 1}, paintOrder++));
    }
    for (const Rect& field : layout.channelFields) {
        if (field.size.x > 0.0f && field.size.y > 0.0f) {
            items.push_back(solid(field, {0.09f, 0.11f, 0.15f, 1.0f}, paintOrder++));
        }
    }
    return items;
}

} // namespace ToolEngine::SpriteUi::Widgets
