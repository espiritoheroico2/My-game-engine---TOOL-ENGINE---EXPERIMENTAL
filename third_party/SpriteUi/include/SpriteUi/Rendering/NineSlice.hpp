#pragma once

#include "SpriteUi/Core/Math.hpp"
#include "SpriteUi/Rendering/DrawList.hpp"

#include <array>
#include <cstddef>
#include <vector>

namespace ToolEngine::SpriteUi::Rendering {

// Dynamic nine-slice (specification V4.3, section 7).
//
// The source image is cut into 3x3 regions by four borders measured in SOURCE TEXELS. The corners keep
// their size, the edges stretch along one axis and the centre stretches along both. When the target is
// smaller than the fixed borders (W < L+R or H < T+B) the corners would overlap, so the borders are
// attenuated proportionally:
//
//     kx = min(1, W / (L + R))      ky = min(1, H / (T + B))
//     L' = L*kx   R' = R*kx         T' = T*ky   B' = B*ky
//
// Every function is total: non-finite, negative or oversized inputs are sanitized, never rejected.
struct NineSliceBorders {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

struct NineSliceQuad {
    Rect rect{};                          // Relative to the target's top-left, in target (logical) pixels.
    float uv[4]{0.0f, 0.0f, 0.0f, 0.0f};  // u, v, width, height inside the SPRITE (0..1), not the atlas.
    bool valid = false;                   // False when the region has no area or no texels to sample.
};

struct NineSliceLayout {
    // Row-major: TL, T, TR, L, C, R, BL, B, BR.
    std::array<NineSliceQuad, 9> quads{};
    float kx = 1.0f; // Attenuation factors actually applied (1 = borders keep their size).
    float ky = 1.0f;

    [[nodiscard]] std::size_t ValidCount() const noexcept;
};

// textureSize: sprite size in texels. borders: in texels. targetSize: logical pixels.
// borderScale: logical pixels per border texel (1 = 1:1; 0.5 for art authored at 2x density).
[[nodiscard]] NineSliceLayout ComputeNineSlice(
    Vec2 textureSize,
    const NineSliceBorders& borders,
    Vec2 targetSize,
    float borderScale = 1.0f) noexcept;

// Appends one item per valid region, copying `prototype` (tint, slice, clip, layer, paintOrder...).
// The prototype's layout rect is the target; its uvRect is the sprite's rect inside the atlas page and
// is subdivided accordingly. Only axis-aligned prototypes are supported (rotation 0, scale 1): for any
// other transform nothing is appended and 0 is returned. Returns the number of items appended.
std::size_t AppendNineSliceItems(
    std::vector<UIRenderItem>& out,
    const UIRenderItem& prototype,
    const NineSliceLayout& layout);

} // namespace ToolEngine::SpriteUi::Rendering
