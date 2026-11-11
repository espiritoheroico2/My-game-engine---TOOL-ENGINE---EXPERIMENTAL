#include "SpriteUi/SpriteUi.hpp"

#include "TestHarness.hpp"
#include "TestSuites.hpp"
#include "Visual/VisualHarness.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace SpriteUiTests {

namespace {

using namespace ToolEngine::SpriteUi;
using namespace ToolEngine::SpriteUi::Rendering;

constexpr Vec2 TextureSize{64.0f, 48.0f};
constexpr NineSliceBorders Borders{16.0f, 12.0f, 16.0f, 12.0f}; // left, top, right, bottom (texels)

float Area(const NineSliceLayout& layout) {
    float total = 0.0f;
    for (const NineSliceQuad& q : layout.quads) {
        if (q.valid) total += q.rect.size.x * q.rect.size.y;
    }
    return total;
}

bool Overlap(const Rect& a, const Rect& b) {
    const float w = std::min(a.position.x + a.size.x, b.position.x + b.size.x) - std::max(a.position.x, b.position.x);
    const float h = std::min(a.position.y + a.size.y, b.position.y + b.size.y) - std::max(a.position.y, b.position.y);
    return w > 1.0e-3f && h > 1.0e-3f;
}

void TestLargeTargetKeepsCornersAndStretchesTheRest() {
    const NineSliceLayout l = ComputeNineSlice(TextureSize, Borders, {200.0f, 100.0f});
    CHECK(Near(l.kx, 1.0f) && Near(l.ky, 1.0f));
    CHECK(l.ValidCount() == 9);
    // Corners keep 16x12; edges stretch along one axis; the centre along both.
    CHECK(Near(l.quads[0].rect.size.x, 16.0f) && Near(l.quads[0].rect.size.y, 12.0f)); // TL
    CHECK(Near(l.quads[2].rect.position.x, 184.0f) && Near(l.quads[2].rect.size.x, 16.0f)); // TR
    CHECK(Near(l.quads[6].rect.position.y, 88.0f));                                         // BL
    CHECK(Near(l.quads[1].rect.size.x, 168.0f) && Near(l.quads[1].rect.size.y, 12.0f));     // T
    CHECK(Near(l.quads[3].rect.size.x, 16.0f) && Near(l.quads[3].rect.size.y, 76.0f));      // L
    CHECK(Near(l.quads[4].rect.size.x, 168.0f) && Near(l.quads[4].rect.size.y, 76.0f));     // C
    CHECK(Near(Area(l), 200.0f * 100.0f));                                                  // exact tiling
    // UV: border regions use their texels; the centre uses the rest.
    CHECK(Near(l.quads[0].uv[2], 16.0f / 64.0f) && Near(l.quads[0].uv[3], 12.0f / 48.0f));
    CHECK(Near(l.quads[4].uv[0], 0.25f) && Near(l.quads[4].uv[2], 0.5f));
    CHECK(Near(l.quads[8].uv[0] + l.quads[8].uv[2], 1.0f) && Near(l.quads[8].uv[1] + l.quads[8].uv[3], 1.0f));
}

void TestAttenuationFollowsTheSpecificationFormula() {
    // W=20 < L+R=32 -> kx = 20/32; H=10 < T+B=24 -> ky = 10/24.
    const NineSliceLayout l = ComputeNineSlice(TextureSize, Borders, {20.0f, 10.0f});
    CHECK(Near(l.kx, 20.0f / 32.0f) && Near(l.ky, 10.0f / 24.0f));
    CHECK(Near(l.quads[0].rect.size.x, 16.0f * l.kx));       // L' = L * kx
    CHECK(Near(l.quads[2].rect.size.x, 16.0f * l.kx));       // R' = R * kx
    CHECK(Near(l.quads[0].rect.size.y, 12.0f * l.ky));       // T' = T * ky
    CHECK(Near(l.quads[6].rect.size.y, 12.0f * l.ky));       // B' = B * ky
    CHECK(!l.quads[4].valid && !l.quads[1].valid);           // no room left for the centre / edges
    CHECK(l.ValidCount() == 4);                              // only the four corners remain
    CHECK(Near(Area(l), 20.0f * 10.0f));                     // and they still tile the target exactly
    // The corners never overlap each other (the whole point of the attenuation).
    bool overlap = false;
    for (std::size_t a = 0; a < 9; ++a)
        for (std::size_t b = a + 1; b < 9; ++b)
            if (l.quads[a].valid && l.quads[b].valid && Overlap(l.quads[a].rect, l.quads[b].rect)) overlap = true;
    CHECK(!overlap);
    // Only one dimension too small: only that axis is attenuated.
    const NineSliceLayout wide = ComputeNineSlice(TextureSize, Borders, {200.0f, 12.0f});
    CHECK(Near(wide.kx, 1.0f) && Near(wide.ky, 0.5f));
    // Exactly the sum of the borders: factor 1 and a zero-width centre.
    const NineSliceLayout exact = ComputeNineSlice(TextureSize, Borders, {32.0f, 24.0f});
    CHECK(Near(exact.kx, 1.0f) && Near(exact.ky, 1.0f));
    CHECK(!exact.quads[4].valid);
}

void TestBorderScaleForHighDensityArt() {
    // Art authored at 2x: 1 border texel = 0.5 logical pixels.
    const NineSliceLayout l = ComputeNineSlice(TextureSize, Borders, {100.0f, 60.0f}, 0.5f);
    CHECK(Near(l.quads[0].rect.size.x, 8.0f) && Near(l.quads[0].rect.size.y, 6.0f));
    CHECK(Near(l.quads[0].uv[2], 16.0f / 64.0f));            // the UV still covers the 16 texels
    CHECK(Near(Area(l), 6000.0f));
    // The attenuation threshold moves with the scale: 16 logical px is now exactly L+R.
    const NineSliceLayout t = ComputeNineSlice(TextureSize, Borders, {16.0f, 12.0f}, 0.5f);
    CHECK(Near(t.kx, 1.0f) && Near(t.ky, 1.0f));
    const NineSliceLayout s = ComputeNineSlice(TextureSize, Borders, {8.0f, 12.0f}, 0.5f);
    CHECK(Near(s.kx, 0.5f));
}

void TestPartialAndEmptyBorders() {
    const NineSliceLayout none = ComputeNineSlice(TextureSize, {}, {120.0f, 60.0f});
    CHECK(none.ValidCount() == 1 && none.quads[4].valid);    // no borders: the sprite is just stretched
    CHECK(Near(none.quads[4].rect.size.x, 120.0f) && Near(none.quads[4].uv[2], 1.0f));

    const NineSliceLayout leftOnly = ComputeNineSlice(TextureSize, {10.0f, 0.0f, 0.0f, 0.0f}, {120.0f, 60.0f});
    CHECK(leftOnly.ValidCount() == 2);                       // a fixed left strip + the stretched rest
    CHECK(Near(leftOnly.quads[3].rect.size.x, 10.0f) && Near(leftOnly.quads[4].rect.position.x, 10.0f));

    // Borders that eat the whole texture leave nothing to sample in the centre.
    const NineSliceLayout frame = ComputeNineSlice(TextureSize, {32.0f, 24.0f, 32.0f, 24.0f}, {200.0f, 100.0f});
    CHECK(!frame.quads[4].valid && !frame.quads[1].valid && !frame.quads[3].valid);
    CHECK(frame.quads[0].valid && frame.quads[8].valid);
}

void TestInvalidInputsAreSanitized() {
    const float nan = std::nanf("");
    CHECK(ComputeNineSlice({0.0f, 48.0f}, Borders, {100.0f, 50.0f}).ValidCount() == 0);   // no texture
    CHECK(ComputeNineSlice({nan, nan}, Borders, {100.0f, 50.0f}).ValidCount() == 0);
    CHECK(ComputeNineSlice(TextureSize, Borders, {nan, -5.0f}).ValidCount() == 0);        // no area to draw
    CHECK(ComputeNineSlice(TextureSize, Borders, {0.0f, 0.0f}).ValidCount() == 0);

    // Hostile borders are clamped, never producing NaN or overlap.
    const NineSliceLayout weird = ComputeNineSlice(TextureSize, {nan, -3.0f, INFINITY, 1.0e9f}, {100.0f, 50.0f}, nan);
    bool finite = std::isfinite(weird.kx) && std::isfinite(weird.ky);
    for (const NineSliceQuad& q : weird.quads) {
        finite = finite && std::isfinite(q.rect.position.x) && std::isfinite(q.rect.size.x) && std::isfinite(q.uv[2]);
    }
    CHECK(finite);
    // Borders larger than the texture are scaled down to fit it; nothing is left to sample in the middle, so
    // the result is a centre-less frame: four 32x24 corners, no overlap, no NaN.
    const NineSliceLayout oversized = ComputeNineSlice(TextureSize, {1000.0f, 1000.0f, 1000.0f, 1000.0f}, {100.0f, 50.0f});
    CHECK(oversized.ValidCount() == 4);
    CHECK(Near(Area(oversized), 4.0f * 32.0f * 24.0f));
}

// Property: for any borders/texture/target the valid quads never overlap, stay inside the target, keep
// their UVs in [0,1] and, when the texture has a centre to sample, tile the target exactly.
void TestRandomLayoutsTileWithoutOverlap() {
    std::srand(31415);
    bool inside = true, noOverlap = true, uvOk = true, tiles = true;
    for (int it = 0; it < 4000; ++it) {
        const Vec2 tex{static_cast<float>(8 + std::rand() % 120), static_cast<float>(8 + std::rand() % 120)};
        const NineSliceBorders b{static_cast<float>(std::rand() % 60), static_cast<float>(std::rand() % 60),
                                 static_cast<float>(std::rand() % 60), static_cast<float>(std::rand() % 60)};
        const Vec2 target{static_cast<float>(1 + std::rand() % 300), static_cast<float>(1 + std::rand() % 300)};
        const float scale = 0.25f + static_cast<float>(std::rand() % 8) * 0.25f;
        const NineSliceLayout l = ComputeNineSlice(tex, b, target, scale);
        for (std::size_t i = 0; i < 9; ++i) {
            const NineSliceQuad& q = l.quads[i];
            if (!q.valid) continue;
            inside = inside && q.rect.position.x >= -1.0e-3f && q.rect.position.y >= -1.0e-3f &&
                     q.rect.position.x + q.rect.size.x <= target.x + 1.0e-3f && q.rect.position.y + q.rect.size.y <= target.y + 1.0e-3f;
            uvOk = uvOk && q.uv[0] >= -1.0e-5f && q.uv[1] >= -1.0e-5f && q.uv[0] + q.uv[2] <= 1.0f + 1.0e-5f && q.uv[1] + q.uv[3] <= 1.0f + 1.0e-5f;
            for (std::size_t j = i + 1; j < 9; ++j) {
                if (l.quads[j].valid && Overlap(q.rect, l.quads[j].rect)) noOverlap = false;
            }
        }
        const bool hasCentre = l.quads[4].valid || (l.quads[3].valid && l.quads[5].valid) == false;
        (void)hasCentre;
        // Every region of a texture whose borders leave a centre must be valid, hence the full target is covered.
        const float bw = std::min(b.left + b.right, tex.x), bh = std::min(b.top + b.bottom, tex.y);
        if (bw < tex.x && bh < tex.y && target.x >= (b.left + b.right) * scale && target.y >= (b.top + b.bottom) * scale) {
            tiles = tiles && std::abs(Area(l) - target.x * target.y) < 0.05f;
        }
    }
    CHECK(inside);
    CHECK(noOverlap);
    CHECK(uvOk);
    CHECK(tiles);
}

void TestAppendProducesOneItemPerValidRegion() {
    UIRenderItem proto;
    proto.handle = {1, 1};
    proto.layout = {{10.0f, 20.0f}, {200.0f, 100.0f}, true, false};
    proto.textureSlice = 3;
    proto.paintOrder = 7;
    proto.layer = 2;
    proto.uvRect[0] = 0.5f; proto.uvRect[1] = 0.25f; proto.uvRect[2] = 0.25f; proto.uvRect[3] = 0.5f; // sprite lives in an atlas
    std::vector<UIRenderItem> items;
    const NineSliceLayout l = ComputeNineSlice(TextureSize, Borders, {200.0f, 100.0f});
    CHECK(AppendNineSliceItems(items, proto, l) == 9);
    CHECK(items.size() == 9);
    CHECK(Near(items[0].layout.resolvedPosition.x, 10.0f) && Near(items[0].layout.resolvedPosition.y, 20.0f)); // TL at the target origin
    CHECK(Near(items[8].layout.resolvedPosition.x, 10.0f + 184.0f) && Near(items[8].layout.resolvedPosition.y, 20.0f + 88.0f));
    CHECK(items[4].textureSlice == 3 && items[4].paintOrder == 7 && items[4].layer == 2); // prototype properties are kept
    // UVs are remapped into the atlas rectangle: the centre starts 25% into the sprite.
    CHECK(Near(items[4].uvRect[0], 0.5f + 0.25f * 0.25f) && Near(items[4].uvRect[2], 0.25f * 0.5f));
    CHECK(Near(items[8].uvRect[0] + items[8].uvRect[2], 0.75f));  // right edge of the atlas rect
    CHECK(Near(items[8].uvRect[1] + items[8].uvRect[3], 0.75f));  // bottom edge

    // Skipped regions produce no item; unsupported transforms produce nothing.
    items.clear();
    CHECK(AppendNineSliceItems(items, proto, ComputeNineSlice(TextureSize, Borders, {20.0f, 10.0f})) == 4);
    items.clear();
    UIRenderItem rotated = proto;
    rotated.transform.rotationRadians = 0.5f;
    CHECK(AppendNineSliceItems(items, rotated, l) == 0 && items.empty());
}

// ------------------------------------------------------------------ the screenshot
// A procedural 64x48 "window frame" so distortion is visible: yellow corners with a dark core, orange
// edges and a striped blue centre that reveals stretching.
constexpr Vec4 CornerYellow{0.95f, 0.80f, 0.20f, 1.0f};
constexpr Vec4 CornerCore{0.10f, 0.10f, 0.14f, 1.0f};
constexpr Vec4 EdgeOrange{0.95f, 0.55f, 0.15f, 1.0f};
constexpr Vec4 CentreA{0.20f, 0.40f, 0.85f, 1.0f};
constexpr Vec4 CentreB{0.30f, 0.52f, 0.95f, 1.0f};

Vec4 Lin(Vec4 srgb) { return SrgbStraightToLinearPremultiplied(srgb); }

Vec4 FrameTexture(std::uint32_t slice, Vec2 uv) {
    if (slice != 1) return {1.0f, 1.0f, 1.0f, 1.0f};
    const float tx = uv.x * 64.0f, ty = uv.y * 48.0f;
    const bool left = tx < 16.0f, right = tx >= 48.0f, top = ty < 12.0f, bottom = ty >= 36.0f;
    const bool borderX = left || right, borderY = top || bottom;
    if (borderX && borderY) {
        const float cx = left ? 8.0f : 56.0f, cy = top ? 6.0f : 42.0f;
        return std::hypot(tx - cx, ty - cy) < 4.0f ? Lin(CornerCore) : Lin(CornerYellow);
    }
    if (borderX || borderY) return Lin(EdgeOrange);
    const int stripe = static_cast<int>((tx - 16.0f) / 4.0f) + static_cast<int>((ty - 12.0f) / 4.0f);
    return (stripe & 1) ? Lin(CentreA) : Lin(CentreB);
}

void AddPanel(std::vector<UIRenderItem>& items, std::uint64_t& order, Vec2 position, Vec2 size, bool nineSlice) {
    UIRenderItem proto;
    proto.handle = {static_cast<std::uint32_t>(items.size() + 1), 1};
    proto.layout = {position, size, true, false};
    proto.transform.pivot = {0.0f, 0.0f};
    proto.textureSlice = 1;
    proto.paintOrder = order++;
    if (nineSlice) {
        AppendNineSliceItems(items, proto, ComputeNineSlice(TextureSize, Borders, size));
    } else {
        items.push_back(proto); // naive: the whole texture stretched over the rectangle
    }
}

void TestVisualNineSliceVersusStretch() {
    const Vec2 sizes[4] = {{220.0f, 130.0f}, {120.0f, 70.0f}, {60.0f, 40.0f}, {30.0f, 22.0f}};
    const float xs[4] = {10.0f, 250.0f, 390.0f, 470.0f};

    std::vector<UIRenderItem> items;
    std::uint64_t order = 0;
    for (int i = 0; i < 4; ++i) {
        AddPanel(items, order, {xs[i], 10.0f}, sizes[i], true);    // top row: nine-slice
        AddPanel(items, order, {xs[i], 170.0f}, sizes[i], false);  // bottom row: plain stretch
    }
    const FramePacket packet = RenderExtractor{}.Build(1, items);
    Visual::Image image(520, 320, Lin({0.075f, 0.085f, 0.105f, 1.0f}));
    Visual::SoftwareRenderBackend backend(image, 1.0f, FrameTexture);
    CHECK(backend.Submit(packet));
    const std::string path = Visual::SaveSnapshot(image, "10_nine_slice_vs_stretch");
    if (!path.empty()) std::cout << "  [snapshot] " << path << '\n';

    // Large nine-slice panel: the corner core is exactly where the texture puts it (corner not distorted).
    CHECK(Visual::PixelIs(image, 10 + 8, 10 + 6, CornerCore));
    CHECK(Visual::PixelIs(image, 10 + 2, 10 + 2, CornerYellow));
    CHECK(Visual::PixelIs(image, 10 + 220 - 8, 10 + 130 - 6, CornerCore));     // bottom-right corner
    CHECK(Visual::PixelIs(image, 10 + 110, 10 + 2, EdgeOrange));               // stretched top edge
    CHECK(Visual::PixelIs(image, 10 + 110, 10 + 65, CentreA, 40) || Visual::PixelIs(image, 10 + 110, 10 + 65, CentreB, 40));

    // The same sprite merely stretched distorts the corner: its core moved to (27.5, 16.25).
    CHECK(!Visual::PixelIs(image, 10 + 8, 170 + 6, CornerCore));
    CHECK(Visual::PixelIs(image, 10 + 27, 170 + 16, CornerCore, 6));

    // Attenuated 30x22 panel: the four corners tile it; no edge or centre texel is left to draw.
    CHECK(Visual::PixelIs(image, 470 + 7, 10 + 5, CornerCore, 6));             // TL core at (8*kx, 6*ky)
    CHECK(Visual::PixelIs(image, 470 + 22, 10 + 5, CornerCore, 6));            // TR core: the corners do not overlap
    std::size_t blueOrOrange = 0;
    for (int y = 10; y < 32; ++y) {
        for (int x = 470; x < 500; ++x) {
            if (Visual::PixelIs(image, x, y, EdgeOrange, 8) || Visual::PixelIs(image, x, y, CentreA, 8) || Visual::PixelIs(image, x, y, CentreB, 8)) ++blueOrOrange;
        }
    }
    CHECK(blueOrOrange == 0);
}

} // namespace

void RunNineSliceTests() {
    TestLargeTargetKeepsCornersAndStretchesTheRest();
    TestAttenuationFollowsTheSpecificationFormula();
    TestBorderScaleForHighDensityArt();
    TestPartialAndEmptyBorders();
    TestInvalidInputsAreSanitized();
    TestRandomLayoutsTileWithoutOverlap();
    TestAppendProducesOneItemPerValidRegion();
    TestVisualNineSliceVersusStretch();
}

} // namespace SpriteUiTests
