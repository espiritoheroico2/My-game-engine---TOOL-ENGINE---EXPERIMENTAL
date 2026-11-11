#include "SpriteUi/Rendering/NineSlice.hpp"

#include <algorithm>
#include <cmath>

namespace ToolEngine::SpriteUi::Rendering {

namespace {

[[nodiscard]] float NonNegativeFinite(float v) noexcept {
    return (std::isfinite(v) && v > 0.0f) ? v : 0.0f;
}

} // namespace

std::size_t NineSliceLayout::ValidCount() const noexcept {
    std::size_t n = 0;
    for (const NineSliceQuad& q : quads) {
        if (q.valid) ++n;
    }
    return n;
}

NineSliceLayout ComputeNineSlice(
    Vec2 textureSize, const NineSliceBorders& borders, Vec2 targetSize, float borderScale) noexcept
{
    NineSliceLayout layout;
    const float texW = NonNegativeFinite(textureSize.x);
    const float texH = NonNegativeFinite(textureSize.y);
    if (texW <= 0.0f || texH <= 0.0f) return layout; // Nothing to sample: all quads stay invalid.

    const float scale = (std::isfinite(borderScale) && borderScale > 0.0f) ? borderScale : 1.0f;
    const float targetW = NonNegativeFinite(targetSize.x);
    const float targetH = NonNegativeFinite(targetSize.y);

    // Borders in texels, limited so that they never exceed the texture itself.
    float bl = NonNegativeFinite(borders.left);
    float br = NonNegativeFinite(borders.right);
    float bt = NonNegativeFinite(borders.top);
    float bb = NonNegativeFinite(borders.bottom);
    if (bl + br > texW) {
        const float f = texW / (bl + br);
        bl *= f;
        br *= f;
    }
    if (bt + bb > texH) {
        const float f = texH / (bt + bb);
        bt *= f;
        bb *= f;
    }

    // Border thickness on the target, then the attenuation factors of the specification.
    const float fixedW = (bl + br) * scale;
    const float fixedH = (bt + bb) * scale;
    layout.kx = fixedW > 0.0f ? std::min(1.0f, targetW / fixedW) : 1.0f;
    layout.ky = fixedH > 0.0f ? std::min(1.0f, targetH / fixedH) : 1.0f;

    const float L = bl * scale * layout.kx;
    const float R = br * scale * layout.kx;
    const float T = bt * scale * layout.ky;
    const float B = bb * scale * layout.ky;

    // Column / row boundaries on the target. The last boundary IS the target size, so the regions
    // tile it exactly (no accumulated rounding).
    const float xs[4] = {0.0f, L, std::max(L, targetW - R), targetW};
    const float ys[4] = {0.0f, T, std::max(T, targetH - B), targetH};
    // Matching boundaries on the sprite's UV space.
    const float us[4] = {0.0f, bl / texW, 1.0f - br / texW, 1.0f};
    const float vs[4] = {0.0f, bt / texH, 1.0f - bb / texH, 1.0f};

    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            NineSliceQuad& q = layout.quads[static_cast<std::size_t>(row * 3 + col)];
            q.rect = {{xs[col], ys[row]}, {xs[col + 1] - xs[col], ys[row + 1] - ys[row]}};
            q.uv[0] = us[col];
            q.uv[1] = vs[row];
            q.uv[2] = us[col + 1] - us[col];
            q.uv[3] = vs[row + 1] - vs[row];
            q.valid = q.rect.size.x > 0.0f && q.rect.size.y > 0.0f && q.uv[2] > 0.0f && q.uv[3] > 0.0f;
        }
    }
    return layout;
}

std::size_t AppendNineSliceItems(
    std::vector<UIRenderItem>& out, const UIRenderItem& prototype, const NineSliceLayout& layout)
{
    const UIRectTransform& t = prototype.transform;
    if (t.rotationRadians != 0.0f || t.scale.x != 1.0f || t.scale.y != 1.0f) return 0;

    std::size_t appended = 0;
    for (const NineSliceQuad& q : layout.quads) {
        if (!q.valid) continue;
        UIRenderItem item = prototype;
        item.layout.resolvedPosition = {
            prototype.layout.resolvedPosition.x + q.rect.position.x,
            prototype.layout.resolvedPosition.y + q.rect.position.y};
        item.layout.resolvedSize = q.rect.size;
        // Sub-rectangle of the sprite inside the prototype's (atlas) UV rect.
        item.uvRect[0] = prototype.uvRect[0] + q.uv[0] * prototype.uvRect[2];
        item.uvRect[1] = prototype.uvRect[1] + q.uv[1] * prototype.uvRect[3];
        item.uvRect[2] = q.uv[2] * prototype.uvRect[2];
        item.uvRect[3] = q.uv[3] * prototype.uvRect[3];
        out.push_back(item);
        ++appended;
    }
    return appended;
}

} // namespace ToolEngine::SpriteUi::Rendering
