#include "SpriteUi/Elements/Image.hpp"

#include "SpriteUi/Elements/Paint.hpp"
#include "SpriteUi/Rendering/Primitives.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace ToolEngine::SpriteUi::Elements {

namespace {

bool Positive(float value) noexcept {
    return std::isfinite(value) && value > 0.0f;
}

// Regiao UV em [0, 1]: u e v dentro da textura; largura e altura nao saem dela. Valores nao finitos viram o padrao.
std::array<float, 4> ClampUv(const float uv[4]) noexcept {
    const auto finiteOr = [](float value, float fallback) { return std::isfinite(value) ? value : fallback; };
    const float u = std::clamp(finiteOr(uv[0], 0.0f), 0.0f, 1.0f);
    const float v = std::clamp(finiteOr(uv[1], 0.0f), 0.0f, 1.0f);
    const float w = std::clamp(finiteOr(uv[2], 1.0f), 0.0f, 1.0f - u);
    const float h = std::clamp(finiteOr(uv[3], 1.0f), 0.0f, 1.0f - v);
    return {u, v, w, h};
}

Vec4 Scaled(Vec4 color, float factor) noexcept {
    return {color.x * factor, color.y * factor, color.z * factor, color.w * factor};
}

// Marcador de erro: fundo magenta com um X. Aparece quando a textura nao esta carregada.
std::vector<Rendering::UIRenderItem> MissingItems(UIHandle owner, Rect content, const ImageStyle& style,
                                                  std::uint32_t clipIndex, float zOrder, std::uint64_t& paint) {
    std::vector<Rendering::UIRenderItem> out;
    out.push_back(Rendering::MakeSolidRect(owner, content, style.missingFill, clipIndex, zOrder, paint++));
    const Vec2 topLeft = content.position;
    const Vec2 topRight{content.position.x + content.size.x, content.position.y};
    const Vec2 bottomLeft{content.position.x, content.position.y + content.size.y};
    const Vec2 bottomRight{content.position.x + content.size.x, content.position.y + content.size.y};
    for (const auto& [from, to] : {std::pair{topLeft, bottomRight}, std::pair{topRight, bottomLeft}}) {
        if (auto line = Rendering::MakeLine(owner, from, to, style.markThickness, style.missingMark, clipIndex,
                                            zOrder, paint++)) {
            out.push_back(*line);
        }
    }
    return out;
}

} // namespace

Rect ImageDrawRect(Rect content, Vec2 textureSize, ImageFit fit) noexcept {
    if (fit != ImageFit::Contain || !Positive(textureSize.x) || !Positive(textureSize.y) ||
        !Positive(content.size.x) || !Positive(content.size.y)) {
        return content;
    }
    const float scale = std::min(content.size.x / textureSize.x, content.size.y / textureSize.y);
    const Vec2 size{textureSize.x * scale, textureSize.y * scale};
    return {{content.position.x + 0.5f * (content.size.x - size.x), content.position.y + 0.5f * (content.size.y - size.y)},
            size};
}

std::array<float, 4> ImageVisibleUv(Rect content, const ImageSource& source) noexcept {
    std::array<float, 4> uv = ClampUv(source.uv);
    if (source.fit != ImageFit::Fill || !Positive(source.textureSize.x) || !Positive(source.textureSize.y) ||
        !Positive(content.size.x) || !Positive(content.size.y)) {
        return uv;
    }
    const float textureAspect = source.textureSize.x / source.textureSize.y;
    const float targetAspect = content.size.x / content.size.y;
    if (textureAspect > targetAspect) {
        // Textura mais larga: corta nos lados, centrado.
        const float keep = targetAspect / textureAspect;
        uv[0] += uv[2] * (1.0f - keep) * 0.5f;
        uv[2] *= keep;
    } else {
        // Textura mais alta: corta em cima e embaixo, centrado.
        const float keep = textureAspect / targetAspect;
        uv[1] += uv[3] * (1.0f - keep) * 0.5f;
        uv[3] *= keep;
    }
    return uv;
}

std::vector<Rendering::UIRenderItem> BuildImageSourceItems(UIHandle owner, Rect content, const ImageSource& source,
                                                           Vec4 tint, const ImageStyle& style,
                                                           std::uint32_t clipIndex, float zOrder,
                                                           std::uint64_t paintOrder) {
    std::vector<Rendering::UIRenderItem> out;
    if (!IsFinite(content.position) || !IsFinite(content.size)) return out;
    std::uint64_t paint = paintOrder;
    if (!source.loaded) return MissingItems(owner, content, style, clipIndex, zOrder, paint);

    if (source.nineSlice) {
        const Rendering::NineSliceLayout layout =
            Rendering::ComputeNineSlice(source.textureSize, source.borders, content.size);
        Rendering::UIRenderItem prototype =
            Rendering::MakeSolidRect(owner, content, tint, clipIndex, zOrder, paint);
        prototype.textureSlice = source.textureSlice;
        const std::array<float, 4> uv = ClampUv(source.uv);
        std::copy(uv.begin(), uv.end(), prototype.uvRect);
        Rendering::AppendNineSliceItems(out, prototype, layout);
        return out;
    }

    const Rect target = ImageDrawRect(content, source.textureSize, source.fit);
    const std::array<float, 4> uv = ImageVisibleUv(content, source);
    Rendering::UIRenderItem item = Rendering::MakeSolidRect(owner, target, tint, clipIndex, zOrder, paint);
    item.textureSlice = source.textureSlice;
    std::copy(uv.begin(), uv.end(), item.uvRect);
    out.push_back(item);
    return out;
}

bool ImageButton::OnPointer(const PointerEvent& event) noexcept {
    if (states.disabled) return false;
    return gesture_.Apply(states, event).clicked;
}

bool ImageButton::KeyActivate() const noexcept {
    return !states.disabled && states.focused;
}

std::vector<Rendering::UIRenderItem> BuildImageItems(const Image& image, const ImageStyle& style,
                                                     std::uint32_t clipIndex, float zOrder,
                                                     std::uint64_t paintOrder) {
    return BuildImageSourceItems(image.handle, image.ContentRect(), image.source, image.tint, style, clipIndex,
                                 zOrder, paintOrder);
}

std::vector<Rendering::UIRenderItem> BuildImageButtonItems(const ImageButton& button, const ImageStyle& style,
                                                           std::uint32_t clipIndex, float zOrder,
                                                           std::uint64_t paintOrder) {
    Vec4 tint = button.tint;
    if (button.states.disabled) {
        tint = Scaled(tint, style.disabledTint);
    } else if (button.states.pressed) {
        tint = Scaled(tint, style.pressedTint);
    }
    std::vector<Rendering::UIRenderItem> out = BuildImageSourceItems(button.handle, button.ContentRect(), button.source,
                                                                     tint, style, clipIndex, zOrder, paintOrder);
    if (button.states.focused && !button.states.disabled && IsFinite(button.border.position)) {
        std::uint64_t paint = paintOrder + out.size() + 1;
        AppendOutlineItems(button.handle, button.border, style.focusRingWidth, style.focusRing, zOrder, paint, out);
    }
    return out;
}

} // namespace ToolEngine::SpriteUi::Elements
