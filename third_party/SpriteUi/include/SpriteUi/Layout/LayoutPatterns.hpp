#pragma once

// Builders for the layout patterns that consumers used to write as FlexItem literals (see
// docs/COMPOSITION.md).
//
// Most functions only return a FlexItem (a style). They never create nodes, so they can be used with any
// LayoutTree::Create or LayoutNode. CreateLabeledField is the one exception: it creates its nodes in a
// LayoutTree and returns their handles, which is the convention for a composed widget.
//
// Conventions shared by all builders:
//  - sizes are logical pixels; Percent takes a fraction of the parent's content area (1.0 = full width);
//  - fixed sizes do not shrink (shrink = 0), so a column or row never squeezes a control to zero;
//  - Spacer and Grow take the free space of their line through `grow`.

#include "SpriteUi/Layout/Layout.hpp"
#include "SpriteUi/Layout/LayoutTree.hpp"

namespace ToolEngine::SpriteUi::Patterns {

// Spacing and size scale in logical pixels. Defaults follow the editor start screen (8 px grid).
struct LayoutTokens {
    float spaceXs = 4.0f;
    float spaceSm = 8.0f;
    float spaceMd = 16.0f;
    float spaceLg = 24.0f;
    float labelHeight = 18.0f;
    float controlHeight = 30.0f; // Text fields and single-line controls.
    float buttonHeight = 34.0f;
};

// Fixed width and height. Does not shrink.
[[nodiscard]] inline FlexItem Sized(float width, float height) noexcept {
    FlexItem s;
    s.width = {SizeMode::Fixed, width};
    s.height = {SizeMode::Fixed, height};
    s.shrink = 0.0f;
    return s;
}

// Width is a fraction of the parent's content width (1.0 = full width), height is fixed.
[[nodiscard]] inline FlexItem WidthFraction(float fraction, float height) noexcept {
    FlexItem s;
    s.width = {SizeMode::Percent, fraction};
    s.height = {SizeMode::Fixed, height};
    s.shrink = 0.0f;
    return s;
}

// Full parent width with a fixed height: the usual row of a form or a toolbar.
[[nodiscard]] inline FlexItem FillWidth(float height) noexcept {
    return WidthFraction(1.0f, height);
}

// Row container (children side by side). `width` and `height` are the container's own size.
[[nodiscard]] inline FlexItem Row(Length width, Length height, float gap = 0.0f) noexcept {
    FlexItem s;
    s.width = width;
    s.height = height;
    s.shrink = 0.0f;
    s.direction = LayoutDirection::Row;
    s.alignItems = AlignItems::Stretch;
    s.gap = gap;
    return s;
}

// Column container (children stacked top to bottom).
[[nodiscard]] inline FlexItem Column(Length width, Length height, float gap = 0.0f) noexcept {
    FlexItem s;
    s.width = width;
    s.height = height;
    s.shrink = 0.0f;
    s.direction = LayoutDirection::Column;
    s.alignItems = AlignItems::Stretch;
    s.gap = gap;
    return s;
}

// Row container that centres its children along x (main axis). The cross axis (y) is per child, because
// AlignItems is read from the child's own style: give the child CenteredItem to centre it on y as well.
[[nodiscard]] inline FlexItem Centered(Length width, Length height) noexcept {
    FlexItem s;
    s.width = width;
    s.height = height;
    s.shrink = 0.0f;
    s.direction = LayoutDirection::Row;
    s.justifyContent = JustifyContent::Center;
    return s;
}

// Fixed-size child that is centred on the cross axis of its parent (y in a Row, x in a Column). Use it
// with Centered to centre a child on both axes.
[[nodiscard]] inline FlexItem CenteredItem(float width, float height) noexcept {
    FlexItem s = Sized(width, height);
    s.alignItems = AlignItems::Center;
    return s;
}

// Takes the free space of its line (an empty item that pushes its siblings apart).
[[nodiscard]] inline FlexItem Spacer(float grow = 1.0f) noexcept {
    FlexItem s;
    s.grow = grow;
    return s;
}

// Flexible cell that takes the rest of a row, with a fixed height. Its width is what is left after the
// fixed siblings and the gaps.
[[nodiscard]] inline FlexItem Grow(float height, float grow = 1.0f) noexcept {
    FlexItem s;
    s.height = {SizeMode::Fixed, height};
    s.grow = grow;
    s.shrink = 0.0f;
    return s;
}

// Nodes created by CreateLabeledField. All three belong to the caller's tree; remove `row` to drop them all.
struct LabeledFieldNodes {
    UIHandle row{};
    UIHandle label{};
    UIHandle field{};
};

// A row with a label of fixed width on the left and a field that takes the rest, `gap` apart.
// Both children are `height` tall. The label and field have no visual or text; the caller attaches those.
[[nodiscard]] inline LabeledFieldNodes CreateLabeledField(LayoutTree& tree, UIHandle parent, float labelWidth,
                                                          float height, float gap = 8.0f) {
    LabeledFieldNodes nodes;
    nodes.row =
        tree.Create(parent, Row(Length{SizeMode::Percent, 1.0f}, Length{SizeMode::Fixed, height}, gap));
    if (!nodes.row.IsValid()) return nodes;
    nodes.label = tree.Create(nodes.row, Sized(labelWidth, height));
    nodes.field = tree.Create(nodes.row, Grow(height));
    return nodes;
}

} // namespace ToolEngine::SpriteUi::Patterns
