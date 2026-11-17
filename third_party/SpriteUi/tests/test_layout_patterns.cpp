// Camada L0 (layout): os construtores de padroes (Layout/LayoutPatterns.hpp) produzem posicoes e tamanhos
// corretos quando passam pelo LayoutTree, que e como o consumidor os usa.
#include "SpriteUi/Layout/LayoutPatterns.hpp"
#include "SpriteUi/Layout/LayoutTree.hpp"
#include "TestHarness.hpp"

#include <cstdio>

namespace {

using namespace ToolEngine::SpriteUi;
using namespace ToolEngine::SpriteUi::Patterns;
using SpriteUiTests::Near;

Rect RectOf(const LayoutTree& tree, UIHandle handle) {
    const auto* el = tree.Get(handle);
    return el ? el->node.rect : Rect{};
}

bool RectIs(const LayoutTree& tree, UIHandle handle, float x, float y, float w, float h) {
    const Rect r = RectOf(tree, handle);
    return Near(r.position.x, x, 1.0e-3f) && Near(r.position.y, y, 1.0e-3f) && Near(r.size.x, w, 1.0e-3f) &&
           Near(r.size.y, h, 1.0e-3f);
}

Length Fixed(float v) {
    return Length{SizeMode::Fixed, v};
}
Length Percent(float v) {
    return Length{SizeMode::Percent, v};
}

void TestSizedKeepsItsSizeWhenTheColumnOverflows() {
    LayoutTree tree;
    const UIHandle root = tree.Create({}, Column(Fixed(100.0f), Fixed(30.0f)));
    const UIHandle a = tree.Create(root, Sized(50.0f, 20.0f));
    const UIHandle b = tree.Create(root, Sized(50.0f, 20.0f));
    tree.Layout({{0.0f, 0.0f}, {100.0f, 30.0f}});
    CHECK(RectIs(tree, a, 0.0f, 0.0f, 50.0f, 20.0f));
    CHECK(RectIs(tree, b, 0.0f, 20.0f, 50.0f, 20.0f)); // 40 px of content in 30 px: still 20 px, not shrunk
}

void TestRowGapPlacesChildrenSideBySide() {
    LayoutTree tree;
    const UIHandle row = tree.Create({}, Row(Fixed(200.0f), Fixed(20.0f), 8.0f));
    const UIHandle a = tree.Create(row, Sized(50.0f, 20.0f));
    const UIHandle b = tree.Create(row, Sized(30.0f, 20.0f));
    tree.Layout({{0.0f, 0.0f}, {200.0f, 20.0f}});
    CHECK(RectIs(tree, a, 0.0f, 0.0f, 50.0f, 20.0f));
    CHECK(RectIs(tree, b, 58.0f, 0.0f, 30.0f, 20.0f)); // 50 + gap 8
}

void TestWidthFractionAndFillWidthUseTheParentWidth() {
    LayoutTree tree;
    const UIHandle root = tree.Create({}, Column(Fixed(240.0f), Fixed(100.0f)));
    const UIHandle half = tree.Create(root, WidthFraction(0.5f, 20.0f));
    const UIHandle full = tree.Create(root, FillWidth(20.0f));
    tree.Layout({{0.0f, 0.0f}, {240.0f, 100.0f}});
    CHECK(RectIs(tree, half, 0.0f, 0.0f, 120.0f, 20.0f));
    CHECK(RectIs(tree, full, 0.0f, 20.0f, 240.0f, 20.0f));
}

void TestSpacerTakesTheFreeSpaceOfItsRow() {
    LayoutTree tree;
    const UIHandle row = tree.Create({}, Row(Fixed(200.0f), Fixed(20.0f)));
    const UIHandle left = tree.Create(row, Sized(50.0f, 20.0f));
    const UIHandle spacer = tree.Create(row, Spacer());
    const UIHandle right = tree.Create(row, Sized(50.0f, 20.0f));
    tree.Layout({{0.0f, 0.0f}, {200.0f, 20.0f}});
    CHECK(RectIs(tree, left, 0.0f, 0.0f, 50.0f, 20.0f));
    CHECK(Near(RectOf(tree, spacer).size.x, 100.0f)); // 200 - 50 - 50
    CHECK(Near(RectOf(tree, spacer).position.x, 50.0f));
    CHECK(RectIs(tree, right, 150.0f, 0.0f, 50.0f, 20.0f));
}

void TestGrowCellTakesTheRestOfTheRow() {
    LayoutTree tree;
    const UIHandle row = tree.Create({}, Row(Fixed(300.0f), Fixed(30.0f), 8.0f));
    const UIHandle label = tree.Create(row, Sized(100.0f, 30.0f));
    const UIHandle cell = tree.Create(row, Grow(30.0f));
    tree.Layout({{0.0f, 0.0f}, {300.0f, 30.0f}});
    CHECK(RectIs(tree, label, 0.0f, 0.0f, 100.0f, 30.0f));
    CHECK(RectIs(tree, cell, 108.0f, 0.0f, 192.0f, 30.0f)); // 300 - 100 - gap 8
}

void TestColumnStacksChildrenWithGap() {
    LayoutTree tree;
    const UIHandle column = tree.Create({}, Column(Fixed(100.0f), Fixed(100.0f), 4.0f));
    const UIHandle a = tree.Create(column, Sized(50.0f, 20.0f));
    const UIHandle b = tree.Create(column, Sized(50.0f, 20.0f));
    tree.Layout({{0.0f, 0.0f}, {100.0f, 100.0f}});
    CHECK(RectIs(tree, a, 0.0f, 0.0f, 50.0f, 20.0f));
    CHECK(RectIs(tree, b, 0.0f, 24.0f, 50.0f, 20.0f)); // 20 + gap 4
}

void TestCenteredPlacesChildInTheMiddle() {
    LayoutTree tree;
    const UIHandle root = tree.Create({}, Column(Fixed(200.0f), Fixed(100.0f)));
    const UIHandle box = tree.Create(root, Centered(Percent(1.0f), Percent(1.0f)));
    const UIHandle child = tree.Create(box, CenteredItem(40.0f, 20.0f));
    tree.Layout({{0.0f, 0.0f}, {200.0f, 100.0f}});
    CHECK(RectIs(tree, box, 0.0f, 0.0f, 200.0f, 100.0f));
    CHECK(RectIs(tree, child, 80.0f, 40.0f, 40.0f, 20.0f)); // (200 - 40) / 2, (100 - 20) / 2
}

void TestCenteredOnlyAlongTheMainAxisWithoutCenteredItem() {
    LayoutTree row;
    const UIHandle box = row.Create({}, Centered(Fixed(200.0f), Fixed(100.0f)));
    const UIHandle plain = row.Create(box, Sized(40.0f, 20.0f));
    row.Layout({{0.0f, 0.0f}, {200.0f, 100.0f}});
    CHECK(RectIs(row, plain, 80.0f, 0.0f, 40.0f, 20.0f)); // the row centres x only; y stays at the top

    LayoutTree column;
    const UIHandle stack = column.Create({}, Column(Fixed(200.0f), Fixed(100.0f)));
    const UIHandle item = column.Create(stack, CenteredItem(40.0f, 20.0f));
    column.Layout({{0.0f, 0.0f}, {200.0f, 100.0f}});
    CHECK(RectIs(column, item, 80.0f, 0.0f, 40.0f, 20.0f)); // the column centres x (its cross axis)
}

void TestLabeledFieldRowSplitsLabelAndField() {
    LayoutTree tree;
    const UIHandle root = tree.Create({}, Column(Fixed(300.0f), Fixed(200.0f)));
    const LabeledFieldNodes field = CreateLabeledField(tree, root, 100.0f, 30.0f, 8.0f);
    CHECK(field.row.IsValid() && field.label.IsValid() && field.field.IsValid());
    tree.Layout({{0.0f, 0.0f}, {300.0f, 200.0f}});
    CHECK(RectIs(tree, field.row, 0.0f, 0.0f, 300.0f, 30.0f));
    CHECK(RectIs(tree, field.label, 0.0f, 0.0f, 100.0f, 30.0f));
    CHECK(RectIs(tree, field.field, 108.0f, 0.0f, 192.0f, 30.0f));
}

void TestLabeledFieldWithStaleParentCreatesNothing() {
    LayoutTree tree;
    const UIHandle root = tree.Create({}, Column(Fixed(300.0f), Fixed(200.0f)));
    CHECK(tree.Remove(root));
    const LabeledFieldNodes nodes = CreateLabeledField(tree, root, 100.0f, 30.0f);
    CHECK(!nodes.row.IsValid());
    CHECK(!nodes.label.IsValid());
    CHECK(!nodes.field.IsValid());
    CHECK(tree.Roots().empty());
}

void TestTokensDocumentTheEditorScale() {
    const LayoutTokens tokens;
    CHECK(Near(tokens.spaceSm, 8.0f));
    CHECK(Near(tokens.spaceMd, 16.0f));
    CHECK(Near(tokens.controlHeight, 30.0f));
    CHECK(Near(tokens.buttonHeight, 34.0f));
}

} // namespace

int main() {
    TestSizedKeepsItsSizeWhenTheColumnOverflows();
    TestRowGapPlacesChildrenSideBySide();
    TestWidthFractionAndFillWidthUseTheParentWidth();
    TestSpacerTakesTheFreeSpaceOfItsRow();
    TestGrowCellTakesTheRestOfTheRow();
    TestColumnStacksChildrenWithGap();
    TestCenteredPlacesChildInTheMiddle();
    TestCenteredOnlyAlongTheMainAxisWithoutCenteredItem();
    TestLabeledFieldRowSplitsLabelAndField();
    TestLabeledFieldWithStaleParentCreatesNothing();
    TestTokensDocumentTheEditorScale();

    if (SpriteUiTests::failures != 0) {
        std::fprintf(stderr, "layout patterns: %d failure(s) in %d checks.\n", SpriteUiTests::failures,
                     SpriteUiTests::testsRun);
        return 1;
    }
    std::printf("layout patterns: %d/%d checks passed\n", SpriteUiTests::testsRun, SpriteUiTests::testsRun);
    return 0;
}
