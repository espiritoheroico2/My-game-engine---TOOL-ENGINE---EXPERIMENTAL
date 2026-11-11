#pragma once

#include "SpriteUi/Core/Math.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace ToolEngine::SpriteUi::Rendering {

struct alignas(16) SpriteInstanceGPU final {
    float transformRow0[4]{}; // m00, m01, tx, depth
    float transformRow1[4]{}; // m10, m11, ty, cornerRadius (px, FlagRoundedPanel only)
    float uvRect[4]{};        // u, v, width, height
    float tint[4]{};          // linear premultiplied RGBA
    std::uint32_t resourceIds[4]{}; // textureSlice, materialIndex, clipIndex, flags
};

static_assert(std::is_standard_layout_v<SpriteInstanceGPU>);
static_assert(std::is_trivially_copyable_v<SpriteInstanceGPU>);
static_assert(sizeof(SpriteInstanceGPU) == 80, "SpriteInstanceGPU ABI must remain 80 bytes");
static_assert(alignof(SpriteInstanceGPU) == 16);
static_assert(offsetof(SpriteInstanceGPU, transformRow0) == 0);
static_assert(offsetof(SpriteInstanceGPU, transformRow1) == 16);
static_assert(offsetof(SpriteInstanceGPU, uvRect) == 32);
static_assert(offsetof(SpriteInstanceGPU, tint) == 48);
static_assert(offsetof(SpriteInstanceGPU, resourceIds) == 64);

struct alignas(16) UIMaterialGPU {
    std::uint32_t textureIndicesAndFlags[4]{};
    float emissiveColorIntensity[4]{};
    float lightingParams[4]{};
};
static_assert(sizeof(UIMaterialGPU) == 48);

[[nodiscard]] inline Vec4 PremultiplyLinear(Vec4 straightLinear) noexcept {
    if (!IsFinite(straightLinear)) return {};
    const float alpha = Clamp01(straightLinear.w);
    return {
        straightLinear.x * alpha,
        straightLinear.y * alpha,
        straightLinear.z * alpha,
        alpha
    };
}

[[nodiscard]] inline float SrgbChannelToLinear(float value) noexcept {
    value = std::max(0.0f, value);
    return value <= 0.04045f ? value / 12.92f
                             : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

[[nodiscard]] inline Vec4 SrgbStraightToLinearPremultiplied(Vec4 srgb) noexcept {
    if (!IsFinite(srgb)) return {};
    return PremultiplyLinear({
        SrgbChannelToLinear(srgb.x),
        SrgbChannelToLinear(srgb.y),
        SrgbChannelToLinear(srgb.z),
        Clamp01(srgb.w)
    });
}

[[nodiscard]] inline bool IsValidPremultipliedTint(Vec4 tint, float epsilon = 1.0e-4f) noexcept {
    if (!IsFinite(tint) || tint.w < 0.0f || tint.w > 1.0f) return false;
    return tint.x >= -epsilon && tint.y >= -epsilon && tint.z >= -epsilon;
}

} // namespace ToolEngine::SpriteUi::Rendering
