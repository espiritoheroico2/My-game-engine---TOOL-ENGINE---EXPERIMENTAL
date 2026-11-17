#include "SpriteUi/Widgets/ColorPicker.hpp"

#include "SpriteUi/Elements/Paint.hpp"
#include "SpriteUi/Rendering/Primitives.hpp"
#include "SpriteUi/Rendering/SpriteInstanceGPU.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>

namespace ToolEngine::SpriteUi::Widgets {

namespace {

constexpr int kMaxChannel = 255;
// Teto para o acumulo de digitos: evita estouro com textos muito longos. Qualquer valor acima ja e prendido.
constexpr long long kDigitsLimit = 1000000000LL;

struct Rgb {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};

[[nodiscard]] std::size_t Index(ColorChannel channel) noexcept {
    return static_cast<std::size_t>(channel);
}

[[nodiscard]] float SanitizeUnit(float value) noexcept {
    return IsFinite(value) ? Clamp01(value) : 0.0f; // NaN nao pode chegar ao cast para byte.
}

[[nodiscard]] std::uint8_t ToByte(float unit) noexcept {
    return static_cast<std::uint8_t>(std::round(SanitizeUnit(unit) * static_cast<float>(kMaxChannel)));
}

// Matiz em [0, 1): valores fora da faixa dao a volta.
[[nodiscard]] float WrapHue(float hue) noexcept {
    const float wrapped = hue - std::floor(hue);
    return wrapped >= 1.0f ? 0.0f : wrapped;
}

[[nodiscard]] float ComponentOf(Rgb color, ColorChannel channel) noexcept {
    switch (channel) {
    case ColorChannel::Red: return color.r;
    case ColorChannel::Green: return color.g;
    default: return color.b;
    }
}

[[nodiscard]] Rgb HsvToRgb(float hue, float saturation, float value) noexcept {
    const float sector6 = WrapHue(hue) * 6.0f;
    const float sector = std::floor(sector6);
    const float fraction = sector6 - sector;
    const float low = value * (1.0f - saturation);
    const float falling = value * (1.0f - fraction * saturation);
    const float rising = value * (1.0f - (1.0f - fraction) * saturation);
    switch (static_cast<int>(sector)) {
    case 0: return {value, rising, low};
    case 1: return {falling, value, low};
    case 2: return {low, value, rising};
    case 3: return {low, falling, value};
    case 4: return {rising, low, value};
    default: return {value, low, falling};
    }
}

// Cinza nao tem matiz: nesse caso o matiz anterior e mantido, para a faixa nao pular.
[[nodiscard]] float HueOf(Rgb color, float previousHue) noexcept {
    const float highest = std::max({color.r, color.g, color.b});
    const float delta = highest - std::min({color.r, color.g, color.b});
    if (delta <= 0.0f) return previousHue;
    float sector = 0.0f;
    if (highest == color.r) {
        sector = (color.g - color.b) / delta;
    } else if (highest == color.g) {
        sector = (color.b - color.r) / delta + 2.0f;
    } else {
        sector = (color.r - color.g) / delta + 4.0f;
    }
    return WrapHue(sector / 6.0f);
}

// Converte RGB (0 a 1) para o espaco de edicao. O matiz anterior vale quando a cor e cinza.
[[nodiscard]] HsvColor ToHsv(Rgb color, float previousHue) noexcept {
    const float highest = std::max({color.r, color.g, color.b});
    const float lowest = std::min({color.r, color.g, color.b});
    const float saturation = highest > 0.0f ? (highest - lowest) / highest : 0.0f;
    return {HueOf(color, previousHue), saturation, highest};
}

[[nodiscard]] Rgb UnitRgbFromBytes(const std::array<std::uint8_t, 3>& bytes) noexcept {
    return {bytes[0] / static_cast<float>(kMaxChannel), bytes[1] / static_cast<float>(kMaxChannel),
            bytes[2] / static_cast<float>(kMaxChannel)};
}

[[nodiscard]] int HexDigitValue(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

struct HexColor {
    std::array<std::uint8_t, 3> rgb{};
    std::uint8_t alpha = 255;
};

// "#RRGGBB" ou "#RRGGBBAA". Qualquer outro formato e recusado.
[[nodiscard]] std::optional<HexColor> ParseHex(std::string_view text) noexcept {
    if (text.size() != 7 && text.size() != 9) return std::nullopt;
    if (text.front() != '#') return std::nullopt;

    std::array<std::uint8_t, 4> bytes{255, 255, 255, 255};
    const std::size_t pairCount = (text.size() - 1) / 2;
    for (std::size_t pair = 0; pair < pairCount; ++pair) {
        const int high = HexDigitValue(text[1 + 2 * pair]);
        const int low = HexDigitValue(text[2 + 2 * pair]);
        if (high < 0 || low < 0) return std::nullopt;
        bytes[pair] = static_cast<std::uint8_t>(high * 16 + low);
    }
    return HexColor{{bytes[0], bytes[1], bytes[2]}, bytes[3]};
}

// Inteiro com sinal opcional, sem espacos internos. Vazio, so sinal ou com outro caractere: nao e numero.
[[nodiscard]] std::optional<int> ParseChannelText(std::string_view text) noexcept {
    while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
    while (!text.empty() && text.back() == ' ') text.remove_suffix(1);

    const bool negative = !text.empty() && text.front() == '-';
    if (negative) text.remove_prefix(1);
    if (text.empty()) return std::nullopt;

    long long value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return std::nullopt;
        value = std::min(value * 10 + (c - '0'), kDigitsLimit);
    }
    return static_cast<int>(negative ? -value : value);
}

} // namespace

ColorPickerControl::ColorPickerControl(Vec4 initial) noexcept {
    const Rgb rgb{SanitizeUnit(initial.x), SanitizeUnit(initial.y), SanitizeUnit(initial.z)};
    hsv_ = ToHsv(rgb, 0.0f);
    alpha_ = SanitizeUnit(initial.w);
    original_ = current();
}

std::uint8_t ColorPickerControl::Channel(ColorChannel channel) const noexcept {
    if (channel == ColorChannel::Alpha) return ToByte(alpha_);
    const Rgb rgb = HsvToRgb(hsv_.hue, hsv_.saturation, hsv_.value);
    return ToByte(ComponentOf(rgb, channel));
}

std::string ColorPickerControl::ChannelText(ColorChannel channel) const {
    return std::to_string(Channel(channel));
}

std::string ColorPickerControl::HexText() const {
    static constexpr char kDigits[] = "0123456789ABCDEF";
    std::string text = "#";
    for (const ColorChannel channel : {ColorChannel::Red, ColorChannel::Green, ColorChannel::Blue,
                                       ColorChannel::Alpha}) {
        const std::uint8_t byte = Channel(channel);
        text.push_back(kDigits[byte >> 4]);
        text.push_back(kDigits[byte & 0x0F]);
    }
    return text;
}

Vec4 ColorPickerControl::Sample() const noexcept {
    const Rgb rgb = HsvToRgb(hsv_.hue, hsv_.saturation, hsv_.value);
    return {rgb.r, rgb.g, rgb.b, alpha_};
}

Vec4 ColorPickerControl::Original() const noexcept {
    const Rgb rgb = HsvToRgb(original_.hsv.hue, original_.hsv.saturation, original_.hsv.value);
    return {rgb.r, rgb.g, rgb.b, original_.alpha};
}

bool ColorPickerControl::SetChannel(ColorChannel channel, int value) noexcept {
    const auto byte = static_cast<std::uint8_t>(std::clamp(value, 0, kMaxChannel));
    if (channel == ColorChannel::Alpha) return SetAlpha(static_cast<float>(byte) / kMaxChannel);

    std::array<std::uint8_t, 3> bytes{Channel(ColorChannel::Red), Channel(ColorChannel::Green),
                                      Channel(ColorChannel::Blue)};
    if (bytes[Index(channel)] == byte) return false;
    bytes[Index(channel)] = byte;
    return SetHsv(ToHsv(UnitRgbFromBytes(bytes), hsv_.hue));
}

bool ColorPickerControl::SetChannelText(ColorChannel channel, std::string_view text) noexcept {
    if (disabled) return false;
    const std::optional<int> value = ParseChannelText(text);
    return value.has_value() && SetChannel(channel, *value);
}

bool ColorPickerControl::Step(ColorChannel channel, int delta) noexcept {
    if (disabled) return false;
    return SetChannel(channel, static_cast<int>(Channel(channel)) + delta);
}

bool ColorPickerControl::SetHex(std::string_view text) noexcept {
    if (disabled) return false;
    const std::optional<HexColor> parsed = ParseHex(text);
    if (!parsed) return false;
    const bool colorChanged = SetHsv(ToHsv(UnitRgbFromBytes(parsed->rgb), hsv_.hue));
    const bool alphaChanged = SetAlpha(static_cast<float>(parsed->alpha) / kMaxChannel);
    return colorChanged || alphaChanged;
}

bool ColorPickerControl::DragHue(float hue) noexcept {
    if (disabled || !IsFinite(hue)) return false;
    return SetHsv({WrapHue(hue), hsv_.saturation, hsv_.value});
}

bool ColorPickerControl::DragSaturationValue(float saturation, float value) noexcept {
    if (disabled || !IsFinite(saturation) || !IsFinite(value)) return false;
    return SetHsv({hsv_.hue, Clamp01(saturation), Clamp01(value)});
}

bool ColorPickerControl::SetAlpha(float alpha) noexcept {
    if (!IsFinite(alpha)) return false;
    const float next = Clamp01(alpha);
    if (next == alpha_) return false;
    alpha_ = next;
    return true;
}

bool ColorPickerControl::SetColor(Vec4 color) noexcept {
    if (!IsFinite(color)) return false;
    const Rgb rgb{Clamp01(color.x), Clamp01(color.y), Clamp01(color.z)};
    const bool colorChanged = SetHsv(ToHsv(rgb, hsv_.hue));
    const bool alphaChanged = SetAlpha(color.w);
    return colorChanged || alphaChanged;
}

bool ColorPickerControl::Revert() noexcept {
    if (current() == original_) return false;
    hsv_ = original_.hsv;
    alpha_ = original_.alpha;
    return true;
}

bool ColorPickerControl::SetHsv(HsvColor next) noexcept {
    if (next == hsv_) return false;
    hsv_ = next;
    return true;
}

ColorPickerLayout LayoutColorPicker(Rect bounds, const ColorPickerStyle& style) noexcept {
    ColorPickerLayout layout;
    const float x = bounds.position.x;
    const float width = std::max(0.0f, bounds.size.x);
    const float gap = style.gap;
    // Quatro vaos (antes do matiz, do alfa, da linha HEX e da linha RGBA) e as duas faixas e as duas linhas de campos.
    const float fixedHeight = 2.0f * style.stripHeight + 2.0f * style.fieldHeight + 4.0f * gap;
    const float areaHeight = std::max(0.0f, bounds.size.y - fixedHeight);

    float y = bounds.position.y;
    layout.saturationValue = {{x, y}, {width, areaHeight}};
    y += areaHeight + gap;
    layout.hue = {{x, y}, {width, style.stripHeight}};
    y += style.stripHeight + gap;
    layout.alpha = {{x, y}, {width, style.stripHeight}};
    y += style.stripHeight + gap;
    layout.sampleOriginal = {{x, y}, {style.sampleWidth, style.fieldHeight}};
    layout.sampleCurrent = {{x + style.sampleWidth, y}, {style.sampleWidth, style.fieldHeight}};
    const float hexX = x + 2.0f * style.sampleWidth + gap;
    layout.hexField = {{hexX, y}, {std::max(0.0f, x + width - hexX), style.fieldHeight}};
    y += style.fieldHeight + gap;
    const float channelWidth = std::max(0.0f, (width - 3.0f * gap) / 4.0f);
    for (std::size_t channel = 0; channel < layout.channelFields.size(); ++channel) {
        layout.channelFields[channel] = {{x + static_cast<float>(channel) * (channelWidth + gap), y},
                                         {channelWidth, style.fieldHeight}};
    }
    return layout;
}

Vec2 SaturationValueMarkerCenter(const ColorPickerControl& picker, const ColorPickerLayout& layout) noexcept {
    const Rect& area = layout.saturationValue;
    return {area.position.x + picker.Saturation() * area.size.x,
            area.position.y + (1.0f - picker.Value()) * area.size.y};
}

Vec2 HueMarkerCenter(const ColorPickerControl& picker, const ColorPickerLayout& layout) noexcept {
    return {layout.hue.position.x + picker.Hue() * layout.hue.size.x, layout.hue.position.y + 0.5f * layout.hue.size.y};
}

Vec2 AlphaMarkerCenter(const ColorPickerControl& picker, const ColorPickerLayout& layout) noexcept {
    const Vec4 sample = picker.Sample();
    return {layout.alpha.position.x + sample.w * layout.alpha.size.x, layout.alpha.position.y + 0.5f * layout.alpha.size.y};
}

using Elements::AppendBox;
using Elements::AppendOutlineItems;
using Elements::GrowRect;

namespace {

constexpr int kSaturationColumns = 32;
constexpr int kSaturationRows = 24;
constexpr int kHueSegments = 36;
constexpr int kAlphaSegments = 32;

// Cores do seletor (da amostra e do estilo) sao sRGB sem premultiplicar; a biblioteca quer linear premultiplicado.
[[nodiscard]] Vec4 ItemColor(Vec4 srgbStraight) noexcept {
    return Rendering::SrgbStraightToLinearPremultiplied(srgbStraight);
}

// Retangulo de cor solida com o proximo paintOrder. Retangulo vazio ou com geometria invalida nao gera item.
void AddSolid(std::vector<Rendering::UIRenderItem>& out, UIHandle owner, Rect rect, Vec4 srgbStraight, float zOrder,
              std::uint64_t& paint) {
    if (!IsFinite(rect.position) || !IsFinite(rect.size) || rect.size.x <= 0.0f || rect.size.y <= 0.0f) return;
    out.push_back(Rendering::MakeSolidRect(owner, rect, ItemColor(srgbStraight), Rendering::NoClipIndex, zOrder,
                                           paint++));
}

// Fundo quadriculado: base clara com celulas escuras alternadas, para a transparencia aparecer.
void AddChecker(std::vector<Rendering::UIRenderItem>& out, UIHandle owner, Rect rect, const ColorPickerStyle& style,
                float zOrder, std::uint64_t& paint) {
    AddSolid(out, owner, rect, style.checkerLight, zOrder, paint);
    const float size = style.checkerSize;
    const float right = rect.position.x + rect.size.x;
    const float bottom = rect.position.y + rect.size.y;
    for (int row = 0;; ++row) {
        const float top = rect.position.y + static_cast<float>(row) * size;
        if (top >= bottom) break;
        for (int col = row % 2 == 0 ? 1 : 0;; col += 2) {
            const float left = rect.position.x + static_cast<float>(col) * size;
            if (left >= right) break;
            const Rect cell{{left, top}, {std::min(size, right - left), std::min(size, bottom - top)}};
            AddSolid(out, owner, cell, style.checkerDark, zOrder, paint);
        }
    }
}

// Area 2D: uma celula por par (saturacao, valor), com o matiz atual. Valor cai de cima para baixo.
void AddSaturationValueArea(std::vector<Rendering::UIRenderItem>& out, UIHandle owner, Rect area, float hue,
                            float zOrder, std::uint64_t& paint) {
    const float cellWidth = area.size.x / kSaturationColumns;
    const float cellHeight = area.size.y / kSaturationRows;
    for (int row = 0; row < kSaturationRows; ++row) {
        for (int col = 0; col < kSaturationColumns; ++col) {
            // Cada celula invade 1 px da vizinha: sem isso, a frincha entre elas aparece como linha escura.
            const Rect cell{{area.position.x + static_cast<float>(col) * cellWidth,
                             area.position.y + static_cast<float>(row) * cellHeight},
                            {cellWidth + 1.0f, cellHeight + 1.0f}};
            const float saturation = (static_cast<float>(col) + 0.5f) / kSaturationColumns;
            const float value = 1.0f - (static_cast<float>(row) + 0.5f) / kSaturationRows;
            const Rgb rgb = HsvToRgb(hue, saturation, value);
            AddSolid(out, owner, cell, {rgb.r, rgb.g, rgb.b, 1.0f}, zOrder, paint);
        }
    }
}

// Faixa de matiz: segmentos opacos de vermelho a vermelho, dando a volta completa.
void AddHueStrip(std::vector<Rendering::UIRenderItem>& out, UIHandle owner, Rect strip, float zOrder,
                 std::uint64_t& paint) {
    const float segmentWidth = strip.size.x / kHueSegments;
    for (int segment = 0; segment < kHueSegments; ++segment) {
        const Rect cell{{strip.position.x + static_cast<float>(segment) * segmentWidth, strip.position.y},
                        {segmentWidth, strip.size.y}};
        const Rgb rgb = HsvToRgb((static_cast<float>(segment) + 0.5f) / kHueSegments, 1.0f, 1.0f);
        AddSolid(out, owner, cell, {rgb.r, rgb.g, rgb.b, 1.0f}, zOrder, paint);
    }
}

// Faixa de alfa: sobre o quadriculado, segmentos da cor atual de transparente (esquerda) a opaco (direita).
void AddAlphaStrip(std::vector<Rendering::UIRenderItem>& out, UIHandle owner, Rect strip, Vec4 sample,
                   const ColorPickerStyle& style, float zOrder, std::uint64_t& paint) {
    AddChecker(out, owner, strip, style, zOrder, paint);
    const float segmentWidth = strip.size.x / kAlphaSegments;
    for (int segment = 0; segment < kAlphaSegments; ++segment) {
        const float alpha = (static_cast<float>(segment) + 0.5f) / kAlphaSegments;
        const Rect cell{{strip.position.x + static_cast<float>(segment) * segmentWidth, strip.position.y},
                        {segmentWidth, strip.size.y}};
        AddSolid(out, owner, cell, {sample.x, sample.y, sample.z, alpha}, zOrder, paint);
    }
}

// Original a esquerda e atual a direita, cada uma sobre o quadriculado.
void AddSamples(std::vector<Rendering::UIRenderItem>& out, UIHandle owner, const ColorPickerLayout& layout,
                const ColorPickerControl& picker, const ColorPickerStyle& style, float zOrder, std::uint64_t& paint) {
    AddChecker(out, owner, layout.sampleOriginal, style, zOrder, paint);
    AddSolid(out, owner, layout.sampleOriginal, picker.Original(), zOrder, paint);
    AddChecker(out, owner, layout.sampleCurrent, style, zOrder, paint);
    AddSolid(out, owner, layout.sampleCurrent, picker.Sample(), zOrder, paint);
}

// Fundo e borda do campo HEX e dos quatro campos RGBA. O texto e desenhado pelo chamador.
void AddFields(std::vector<Rendering::UIRenderItem>& out, UIHandle owner, const ColorPickerLayout& layout,
               const ColorPickerStyle& style, float zOrder, std::uint64_t& paint) {
    const Vec4 fill = ItemColor(style.fieldFill);
    const Vec4 border = ItemColor(style.fieldBorder);
    AppendBox(owner, layout.hexField, 0.0f, 1.0f, fill, border, Rendering::NoClipIndex, zOrder, paint, out);
    for (const Rect& field : layout.channelFields) {
        AppendBox(owner, field, 0.0f, 1.0f, fill, border, Rendering::NoClipIndex, zOrder, paint, out);
    }
}

// Contorno escuro por fora e miolo claro por dentro: o marcador aparece em qualquer cor de fundo.
void AddMarkerBox(std::vector<Rendering::UIRenderItem>& out, UIHandle owner, Rect outer,
                  const ColorPickerStyle& style, float zOrder, std::uint64_t& paint) {
    AppendOutlineItems(owner, outer, style.markerBorder, ItemColor(style.markerEdge), zOrder, paint, out);
    AppendOutlineItems(owner, GrowRect(outer, -style.markerBorder), style.markerBorder,
                       ItemColor(style.markerFill), zOrder, paint, out);
}

// Marcador quadrado na area 2D.
void AddSquareMarker(std::vector<Rendering::UIRenderItem>& out, UIHandle owner, Vec2 center,
                     const ColorPickerStyle& style, float zOrder, std::uint64_t& paint) {
    const float half = 0.5f * style.markerSize;
    const Rect outer{{center.x - half, center.y - half}, {style.markerSize, style.markerSize}};
    AddMarkerBox(out, owner, outer, style, zOrder, paint);
}

// Marcador vertical que passa por cima de uma faixa (matiz ou alfa), um pouco maior que ela.
void AddStripMarker(std::vector<Rendering::UIRenderItem>& out, UIHandle owner, Vec2 center, Rect strip,
                    const ColorPickerStyle& style, float zOrder, std::uint64_t& paint) {
    const float half = 0.5f * style.markerSize;
    const Rect outer{{center.x - half, strip.position.y - style.markerBorder},
                     {style.markerSize, strip.size.y + 2.0f * style.markerBorder}};
    AddMarkerBox(out, owner, outer, style, zOrder, paint);
}

} // namespace

std::vector<Rendering::UIRenderItem> BuildColorPickerItems(const ColorPickerControl& picker,
                                                           const ColorPickerLayout& layout, UIHandle owner,
                                                           const ColorPickerStyle& style, float zOrder,
                                                           std::uint64_t paintOrder) {
    std::vector<Rendering::UIRenderItem> out;
    std::uint64_t paint = paintOrder;
    AddSaturationValueArea(out, owner, layout.saturationValue, picker.Hue(), zOrder, paint);
    AddHueStrip(out, owner, layout.hue, zOrder, paint);
    AddAlphaStrip(out, owner, layout.alpha, picker.Sample(), style, zOrder, paint);
    AddSamples(out, owner, layout, picker, style, zOrder, paint);
    AddFields(out, owner, layout, style, zOrder, paint);
    AddSquareMarker(out, owner, SaturationValueMarkerCenter(picker, layout), style, zOrder, paint);
    AddStripMarker(out, owner, HueMarkerCenter(picker, layout), layout.hue, style, zOrder, paint);
    AddStripMarker(out, owner, AlphaMarkerCenter(picker, layout), layout.alpha, style, zOrder, paint);
    return out;
}

} // namespace ToolEngine::SpriteUi::Widgets
