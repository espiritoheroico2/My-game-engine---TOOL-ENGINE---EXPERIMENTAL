#include "SpriteUi/SpriteUi.hpp"
#include "SpriteUi/Rendering/PanelSdf.hpp"
#include "SpriteUi/Text/DebugShaper.hpp"

#include "TestHarness.hpp"
#include "TestSuites.hpp"
#include "Visual/VisualHarness.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace SpriteUiTests {

namespace {

using namespace Visual;
namespace SU = ToolEngine::SpriteUi;

constexpr SU::Vec4 Orange{0.95f, 0.55f, 0.15f, 1.0f};
constexpr SU::Vec4 Green{0.25f, 0.80f, 0.40f, 1.0f};
constexpr SU::Vec4 Blue{0.20f, 0.45f, 0.95f, 1.0f};
constexpr SU::Vec4 Red{0.90f, 0.25f, 0.25f, 1.0f};
constexpr SU::Vec4 White{0.95f, 0.95f, 0.95f, 1.0f};
constexpr SU::Vec4 Cyan{0.20f, 0.85f, 0.90f, 1.0f};
constexpr SU::Vec4 Magenta{0.95f, 0.30f, 0.80f, 1.0f};
constexpr SU::Vec4 Yellow{1.0f, 0.85f, 0.20f, 1.0f};
constexpr SU::Vec4 Panel{0.13f, 0.16f, 0.22f, 1.0f};
constexpr SU::Vec4 Background{0.075f, 0.085f, 0.105f, 1.0f};

void Note(const std::string& path) {
    if (!path.empty()) std::cout << "  [snapshot] " << path << '\n';
}

// ---------------------------------------------------------------- oscilloscope window
// Visualises the fix for GetOrderedSamples: with a 64-column plot over a 300-sample history, the
// legacy call plots samples 44..107 (stale), GetLatestSamples plots 236..299 (what you want).
void TestVisualOscilloscopeWindows() {
    constexpr float Phase = 2.16f; // puts the newest sample (299) near 0.8 so its bar is tall enough to probe
    SU::Widgets::UIOscilloscopeGraph osc;
    for (int i = 0; i < 300; ++i) osc.AddSample(0.5f + 0.5f * std::sin(0.1f * static_cast<float>(i) + Phase));

    constexpr int Columns = 64;
    float oldest[Columns];
    float latest[Columns];
    osc.GetOrderedSamples(oldest);
    CHECK(osc.GetLatestSamples(latest) == Columns);

    constexpr float Baseline = 130.0f, Scale = 100.0f, BarWidth = 4.0f;
    Scene scene;
    const float leftX = 10.0f, rightX = 290.0f;
    scene.Rect({{leftX - 4, 10}, {Columns * BarWidth + 8, 130}}, Panel);
    scene.Rect({{rightX - 4, 10}, {Columns * BarWidth + 8, 130}}, Panel);
    for (int c = 0; c < Columns; ++c) {
        const float ho = oldest[c] * Scale, hl = latest[c] * Scale;
        scene.Rect({{leftX + static_cast<float>(c) * BarWidth, Baseline - ho}, {BarWidth - 1, ho}}, Orange);
        scene.Rect({{rightX + static_cast<float>(c) * BarWidth, Baseline - hl}, {BarWidth - 1, hl}}, Green);
    }
    const Image image = scene.Render(560, 150);
    Note(SaveSnapshot(image, "01_oscilloscope_oldest_vs_latest"));

    // The newest sample (299) is the last green bar and NOT part of the legacy window.
    const float newest = 0.5f + 0.5f * std::sin(0.1f * 299.0f + Phase);
    CHECK(Near(latest[Columns - 1], newest));
    const int lastX = static_cast<int>(rightX) + (Columns - 1) * 4 + 1;
    const int topY = static_cast<int>(std::lround(Baseline - newest * Scale));
    CHECK(PixelIs(image, lastX, topY + 3, Green));        // inside the bar
    CHECK(PixelIs(image, lastX, topY - 4, Panel));        // above the bar
    CHECK(!Near(oldest[Columns - 1], latest[Columns - 1], 1.0e-3f)); // different windows
    CHECK(Near(oldest[0], 0.5f + 0.5f * std::sin(0.1f * 44.0f + Phase)));
}

// ---------------------------------------------------------------- painter order vs the spec's sort key
// The specification (section 2) sorts by one 64-bit key: Canvas|Layer|Clip|Material|Sibling. Material
// sits ABOVE sibling depth, so overlapping translucent sprites are drawn in material order, not
// paint order. This scene proves the implemented rule (paint order is authoritative) is the correct one.
std::uint64_t SpecSortKey(std::uint64_t canvas, std::uint64_t layer, std::uint64_t clip,
                          std::uint64_t material, std::uint64_t sibling) {
    return (canvas << 48) | (layer << 32) | (clip << 16) | (material << 8) | sibling;
}

SU::Vec4 OverSrgb(SU::Vec4 backgroundSrgb, SU::Vec4 layerSrgb, float alpha) {
    // Premultiplied "over" in linear space, the way the pipeline blends.
    const SU::Vec4 bg = SU::Rendering::SrgbStraightToLinearPremultiplied(backgroundSrgb);
    SU::Vec4 l = SU::Rendering::SrgbStraightToLinearPremultiplied({layerSrgb.x, layerSrgb.y, layerSrgb.z, alpha});
    const float inv = 1.0f - l.w;
    return {LinearToSrgbChannel(l.x + bg.x * inv), LinearToSrgbChannel(l.y + bg.y * inv),
            LinearToSrgbChannel(l.z + bg.z * inv), 1.0f};
}

void TestVisualPaintOrderBeatsSpecSortKey() {
    struct Sprite { SU::Rect rect; SU::Vec4 color; std::uint32_t material; std::uint64_t sibling; };
    // A is BELOW B in paint order (sibling 0 < 1) but has the HIGHER material id.
    const std::vector<Sprite> sprites = {
        {{{20, 20}, {90, 90}}, {0.95f, 0.20f, 0.20f, 0.65f}, 5, 0},  // A: red, material 5, below
        {{{60, 50}, {90, 90}}, {0.20f, 0.35f, 0.95f, 0.65f}, 2, 1},  // B: blue, material 2, above
    };

    auto render = [&](bool useSpecKey) {
        std::vector<std::size_t> order = {0, 1};
        if (useSpecKey) {
            std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
                return SpecSortKey(0, 0, 0, sprites[a].material, sprites[a].sibling) <
                       SpecSortKey(0, 0, 0, sprites[b].material, sprites[b].sibling);
            });
        }
        Scene scene;
        for (std::size_t idx : order) {
            const Sprite& s = sprites[idx];
            auto& item = scene.Raw(s.rect, {s.color.x, s.color.y, s.color.z, 1.0f});
            item.tintPremultipliedLinear = SU::Rendering::SrgbStraightToLinearPremultiplied(s.color);
            item.materialIndex = s.material;
        }
        return scene.Render(170, 160);
    };

    const Image implemented = render(false); // paint order: A then B
    const Image specKey = render(true);      // spec key: material 2 (B) then material 5 (A)

    // Side-by-side for the screenshot.
    Image both(350, 160, SU::Rendering::SrgbStraightToLinearPremultiplied(Background));
    for (int y = 0; y < 160; ++y) {
        for (int x = 0; x < 170; ++x) {
            std::copy(implemented.Pixel(x, y), implemented.Pixel(x, y) + 4, both.Pixel(x, y));
            std::copy(specKey.Pixel(x, y), specKey.Pixel(x, y) + 4, both.Pixel(x + 180, y));
        }
    }
    Note(SaveSnapshot(both, "02_paint_order_vs_spec_sort_key"));

    const int ox = 90, oy = 80; // inside the overlap of A and B
    const SU::Vec4 aOnly = OverSrgb(Background, {0.95f, 0.20f, 0.20f, 1.0f}, 0.65f);
    const SU::Vec4 bOverA = {OverSrgb(aOnly, {0.20f, 0.35f, 0.95f, 1.0f}, 0.65f)};
    const SU::Vec4 aOverB = {OverSrgb(OverSrgb(Background, {0.20f, 0.35f, 0.95f, 1.0f}, 0.65f), {0.95f, 0.20f, 0.20f, 1.0f}, 0.65f)};

    CHECK(PixelIs(implemented, ox, oy, bOverA));   // B really is on top of A
    CHECK(PixelIs(specKey, ox, oy, aOverB));       // the spec key flips it: A on top of B
    CHECK(!PixelIs(implemented, ox, oy, aOverB));  // and the two results are visibly different
    CHECK(!PixelIs(specKey, ox, oy, bOverA, 8));
}

// ---------------------------------------------------------------- SDF rounded panel
void TestSdfReferenceValues() {
    using SU::Rendering::EvaluatePanelSdf;
    const SU::Vec2 half{50.0f, 20.0f};
    CHECK(Near(EvaluatePanelSdf({0, 0}, half, 10.0f), -20.0f));           // centre: distance to the nearest edge
    CHECK(Near(EvaluatePanelSdf({50, 0}, half, 10.0f), 0.0f));            // on the right edge
    CHECK(Near(EvaluatePanelSdf({60, 0}, half, 10.0f), 10.0f));           // 10 px outside the right edge
    CHECK(Near(EvaluatePanelSdf({50, 20}, half, 10.0f), std::sqrt(200.0f) - 10.0f)); // the cut corner is OUTSIDE
    CHECK(Near(EvaluatePanelSdf({50, 20}, half, 0.0f), 0.0f));            // square corner is ON the edge
    // A radius larger than the half extent is clamped (pill), instead of degenerating.
    CHECK(Near(EvaluatePanelSdf({0, 0}, half, 999.0f), EvaluatePanelSdf({0, 0}, half, 20.0f)));
    CHECK(Near(EvaluatePanelSdf({0, 0}, half, -5.0f), EvaluatePanelSdf({0, 0}, half, 0.0f)));
    CHECK(std::isfinite(EvaluatePanelSdf({1, 1}, half, std::nanf(""))));
    CHECK(Near(SU::Rendering::PanelCoverage(-3.0f, 1.0f), 1.0f));
    CHECK(Near(SU::Rendering::PanelCoverage(0.0f, 1.0f), 0.5f));
    CHECK(Near(SU::Rendering::PanelCoverage(3.0f, 1.0f), 0.0f));
}

void TestVisualSdfPanels() {
    Scene scene;
    const float radii[] = {0.0f, 8.0f, 24.0f, 1000.0f};
    for (int i = 0; i < 4; ++i) {
        const SU::Rect r{{20.0f + 140.0f * static_cast<float>(i), 20.0f}, {120.0f, 70.0f}};
        scene.Rect(r, Blue, SU::Rendering::WhiteTextureSlice, SU::Rendering::FlagRoundedPanel, radii[i]);
    }
    scene.Rect({{20, 110}, {540, 40}}, Green, 0, SU::Rendering::FlagRoundedPanel, 20.0f); // pill
    const Image image = scene.Render(580, 170);
    Note(SaveSnapshot(image, "03_sdf_rounded_panels_dpi1"));
    const Image retina = scene.Render(1160, 340, 2.0f);
    Note(SaveSnapshot(retina, "04_sdf_rounded_panels_dpi2"));

    // radius 0: the corner pixel is filled; radius 24: the corner is cut away; centre always filled.
    CHECK(PixelIs(image, 21, 21, Blue));
    CHECK(PixelIs(image, 20 + 140 * 2 + 1, 21, Background));
    CHECK(PixelIs(image, 20 + 140 * 2 + 60, 55, Blue));
    // radius 1000 clamps to 35 (half of the shorter side): a pill-ish shape, corner empty, middle full.
    CHECK(PixelIs(image, 20 + 140 * 3 + 1, 21, Background));
    CHECK(PixelIs(image, 20 + 140 * 3 + 60, 55, Blue));
    // pill: the left end is a semicircle.
    CHECK(PixelIs(image, 21, 111, Background));
    CHECK(PixelIs(image, 40, 130, Green));
    // DPI 2 renders the same shape twice as large, still with cut corners.
    CHECK(PixelIs(retina, 2 * (20 + 140 * 2) + 2, 42, Background));
    CHECK(PixelIs(retina, 2 * (20 + 140 * 2 + 60), 110, Blue));
}

// ---------------------------------------------------------------- clip hierarchy
void TestVisualClipHierarchy() {
    using namespace SU::Rendering;
    std::vector<ClipNode> nodes(3);
    nodes[0].rect = {40, 30, 240, 170};                        // outer window
    nodes[1].parentIndex = 0; nodes[1].rect = {120, 60, 360, 200}; // scroll area, sticks out of the window
    nodes[2].parentIndex = 1; nodes[2].enabled = false;        // disabled node inherits its parent
    const ClipHierarchyResult clips = BuildClipHierarchy(nodes);
    CHECK(clips.valid);
    CHECK(clips.effectiveRects.size() == 2);
    CHECK(Near(clips.effectiveRects[1].minX, 120.0f) && Near(clips.effectiveRects[1].maxX, 240.0f));
    CHECK(clips.nodeClipIndices[2] == clips.nodeClipIndices[1]);

    Scene scene;
    for (const auto& rect : clips.effectiveRects) scene.AddClip(rect);
    scene.Rect({{0, 0}, {400, 230}}, SU::Vec4{0.30f, 0.30f, 0.38f, 1.0f}, 0, 0, 0, clips.nodeClipIndices[0]);
    scene.Rect({{0, 0}, {400, 230}}, Orange, 0, 0, 0, clips.nodeClipIndices[1]);
    scene.Outline({{40, 30}, {200, 140}}, 1.0f, Cyan);     // window clip
    scene.Outline({{120, 60}, {120, 110}}, 1.0f, Yellow);  // effective (intersected) clip
    const Image image = scene.Render(400, 230);
    Note(SaveSnapshot(image, "05_clip_hierarchy"));

    CHECK(PixelIs(image, 60, 50, {0.30f, 0.30f, 0.38f, 1.0f}));  // inside the window, outside the scroll area
    CHECK(PixelIs(image, 200, 100, Orange));                      // inside the intersection
    CHECK(PixelIs(image, 300, 100, Background));                  // the scroll area sticks out: clipped by the window
    CHECK(PixelIs(image, 200, 190, Background));                  // below the window: clipped
    CHECK(PixelIs(image, 20, 20, Background));                    // outside everything
}


// ---------------------------------------------------------------- text: wrap, alignment, caret, click
SU::Text::FontMetrics VisualMetrics() {
    SU::Text::FontMetrics m;
    m.fontSize = 12.0f; m.unitsPerEm = 1000.0f; m.ascent = 800.0f; m.descent = -200.0f; m.lineGap = 200.0f;
    return m;
}

SU::Text::TextLayoutResult LayoutVisual(std::string_view text, float width, SU::Text::TextWrap wrap,
                                        SU::Text::TextAlignment align) {
    const auto metrics = VisualMetrics();
    SU::Text::DebugShaperConfig shaper;
    shaper.advanceEm = 0.5f;
    const auto glyphs = SU::Text::ShapeDebug(text, metrics, shaper);
    SU::Text::TextLayoutConfig config;
    config.fontSize = 12.0f;
    config.maxWidth = width;
    config.wrap = wrap;
    config.alignment = align;
    return SU::Text::LayoutText(text, glyphs, metrics, config);
}

std::string LineText(std::string_view text, const SU::Text::TextLine& line) {
    std::string s(text.substr(line.byteBegin, line.byteEnd - line.byteBegin));
    while (!s.empty() && (s.back() == ' ' || s.back() == '\n')) s.pop_back();
    return s;
}

void TestVisualTextWrapCaretAndClick() {
    using SU::Text::TextAlignment;
    using SU::Text::TextWrap;
    const std::string text = "The quick brown fox jumps over the lazy dog; supercalifragilisticexpialidocious!";
    constexpr float Width = 150.0f;

    const auto word = LayoutVisual(text, Width, TextWrap::Word, TextAlignment::Left);
    const auto chr = LayoutVisual(text, Width, TextWrap::Char, TextAlignment::Left);
    const auto centered = LayoutVisual(text, Width, TextWrap::Word, TextAlignment::Center);

    Scene scene;
    const float colX[3] = {12.0f, 182.0f, 352.0f};
    const SU::Text::TextLayoutResult* layouts[3] = {&word, &chr, &centered};
    for (int i = 0; i < 3; ++i) {
        const SU::Vec2 origin{colX[i], 14.0f};
        scene.Rect({{origin.x - 4, origin.y - 4}, {Width + 8, 150.0f}}, Panel);
        scene.Outline({{origin.x, origin.y}, {Width, 142.0f}}, 1.0f, {0.30f, 0.34f, 0.44f, 1.0f}); // wrap limit
        scene.Text(*layouts[i], origin, White);
        scene.TextGuides(*layouts[i], origin, {0.15f, 0.45f, 0.50f, 1.0f}, {0.55f, 0.20f, 0.45f, 1.0f});
    }
    // Carets at a few byte offsets (pen origin, line top) and the result of a click.
    for (std::uint32_t offset : {0u, 4u, 36u, 70u}) {
        scene.Caret(SU::Text::GetCaretRect(word, offset), {colX[0], 14.0f}, Yellow);
    }
    const SU::Vec2 click{88.0f, 30.0f};
    const std::uint32_t hit = SU::Text::FindInsertionOffset(word, click);
    scene.Rect({{colX[0] + click.x - 3, 14.0f + click.y - 3}, {6, 6}}, Red);
    scene.Caret(SU::Text::GetCaretRect(word, hit), {colX[0], 14.0f}, Green, 2.5f);
    const Image image = scene.Render(520, 178);
    Note(SaveSnapshot(image, "06_text_wrap_word_char_center"));

    std::vector<std::string> wordLines, charLines;
    for (const auto& l : word.lines) wordLines.push_back(LineText(text, l));
    for (const auto& l : chr.lines) charLines.push_back(LineText(text, l));
    CHECK(wordLines.size() == 4);
    CHECK(wordLines[0] == "The quick brown fox jumps");
    CHECK(wordLines[1] == "over the lazy dog;");              // the long word moved down whole...
    CHECK(wordLines[2] == "supercalifragilisticexpia");       // ...and only broken because it cannot fit anywhere
    CHECK(wordLines[3] == "lidocious!");
    CHECK(wordLines != charLines);                             // Char breaks mid-word, Word does not
    CHECK(charLines[1] != "over the lazy dog;");
    CHECK(Near(centered.lines[3].x, (Width - centered.lines[3].width) * 0.5f));

    // Click at x=88 on the first line lands between 'q'-'u'-'i'...: the caret stop nearest to the click.
    CHECK(Near(SU::Text::GetCaretRect(word, hit).position.x, std::round(88.0f / 6.0f) * 6.0f));
    // The first glyph box of column 0 is painted in the text colour.
    CHECK(PixelIs(image, static_cast<int>(colX[0] + 2), 14 + 4, White, 12));
}


// ---------------------------------------------------------------- layout: real pipeline, wrap, baseline
SU::Vec4 Palette(std::size_t i) {
    static const SU::Vec4 colors[] = {{0.20f, 0.45f, 0.95f, 1.0f}, {0.25f, 0.80f, 0.40f, 1.0f}, {0.95f, 0.55f, 0.15f, 1.0f},
                                      {0.85f, 0.30f, 0.60f, 1.0f}, {0.60f, 0.45f, 0.95f, 1.0f}, {0.95f, 0.80f, 0.20f, 1.0f}};
    return colors[i % 6];
}

SU::LayoutNode FixedBox(float w, float h) {
    SU::LayoutNode n;
    n.style.width = {SU::SizeMode::Fixed, w};
    n.style.height = {SU::SizeMode::Fixed, h};
    return n;
}

void DrawLayoutLeaves(Scene& scene, const SU::LayoutNode& node, SU::Vec2 origin, std::size_t& colorIndex) {
    if (node.children.empty()) {
        scene.Rect({{origin.x + node.rect.position.x, origin.y + node.rect.position.y}, node.rect.size}, Palette(colorIndex++));
        return;
    }
    for (const auto& child : node.children) DrawLayoutLeaves(scene, child, origin, colorIndex);
}

// Menu laid out by LayoutTree, turned into render items by LayoutSceneBridge, sorted/batched by the
// RenderExtractor and rasterized: the whole CPU side of the pipeline, at two viewport sizes.
struct MenuHandles { SU::UIHandle header, sidebar, content, footer; SU::UIHandle button[3]; };

MenuHandles BuildMenu(SU::LayoutTree& tree, SU::LayoutSceneBridge& bridge) {
    using namespace SU;
    MenuHandles h;
    FlexItem rootStyle;
    rootStyle.direction = LayoutDirection::Column;
    rootStyle.width = {SizeMode::Percent, 1.0f};
    rootStyle.height = {SizeMode::Percent, 1.0f};
    rootStyle.padding = {24, 24, 24, 24};
    rootStyle.gap = 12.0f;
    const UIHandle root = tree.Create({}, rootStyle);

    FlexItem headerStyle; headerStyle.height = {SizeMode::Fixed, 56.0f};
    h.header = tree.Create(root, headerStyle);

    FlexItem bodyStyle; bodyStyle.direction = LayoutDirection::Row; bodyStyle.gap = 12.0f; bodyStyle.grow = 1.0f;
    bodyStyle.height = {SizeMode::Flex, 0.0f};
    const UIHandle body = tree.Create(root, bodyStyle);

    FlexItem sidebarStyle; sidebarStyle.width = {SizeMode::Fixed, 150.0f};
    h.sidebar = tree.Create(body, sidebarStyle);

    FlexItem contentStyle; contentStyle.direction = LayoutDirection::Column; contentStyle.gap = 10.0f; contentStyle.grow = 1.0f;
    contentStyle.padding = {12, 12, 12, 12};
    h.content = tree.Create(body, contentStyle);
    for (int i = 0; i < 3; ++i) {
        FlexItem buttonStyle; buttonStyle.height = {SizeMode::Fixed, 44.0f};
        buttonStyle.margin = {0, 0, i == 1 ? 24.0f : 0.0f, 0}; // Edges = {left, top, right, bottom}: the middle button is inset on the right
        h.button[i] = tree.Create(h.content, buttonStyle);
    }
    FlexItem footerStyle; footerStyle.height = {SizeMode::Fixed, 36.0f};
    h.footer = tree.Create(root, footerStyle);

    auto visual = [&](UIHandle handle, SU::Vec4 srgb, float z) {
        ElementVisual v;
        v.tintPremultipliedLinear = Rendering::SrgbStraightToLinearPremultiplied(srgb);
        v.zOrder = z;
        bridge.SetVisual(handle, v);
    };
    visual(h.header, {0.20f, 0.45f, 0.95f, 1.0f}, 1.0f);
    visual(h.sidebar, {0.18f, 0.22f, 0.30f, 1.0f}, 1.0f);
    visual(h.content, {0.13f, 0.16f, 0.22f, 1.0f}, 1.0f);
    for (int i = 0; i < 3; ++i) visual(h.button[i], Palette(static_cast<std::size_t>(i + 1)), 2.0f);
    visual(h.footer, {0.30f, 0.32f, 0.38f, 1.0f}, 1.0f);
    return h;
}

void TestVisualMenuThroughTheRealPipeline() {
    SU::LayoutTree tree;
    SU::LayoutSceneBridge bridge;
    const MenuHandles h = BuildMenu(tree, bridge);

    for (const SU::Vec2 size : {SU::Vec2{640.0f, 360.0f}, SU::Vec2{960.0f, 540.0f}}) {
        tree.Layout({{0.0f, 0.0f}, size});
        const SU::SceneExtractionResult scene = bridge.Extract(tree);

        SU::Rendering::ExtractionStats stats;
        const SU::Rendering::FramePacket packet = SU::Rendering::RenderExtractor{}.Build(
            1, scene.renderItems, &stats, scene.clipHierarchy.effectiveRects);
        Image image(static_cast<int>(size.x), static_cast<int>(size.y), SU::Rendering::SrgbStraightToLinearPremultiplied(Background));
        SoftwareRenderBackend backend(image);
        CHECK(backend.Submit(packet));
        Note(SaveSnapshot(image, "09_menu_pipeline_" + std::to_string(static_cast<int>(size.x)) + "x" + std::to_string(static_cast<int>(size.y))));

        const SU::Rect header = tree.Get(h.header)->node.rect;
        const SU::Rect sidebar = tree.Get(h.sidebar)->node.rect;
        const SU::Rect content = tree.Get(h.content)->node.rect;
        const SU::Rect footer = tree.Get(h.footer)->node.rect;
        CHECK(Near(header.size.x, size.x - 48.0f));                       // stretches across the padded root
        CHECK(Near(footer.position.y + footer.size.y, size.y - 24.0f));   // pinned to the bottom padding
        CHECK(Near(sidebar.size.x, 150.0f));                              // fixed width regardless of viewport
        CHECK(Near(content.position.x, sidebar.position.x + 150.0f + 12.0f));
        CHECK(Near(content.size.x, size.x - 48.0f - 150.0f - 12.0f));     // the rest of the row (grow)
        CHECK(Near(content.size.y, footer.position.y - 12.0f - (header.position.y + header.size.y + 12.0f))); // body takes the remaining height

        // Buttons: inside the content panel's padding, the middle one inset by its right margin.
        const SU::Rect b0 = tree.Get(h.button[0])->node.rect;
        const SU::Rect b1 = tree.Get(h.button[1])->node.rect;
        CHECK(Near(b0.size.x, content.size.x - 24.0f));
        CHECK(Near(b1.size.x, content.size.x - 24.0f - 24.0f));

        // Pixels agree with the layout numbers (the full chain CPU -> packet -> raster).
        const auto cx = [](const SU::Rect& r) { return static_cast<int>(r.position.x + r.size.x * 0.5f); };
        const auto cy = [](const SU::Rect& r) { return static_cast<int>(r.position.y + r.size.y * 0.5f); };
        CHECK(PixelIs(image, cx(header), cy(header), {0.20f, 0.45f, 0.95f, 1.0f}));
        CHECK(PixelIs(image, cx(b0), cy(b0), Palette(1)));
        CHECK(PixelIs(image, cx(b1), cy(b1), Palette(2)));
        CHECK(PixelIs(image, cx(sidebar), cy(sidebar), {0.18f, 0.22f, 0.30f, 1.0f}));
        CHECK(PixelIs(image, 4, 4, Background));                          // the root padding stays empty
        CHECK(stats.batchCount >= 1 && stats.skippedInvalid == 0);
    }
}

void TestVisualFlexWrapAlignContent() {
    const SU::AlignContent modes[3] = {SU::AlignContent::Start, SU::AlignContent::SpaceBetween, SU::AlignContent::Stretch};
    const float widths[] = {40, 24, 56, 32, 48, 28, 44, 36, 52};

    Scene scene;
    std::vector<SU::LayoutNode> containers;
    for (int c = 0; c < 3; ++c) {
        SU::LayoutNode root;
        root.style.direction = SU::LayoutDirection::Row;
        root.style.wrap = SU::FlexWrap::Wrap;
        root.style.alignContent = modes[c];
        root.style.width = {SU::SizeMode::Fixed, 160.0f};
        root.style.height = {SU::SizeMode::Fixed, 150.0f};
        root.style.padding = {6, 6, 6, 6};
        root.style.gap = 6.0f;
        for (float w : widths) {
            SU::LayoutNode item = FixedBox(w, 24.0f);
            if (c == 2) item.style.height = {SU::SizeMode::Auto, 0.0f}; // Stretch demo: items take the line height
            root.children.push_back(item);
        }
        SU::LayoutEngine engine;
        (void)engine.Measure(root, {500.0f, 500.0f});
        engine.Arrange(root, {{0.0f, 0.0f}, root.measured});
        containers.push_back(root);

        const SU::Vec2 origin{10.0f + 175.0f * static_cast<float>(c), 10.0f};
        scene.Rect({origin, root.measured}, Panel);
        std::size_t colorIndex = 0;
        DrawLayoutLeaves(scene, root, origin, colorIndex);
    }
    const Image image = scene.Render(540, 170);
    Note(SaveSnapshot(image, "07_flex_wrap_align_content"));

    // 9 items in a 148px content width wrap into 3 lines (132 / 120 / 144 wide) whatever the alignContent is.
    for (const auto& root : containers) {
        std::vector<float> lineYs;
        for (const auto& item : root.children) {
            if (lineYs.empty() || std::abs(item.rect.position.y - lineYs.back()) > 0.5f) lineYs.push_back(item.rect.position.y);
        }
        CHECK(lineYs.size() == 3);
        float maxRight = 0.0f;
        for (const auto& item : root.children) maxRight = std::max(maxRight, item.rect.position.x + item.rect.size.x);
        CHECK(maxRight <= 160.0f - 6.0f + 1.0e-3f);   // nothing leaks past the padded edge
    }
    const auto& start = containers[0].children;
    const auto& between = containers[1].children;
    const auto& stretch = containers[2].children;
    CHECK(Near(start.front().rect.position.y, 6.0f));                       // Start: packed at the top
    CHECK(Near(between.front().rect.position.y, 6.0f));                     // SpaceBetween: first line at the top...
    CHECK(Near(between.back().rect.position.y + between.back().rect.size.y, 150.0f - 6.0f)); // ...last line at the bottom
    CHECK(Near(stretch.front().rect.size.y, stretch.back().rect.size.y));   // Stretch: lines (and items) grow equally
    CHECK(stretch.front().rect.size.y > 24.0f);
    // Pixel: the 1st item of the 1st container sits at (16, 16).
    CHECK(PixelIs(image, 10 + 6 + 3, 10 + 6 + 3, Palette(0)));
}

void TestVisualBaselineAlignmentWithText() {
    using namespace SU::Text;
    FontMetrics metrics;
    metrics.fontSize = 12.0f; metrics.unitsPerEm = 1000.0f; metrics.ascent = 800.0f; metrics.descent = -200.0f; metrics.lineGap = 0.0f;
    DebugShaperConfig shaper;
    shaper.advanceEm = 0.5f;

    struct Label { const char* text; float size; };
    const Label labels[] = {{"Small", 12.0f}, {"BIG TITLE", 30.0f}, {"medium", 18.0f}};

    Scene scene;
    float baselineYs[2][3] = {};
    for (int column = 0; column < 2; ++column) {
        const bool baselineAligned = column == 1;
        SU::LayoutNode row;
        row.style.direction = SU::LayoutDirection::Row;
        row.style.gap = 8.0f;
        row.style.padding = {8, 8, 8, 8};
        row.style.width = {SU::SizeMode::Fixed, 250.0f};
        row.style.height = {SU::SizeMode::Fixed, 70.0f};
        std::vector<TextLayoutResult> layouts;
        for (const Label& label : labels) {
            FontMetrics m = metrics;
            m.fontSize = label.size;
            const auto glyphs = ShapeDebug(label.text, m, shaper);
            TextLayoutConfig config;
            config.fontSize = label.size;
            layouts.push_back(LayoutText(label.text, glyphs, m, config));
            SU::LayoutNode node = FixedBox(layouts.back().size.x, layouts.back().size.y);
            node.style.alignItems = baselineAligned ? SU::AlignItems::Baseline : SU::AlignItems::Start;
            node.style.baseline = label.size * 0.8f;          // ascent = 0.8 em
            row.children.push_back(node);
        }
        SU::LayoutEngine engine;
        (void)engine.Measure(row, {500.0f, 500.0f});
        engine.Arrange(row, {{0.0f, 0.0f}, row.measured});

        const SU::Vec2 origin{10.0f + 270.0f * static_cast<float>(column), 10.0f};
        scene.Rect({origin, row.measured}, Panel);
        for (std::size_t i = 0; i < 3; ++i) {
            const SU::Rect& r = row.children[i].rect;
            baselineYs[column][i] = r.position.y + labels[i].size * 0.8f;
            scene.Outline({{origin.x + r.position.x, origin.y + r.position.y}, r.size}, 1.0f, {0.30f, 0.34f, 0.44f, 1.0f});
            scene.Text(layouts[i], {origin.x + r.position.x, origin.y + r.position.y}, White);
        }
        if (baselineAligned) {
            scene.HLine(origin.x, origin.y + baselineYs[1][0], row.measured.x, Magenta);
        }
    }
    const Image image = scene.Render(540, 90);
    Note(SaveSnapshot(image, "08_baseline_start_vs_baseline"));

    // Start: baselines differ (labels share the top). Baseline: all three coincide.
    CHECK(std::abs(baselineYs[0][0] - baselineYs[0][1]) > 5.0f);
    CHECK(Near(baselineYs[1][0], baselineYs[1][1], 1.0e-3f));
    CHECK(Near(baselineYs[1][1], baselineYs[1][2], 1.0e-3f));
    // On the magenta baseline of column 1, the pixel row just above it is glyph-coloured for all three labels.
    const int baselineY = 10 + static_cast<int>(std::lround(baselineYs[1][0]));
    CHECK(PixelIs(image, 10 + 270 + 8 + 4, baselineY - 3, White, 12));
    CHECK(PixelIs(image, 10 + 270, baselineY, Magenta, 30));
}

} // namespace

void RunVisualTests() {
    std::cout << "[visual] screenshots -> " << Visual::SnapshotDir().string() << '\n';
    TestVisualOscilloscopeWindows();
    TestVisualPaintOrderBeatsSpecSortKey();
    TestSdfReferenceValues();
    TestVisualSdfPanels();
    TestVisualClipHierarchy();
    TestVisualTextWrapCaretAndClick();
    TestVisualMenuThroughTheRealPipeline();
    TestVisualFlexWrapAlignContent();
    TestVisualBaselineAlignmentWithText();
}

} // namespace SpriteUiTests
