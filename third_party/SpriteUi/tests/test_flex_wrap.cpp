#include "SpriteUi/SpriteUi.hpp"

#include "Legacy/LegacyLayout.hpp"
#include "TestHarness.hpp"
#include "TestSuites.hpp"

#include <cmath>
#include <cstdlib>
#include <vector>

namespace SpriteUiTests {

namespace {

using namespace ToolEngine::SpriteUi;

LayoutNode Box(float w, float h) {
    LayoutNode n;
    n.style.width = {SizeMode::Fixed, w};
    n.style.height = {SizeMode::Fixed, h};
    return n;
}

// Measure with the given available space, then arrange the root at its measured size (origin 0,0).
void Run(LayoutNode& root, float availW, float availH) {
    LayoutEngine engine;
    (void)engine.Measure(root, {availW, availH});
    engine.Arrange(root, {{0.0f, 0.0f}, root.measured});
}

bool RectIs(const LayoutNode& n, float x, float y, float w, float h) {
    return Near(n.rect.position.x, x, 1.0e-3f) && Near(n.rect.position.y, y, 1.0e-3f) &&
           Near(n.rect.size.x, w, 1.0e-3f) && Near(n.rect.size.y, h, 1.0e-3f);
}

LayoutNode WrapRow(float width, float gap, int items, float itemW = 40.0f, float itemH = 20.0f) {
    LayoutNode root;
    root.style.direction = LayoutDirection::Row;
    root.style.wrap = FlexWrap::Wrap;
    root.style.width = {SizeMode::Fixed, width};
    root.style.gap = gap;
    for (int i = 0; i < items; ++i) root.children.push_back(Box(itemW, itemH));
    return root;
}

// ------------------------------------------------------------------ wrap
void TestRowWrapPacksLinesAndMeasuresHeight() {
    LayoutNode root = WrapRow(100.0f, 10.0f, 5); // 40+10+40 = 90 fits, a third item (140) does not
    Run(root, 500.0f, 500.0f);
    CHECK(RectIs(root.children[0], 0, 0, 40, 20));
    CHECK(RectIs(root.children[1], 50, 0, 40, 20));
    CHECK(RectIs(root.children[2], 0, 30, 40, 20));   // second line, 20 + 10 gap below
    CHECK(RectIs(root.children[3], 50, 30, 40, 20));
    CHECK(RectIs(root.children[4], 0, 60, 40, 20));   // third line
    CHECK(Near(root.measured.y, 80.0f));              // 3 lines * 20 + 2 gaps * 10: the container wraps its own height
    CHECK(Near(root.measured.x, 100.0f));
}

void TestNoWrapStillOverflowsOrShrinks() {
    LayoutNode root = WrapRow(100.0f, 10.0f, 5);
    root.style.wrap = FlexWrap::NoWrap;
    Run(root, 500.0f, 500.0f);
    CHECK(Near(root.measured.y, 20.0f)); // a single line
    float maxRight = 0.0f;
    for (const auto& c : root.children) maxRight = std::max(maxRight, c.rect.position.x + c.rect.size.x);
    CHECK(maxRight <= 100.0f + 1.0e-3f); // items shrink (shrink = 1) instead of wrapping
}

void TestWrapJustifyAppliesPerLine() {
    LayoutNode root = WrapRow(100.0f, 10.0f, 3); // lines: [40,40] [40]
    root.style.justifyContent = JustifyContent::Center;
    Run(root, 500.0f, 500.0f);
    CHECK(Near(root.children[0].rect.position.x, 5.0f));   // (100-90)/2
    CHECK(Near(root.children[1].rect.position.x, 55.0f));
    CHECK(Near(root.children[2].rect.position.x, 30.0f));   // last line is centred on its own: (100-40)/2
    root.style.justifyContent = JustifyContent::SpaceBetween;
    Run(root, 500.0f, 500.0f);
    CHECK(Near(root.children[1].rect.position.x, 60.0f));   // pushed to the right edge
    CHECK(Near(root.children[2].rect.position.x, 0.0f));    // single item: starts at the edge
}

void TestWrapGrowFillsEachLine() {
    LayoutNode root = WrapRow(100.0f, 10.0f, 3);
    for (auto& c : root.children) { c.style.width = {SizeMode::Auto, 0.0f}; c.style.grow = 1.0f; c.style.minSize = {40.0f, 0.0f}; }
    Run(root, 500.0f, 500.0f);
    CHECK(Near(root.children[0].rect.size.x, 45.0f));       // two items share (100 - 10) on line 1
    CHECK(Near(root.children[1].rect.size.x, 45.0f));
    CHECK(Near(root.children[2].rect.size.x, 100.0f));      // the lonely last item fills its whole line
}

void TestWrapMarginsCountTowardsTheLine() {
    LayoutNode root = WrapRow(100.0f, 0.0f, 3);
    for (auto& c : root.children) c.style.margin = {5.0f, 5.0f, 5.0f, 5.0f}; // outer width 50
    Run(root, 500.0f, 500.0f);
    CHECK(Near(root.children[1].rect.position.x, 55.0f));   // 5 + 40 + 5 + 5
    CHECK(Near(root.children[2].rect.position.x, 5.0f));    // wrapped
    CHECK(Near(root.children[2].rect.position.y, 35.0f));   // line 2 starts after outer height 30 + margin 5
    CHECK(Near(root.measured.y, 60.0f));                    // 2 lines * (20 + 10 margins)
}

void TestAlignContentDistributesLines() {
    auto make = [](AlignContent mode) {
        LayoutNode root = WrapRow(100.0f, 10.0f, 5);        // 3 lines of 20 + 2 gaps = 80
        root.style.height = {SizeMode::Fixed, 140.0f};       // 60 of free cross space
        root.style.alignContent = mode;
        return root;
    };
    LayoutNode start = make(AlignContent::Start);
    Run(start, 500.0f, 500.0f);
    CHECK(Near(start.children[0].rect.position.y, 0.0f));
    CHECK(Near(start.children[4].rect.position.y, 60.0f));

    LayoutNode center = make(AlignContent::Center);
    Run(center, 500.0f, 500.0f);
    CHECK(Near(center.children[0].rect.position.y, 30.0f));

    LayoutNode end = make(AlignContent::End);
    Run(end, 500.0f, 500.0f);
    CHECK(Near(end.children[0].rect.position.y, 60.0f));
    CHECK(Near(end.children[4].rect.position.y + end.children[4].rect.size.y, 140.0f));

    LayoutNode between = make(AlignContent::SpaceBetween);
    Run(between, 500.0f, 500.0f);
    CHECK(Near(between.children[0].rect.position.y, 0.0f));
    CHECK(Near(between.children[2].rect.position.y, 60.0f));         // second line: 20 + 10 gap + 30 extra
    CHECK(Near(between.children[4].rect.position.y, 120.0f));        // last line flush with the end
    CHECK(Near(between.children[4].rect.position.y + 20.0f, 140.0f));

    LayoutNode evenly = make(AlignContent::SpaceEvenly);
    Run(evenly, 500.0f, 500.0f);
    CHECK(Near(evenly.children[0].rect.position.y, 15.0f));          // 60 / 4 steps

    LayoutNode around = make(AlignContent::SpaceAround);
    Run(around, 500.0f, 500.0f);
    CHECK(Near(around.children[0].rect.position.y, 10.0f));          // 60 / 3 lines / 2
}

void TestAlignContentStretchGrowsLinesAndStretchItems() {
    LayoutNode root = WrapRow(100.0f, 10.0f, 5);
    root.style.height = {SizeMode::Fixed, 140.0f};
    root.style.alignContent = AlignContent::Stretch;
    for (auto& c : root.children) c.style.height = {SizeMode::Auto, 0.0f}; // items without an explicit height stretch
    Run(root, 500.0f, 500.0f);
    // 3 lines share 140 - 2 gaps = 120 -> 40 each.
    CHECK(Near(root.children[0].rect.size.y, 40.0f));
    CHECK(Near(root.children[2].rect.position.y, 50.0f));
    CHECK(Near(root.children[4].rect.position.y, 100.0f));
}

void TestColumnWrapFlowsIntoColumns() {
    LayoutNode root;
    root.style.direction = LayoutDirection::Column;
    root.style.wrap = FlexWrap::Wrap;
    root.style.height = {SizeMode::Fixed, 70.0f};
    root.style.gap = 5.0f;
    for (int i = 0; i < 5; ++i) root.children.push_back(Box(30.0f, 30.0f)); // 30+5+30 = 65 fits, 100 does not
    Run(root, 500.0f, 500.0f);
    CHECK(RectIs(root.children[0], 0, 0, 30, 30));
    CHECK(RectIs(root.children[1], 0, 35, 30, 30));
    CHECK(RectIs(root.children[2], 35, 0, 30, 30));       // second column
    CHECK(RectIs(root.children[4], 70, 0, 30, 30));       // third column
    CHECK(Near(root.measured.x, 100.0f));                  // 3 columns * 30 + 2 gaps
}

void TestWrapMeasureUsesAvailableWidthWhenAuto() {
    // A wrap container without its own width wraps at the space its parent offers.
    LayoutNode root;
    root.style.direction = LayoutDirection::Row;
    root.style.wrap = FlexWrap::Wrap;
    root.style.gap = 10.0f;
    for (int i = 0; i < 4; ++i) root.children.push_back(Box(40.0f, 20.0f));
    LayoutEngine engine;
    const Vec2 narrow = engine.Measure(root, {100.0f, 500.0f});
    CHECK(Near(narrow.y, 50.0f));   // 2 lines
    const Vec2 wide = engine.Measure(root, {500.0f, 500.0f});
    CHECK(Near(wide.y, 20.0f));     // 1 line
    CHECK(Near(wide.x, 190.0f));
}

void TestWrappedContainerInsideColumnGetsItsHeight() {
    LayoutNode column;
    column.style.direction = LayoutDirection::Column;
    column.style.width = {SizeMode::Fixed, 100.0f};
    column.style.gap = 8.0f;
    LayoutNode tags = WrapRow(100.0f, 10.0f, 5);
    tags.style.width = {SizeMode::Auto, 0.0f};
    column.children.push_back(Box(100.0f, 30.0f));  // header
    column.children.push_back(tags);                 // wraps inside the 100px column
    column.children.push_back(Box(100.0f, 30.0f));  // footer
    Run(column, 500.0f, 500.0f);
    CHECK(Near(column.children[1].rect.size.y, 80.0f));          // the wrapped block really is 3 lines tall
    CHECK(Near(column.children[2].rect.position.y, 30.0f + 8.0f + 80.0f + 8.0f)); // footer sits below it, no overlap
}

// ------------------------------------------------------------------ baseline
LayoutNode BaselineItem(float h, float baseline) {
    LayoutNode n = Box(30.0f, h);
    n.style.alignItems = AlignItems::Baseline;
    n.style.baseline = baseline;
    return n;
}

void TestBaselineAlignsItemsOnASharedLine() {
    LayoutNode root;
    root.style.direction = LayoutDirection::Row;
    root.style.gap = 5.0f;
    root.children = {BaselineItem(20.0f, 15.0f), BaselineItem(40.0f, 30.0f), BaselineItem(10.0f, -1.0f)};
    Run(root, 500.0f, 500.0f);
    // Ascents 15, 30, 10 -> shared ascent 30. Descents 5, 10, 0 -> line height 30 + 10 = 40.
    CHECK(Near(root.measured.y, 40.0f));
    CHECK(Near(root.children[0].rect.position.y, 15.0f));
    CHECK(Near(root.children[1].rect.position.y, 0.0f));
    CHECK(Near(root.children[2].rect.position.y, 20.0f));   // no baseline: its bottom edge is the baseline
    // The baselines coincide at y = 30.
    CHECK(Near(root.children[0].rect.position.y + 15.0f, 30.0f));
    CHECK(Near(root.children[1].rect.position.y + 30.0f, 30.0f));
    CHECK(Near(root.children[2].rect.position.y + 10.0f, 30.0f));
}

void TestBaselineWithMarginsAndMixedAlignment() {
    LayoutNode root;
    root.style.direction = LayoutDirection::Row;
    root.style.height = {SizeMode::Fixed, 60.0f};
    LayoutNode a = BaselineItem(20.0f, 14.0f);
    a.style.margin = {0.0f, 4.0f, 0.0f, 0.0f};  // Edges = {left, top, right, bottom}: a 4px TOP margin
    LayoutNode b = BaselineItem(30.0f, 24.0f);
    LayoutNode c = Box(30.0f, 10.0f);            // not baseline: centred
    c.style.alignItems = AlignItems::Center;
    root.children = {a, b, c};
    Run(root, 500.0f, 500.0f);
    // ascents: a = 4 + 14 = 18, b = 24 -> shared 24.
    CHECK(Near(root.children[0].rect.position.y, 24.0f - 18.0f + 4.0f));
    CHECK(Near(root.children[1].rect.position.y, 0.0f));
    CHECK(Near(root.children[2].rect.position.y, 25.0f));    // centred in the 60px line
}

void TestBaselineIsIgnoredInColumns() {
    LayoutNode root;
    root.style.direction = LayoutDirection::Column;
    root.children = {BaselineItem(20.0f, 15.0f), BaselineItem(40.0f, 30.0f)};
    Run(root, 500.0f, 500.0f);
    CHECK(Near(root.children[0].rect.position.x, 0.0f));     // behaves like Start
    CHECK(Near(root.children[1].rect.position.x, 0.0f));
    CHECK(Near(root.measured.y, 60.0f));
}

void TestBaselinePerLineWithWrap() {
    LayoutNode root = WrapRow(70.0f, 5.0f, 0);
    root.style.direction = LayoutDirection::Row;
    root.children = {BaselineItem(20.0f, 10.0f), BaselineItem(30.0f, 25.0f),    // line 1: ascent 25, descent 10 -> 35
                     BaselineItem(10.0f, 10.0f), BaselineItem(20.0f, 5.0f)};     // line 2: ascent 10, descent 15 -> 25
    Run(root, 500.0f, 500.0f);
    CHECK(Near(root.children[0].rect.position.y, 15.0f));   // line 1: 25 - 10
    CHECK(Near(root.children[1].rect.position.y, 0.0f));
    CHECK(Near(root.children[2].rect.position.y, 35.0f + 5.0f + 0.0f));       // line 2 starts at 40: ascent 10 - 10 = 0
    CHECK(Near(root.children[3].rect.position.y, 35.0f + 5.0f + 5.0f));
    CHECK(Near(root.measured.y, 35.0f + 5.0f + 25.0f));
}

void TestBaselineFromTextMetrics() {
    // Two labels with different font sizes sit on the same baseline: ascent = fontSize * 0.8.
    LayoutNode root;
    root.style.direction = LayoutDirection::Row;
    LayoutNode small = Box(60.0f, 12.0f * 1.2f);
    small.style.alignItems = AlignItems::Baseline;
    small.style.baseline = 12.0f * 0.8f;
    LayoutNode big = Box(80.0f, 28.0f * 1.2f);
    big.style.alignItems = AlignItems::Baseline;
    big.style.baseline = 28.0f * 0.8f;
    root.children = {small, big};
    Run(root, 500.0f, 500.0f);
    CHECK(Near(root.children[0].rect.position.y + 12.0f * 0.8f, root.children[1].rect.position.y + 28.0f * 0.8f));
}

// ------------------------------------------------------------------ robustness + regression
void TestNonFiniteGapAndSizesStayFinite() {
    LayoutNode root = WrapRow(100.0f, std::nanf(""), 4);
    root.children[1].style.margin = {0, std::nanf(""), 0, INFINITY}; // top = NaN, bottom = Inf
    root.children[2].style.baseline = std::nanf("");
    root.children[2].style.alignItems = AlignItems::Baseline;
    Run(root, 500.0f, 500.0f);
    bool finite = std::isfinite(root.measured.x) && std::isfinite(root.measured.y);
    for (const auto& c : root.children) {
        finite = finite && std::isfinite(c.rect.position.x) && std::isfinite(c.rect.position.y);
    }
    CHECK(finite);
}

LayoutNode RandomTree(int depth, bool allowWrapAndBaseline) {
    auto rnd = [](int lo, int hi) { return lo + std::rand() % (hi - lo + 1); };
    auto length = [&]() {
        switch (rnd(0, 3)) {
        case 0: return Length{SizeMode::Fixed, static_cast<float>(rnd(8, 90))};
        case 1: return Length{SizeMode::Percent, static_cast<float>(rnd(2, 9)) / 10.0f};
        case 2: return Length{SizeMode::Flex, 0.0f};
        default: return Length{SizeMode::Auto, 0.0f};
        }
    };
    LayoutNode n;
    n.style.direction = rnd(0, 1) ? LayoutDirection::Row : LayoutDirection::Column;
    n.style.width = length();
    n.style.height = length();
    n.style.gap = static_cast<float>(rnd(0, 10));
    n.style.padding = {static_cast<float>(rnd(0, 8)), static_cast<float>(rnd(0, 8)), static_cast<float>(rnd(0, 8)), static_cast<float>(rnd(0, 8))};
    n.style.margin = {static_cast<float>(rnd(0, 6)), static_cast<float>(rnd(0, 6)), static_cast<float>(rnd(0, 6)), static_cast<float>(rnd(0, 6))};
    n.style.grow = static_cast<float>(rnd(0, 3));
    n.style.shrink = static_cast<float>(rnd(0, 2));
    n.style.justifyContent = static_cast<JustifyContent>(rnd(0, 5));
    n.style.alignItems = static_cast<AlignItems>(rnd(0, allowWrapAndBaseline ? 4 : 3));
    if (rnd(0, 3) == 0) n.style.minSize = {static_cast<float>(rnd(0, 40)), static_cast<float>(rnd(0, 40))};
    if (rnd(0, 3) == 0) n.style.maxSize = {static_cast<float>(rnd(40, 200)), static_cast<float>(rnd(40, 200))};
    if (allowWrapAndBaseline) {
        n.style.wrap = rnd(0, 1) ? FlexWrap::Wrap : FlexWrap::NoWrap;
        n.style.alignContent = static_cast<AlignContent>(rnd(0, 6));
        n.style.baseline = rnd(0, 1) ? static_cast<float>(rnd(0, 30)) : -1.0f;
    }
    if (depth > 0) {
        const int kids = rnd(0, 5);
        for (int i = 0; i < kids; ++i) n.children.push_back(RandomTree(depth - 1, allowWrapAndBaseline));
    }
    return n;
}

bool SameLayout(const LayoutNode& a, const LayoutNode& b) {
    if (a.children.size() != b.children.size()) return false;
    const auto near = [](float x, float y) { return std::abs(x - y) <= 1.0e-3f * (1.0f + std::abs(x)); };
    if (!near(a.rect.position.x, b.rect.position.x) || !near(a.rect.position.y, b.rect.position.y) ||
        !near(a.rect.size.x, b.rect.size.x) || !near(a.rect.size.y, b.rect.size.y) ||
        !near(a.measured.x, b.measured.x) || !near(a.measured.y, b.measured.y)) {
        return false;
    }
    for (std::size_t i = 0; i < a.children.size(); ++i) {
        if (!SameLayout(a.children[i], b.children[i])) return false;
    }
    return true;
}

// Differential test: without wrap and without baseline the refactored engine must equal the frozen
// legacy engine on any tree.
void TestMatchesLegacyEngineWithoutWrapOrBaseline() {
    std::srand(1234);
    bool identical = true;
    int firstMismatch = -1;
    for (int it = 0; it < 4000; ++it) {
        LayoutNode current = RandomTree(3, false);
        LayoutNode legacy = current;
        const Vec2 viewport{static_cast<float>(100 + std::rand() % 700), static_cast<float>(100 + std::rand() % 500)};

        LayoutEngine engine;
        (void)engine.Measure(current, viewport);
        engine.Arrange(current, {{0.0f, 0.0f}, viewport});

        Legacy::LegacyLayoutEngine reference;
        (void)reference.Measure(legacy, viewport);
        reference.Arrange(legacy, {{0.0f, 0.0f}, viewport});

        if (!SameLayout(current, legacy)) {
            identical = false;
            if (firstMismatch < 0) firstMismatch = it;
        }
    }
    CHECK(identical);
    CHECK(firstMismatch == -1);
}

bool AllFinite(const LayoutNode& n) {
    bool ok = std::isfinite(n.rect.position.x) && std::isfinite(n.rect.position.y) &&
              n.rect.size.x >= 0.0f && n.rect.size.y >= 0.0f && std::isfinite(n.rect.size.x) && std::isfinite(n.rect.size.y) &&
              std::isfinite(n.measured.x) && std::isfinite(n.measured.y);
    for (const auto& c : n.children) ok = ok && AllFinite(c);
    return ok;
}

void TestRandomWrapAndBaselineTreesStayFinite() {
    std::srand(77);
    bool finite = true;
    bool idempotent = true;
    for (int it = 0; it < 3000; ++it) {
        LayoutNode root = RandomTree(3, true);
        const Vec2 viewport{static_cast<float>(50 + std::rand() % 800), static_cast<float>(50 + std::rand() % 600)};
        LayoutEngine engine;
        (void)engine.Measure(root, viewport);
        engine.Arrange(root, {{0.0f, 0.0f}, viewport});
        finite = finite && AllFinite(root);
        // Measuring twice (the engine caches nothing) must give the same answer.
        const Vec2 first = root.measured;
        (void)engine.Measure(root, viewport);
        idempotent = idempotent && Near(first.x, root.measured.x, 1.0e-3f) && Near(first.y, root.measured.y, 1.0e-3f);
    }
    CHECK(finite);
    CHECK(idempotent);
}

// A wrapped row never has an item crossing the container's main edge unless that item alone is wider.
void TestWrappedItemsStayInsideTheContainerWidth() {
    std::srand(5150);
    bool inside = true;
    for (int it = 0; it < 2000; ++it) {
        LayoutNode root;
        root.style.direction = LayoutDirection::Row;
        root.style.wrap = FlexWrap::Wrap;
        const float width = static_cast<float>(60 + std::rand() % 200);
        root.style.width = {SizeMode::Fixed, width};
        root.style.gap = static_cast<float>(std::rand() % 12);
        const int items = 1 + std::rand() % 12;
        for (int i = 0; i < items; ++i) {
            LayoutNode c = Box(static_cast<float>(10 + std::rand() % 50), static_cast<float>(8 + std::rand() % 30));
            c.style.shrink = 0.0f;
            root.children.push_back(c);
        }
        Run(root, 1000.0f, 1000.0f);
        for (const auto& c : root.children) {
            inside = inside && c.rect.position.x >= -1.0e-3f && c.rect.position.x + c.rect.size.x <= width + 1.0e-3f;
        }
    }
    CHECK(inside); // item widths are at most 59 and containers at least 60: every item fits on some line
}

} // namespace

void RunFlexWrapTests() {
    TestRowWrapPacksLinesAndMeasuresHeight();
    TestNoWrapStillOverflowsOrShrinks();
    TestWrapJustifyAppliesPerLine();
    TestWrapGrowFillsEachLine();
    TestWrapMarginsCountTowardsTheLine();
    TestAlignContentDistributesLines();
    TestAlignContentStretchGrowsLinesAndStretchItems();
    TestColumnWrapFlowsIntoColumns();
    TestWrapMeasureUsesAvailableWidthWhenAuto();
    TestWrappedContainerInsideColumnGetsItsHeight();
    TestNonFiniteGapAndSizesStayFinite();
    TestMatchesLegacyEngineWithoutWrapOrBaseline();
    TestRandomWrapAndBaselineTreesStayFinite();
    TestWrappedItemsStayInsideTheContainerWidth();
}

void RunFlexBaselineTests() {
    TestBaselineAlignsItemsOnASharedLine();
    TestBaselineWithMarginsAndMixedAlignment();
    TestBaselineIsIgnoredInColumns();
    TestBaselinePerLineWithWrap();
    TestBaselineFromTextMetrics();
}

} // namespace SpriteUiTests
