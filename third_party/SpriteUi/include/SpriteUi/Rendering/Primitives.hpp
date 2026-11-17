#pragma once

// Primitivas de desenho: formas prontas como UIRenderItem, para os widgets e para grades e gizmos. Funcoes
// puras: recebem geometria e cor e devolvem o item que o RenderExtractor aceita. Cores em RGBA linear
// premultiplicado (mesmo formato de tintPremultipliedLinear).

#include "SpriteUi/Core/Handle.hpp"
#include "SpriteUi/Core/Math.hpp"
#include "SpriteUi/Rendering/DrawList.hpp"
#include "SpriteUi/Rendering/RenderFlags.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace ToolEngine::SpriteUi::Rendering {

// Retangulo de cor solida, sem textura.
[[nodiscard]] inline UIRenderItem MakeSolidRect(UIHandle owner, Rect rect, Vec4 colorPremultipliedLinear,
                                                std::uint32_t clipIndex = NoClipIndex, float zOrder = 0.0f,
                                                std::uint64_t paintOrder = 0) noexcept {
    UIRenderItem item;
    item.handle = owner;
    item.layout.resolvedPosition = rect.position;
    item.layout.resolvedSize = rect.size;
    item.tintPremultipliedLinear = colorPremultipliedLinear;
    item.textureSlice = WhiteTextureSlice;
    item.clipIndex = clipIndex;
    item.zOrder = zOrder;
    item.paintOrder = paintOrder;
    return item;
}

// Linha de from a to, com espessura em pixels, pintada como um retangulo girado. O pivo fica no meio da ponta
// inicial, entao o item comeca em from e aponta para to. Sem geometria valida (pontos iguais, espessura nao
// positiva ou valores nao finitos) nao ha item.
[[nodiscard]] inline std::optional<UIRenderItem>
MakeLine(UIHandle owner, Vec2 from, Vec2 to, float thickness, Vec4 colorPremultipliedLinear,
         std::uint32_t clipIndex = NoClipIndex, float zOrder = 0.0f, std::uint64_t paintOrder = 0) noexcept {
    if (!IsFinite(from) || !IsFinite(to) || !IsFinite(thickness) || thickness <= 0.0f) return std::nullopt;
    const Vec2 delta = to - from;
    const float length = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    if (!IsFinite(length) || length <= 0.0f) return std::nullopt;

    UIRenderItem item = MakeSolidRect(owner, {}, colorPremultipliedLinear, clipIndex, zOrder, paintOrder);
    item.layout.resolvedSize = {length, thickness};
    // Com pivo (0, 0.5) o ponto de origem do retangulo e resolvedPosition + (0, thickness / 2): desloca-se
    // para que esse ponto caia exatamente em from.
    item.layout.resolvedPosition = {from.x, from.y - 0.5f * thickness};
    item.transform.pivot = {0.0f, 0.5f};
    item.transform.rotationRadians = std::atan2(delta.y, delta.x);
    return item;
}

// Retangulo de cantos arredondados, pelo SDF analitico do shader (FlagRoundedPanel). O raio e prendido entre
// zero e metade do menor lado; raio nao finito vale zero.
[[nodiscard]] inline std::optional<UIRenderItem> MakeRoundedRect(UIHandle owner, Rect rect, float radius,
                                                                 Vec4 colorPremultipliedLinear,
                                                                 std::uint32_t clipIndex = NoClipIndex,
                                                                 float zOrder = 0.0f,
                                                                 std::uint64_t paintOrder = 0) noexcept {
    if (!IsFinite(rect.position) || !IsFinite(rect.size) || rect.size.x <= 0.0f || rect.size.y <= 0.0f) {
        return std::nullopt;
    }
    const float limit = 0.5f * std::min(rect.size.x, rect.size.y);
    const float clamped = IsFinite(radius) ? std::clamp(radius, 0.0f, limit) : 0.0f;

    UIRenderItem item = MakeSolidRect(owner, rect, colorPremultipliedLinear, clipIndex, zOrder, paintOrder);
    item.flags = FlagRoundedPanel;
    item.cornerRadius = clamped;
    return item;
}

} // namespace ToolEngine::SpriteUi::Rendering
