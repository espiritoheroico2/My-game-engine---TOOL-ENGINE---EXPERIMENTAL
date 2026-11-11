#pragma once

#include "SpriteUi/Core/Math.hpp"

#include <cstdint>
#include <limits>
#include <vector>

namespace ToolEngine::SpriteUi {

enum class LayoutDirection : std::uint8_t {
    Row,
    Column
};

// Cross-axis alignment of an ITEM (read from the child's own style, i.e. CSS align-self).
// Baseline aligns the baselines of all Baseline items of a line (Row direction only; in a Column it
// behaves like Start). An item's baseline is FlexItem::baseline; without one it is the bottom edge.
enum class AlignItems : std::uint8_t {
    Stretch,
    Start,
    Center,
    End,
    Baseline
};

// Container property: whether children that do not fit the main axis start a new line.
enum class FlexWrap : std::uint8_t {
    NoWrap,
    Wrap
};

// Container property (only meaningful with FlexWrap::Wrap): how the lines are distributed along the
// cross axis when there is free cross space. Unlike CSS the default is Start, because stretching
// every line to fill the container surprises UI authors (tag lists, toolbars).
enum class AlignContent : std::uint8_t {
    Start,
    Center,
    End,
    SpaceBetween,
    SpaceAround,
    SpaceEvenly,
    Stretch
};

enum class JustifyContent : std::uint8_t {
    FlexStart,
    Center,
    FlexEnd,
    SpaceBetween,
    SpaceAround,
    SpaceEvenly
};

enum class SizeMode : std::uint8_t {
    Fixed,
    Percent,
    Auto,
    Flex
};

struct Length {
    SizeMode mode = SizeMode::Auto;
    float value = 0.0f;
};

struct Edges {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

struct FlexItem {
    Length width{};
    Length height{};
    float grow = 0.0f;
    float shrink = 1.0f;
    Edges margin{};
    Edges padding{};
    float gap = 0.0f;
    LayoutDirection direction = LayoutDirection::Row;
    AlignItems alignItems = AlignItems::Stretch;
    JustifyContent justifyContent = JustifyContent::FlexStart;
    Vec2 minSize{0.0f, 0.0f};
    Vec2 maxSize{
        std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::infinity()
    };

    // Container: multi-line layout. The cross-axis gap between lines is `gap` as well.
    FlexWrap wrap = FlexWrap::NoWrap;
    AlignContent alignContent = AlignContent::Start;

    // Item: distance from the top edge of the item's box to its first baseline (Row direction).
    // Negative (default) means "no baseline": the bottom edge is used, like a box without text.
    float baseline = -1.0f;
};

struct LayoutNode {
    FlexItem style{};
    Rect rect{};
    Vec2 measured{};
    std::vector<LayoutNode> children;
};

struct LayoutStats {
    std::uint32_t measured = 0;
    std::uint32_t arranged = 0;
};

class LayoutEngine {
public:
    [[nodiscard]] Vec2 Measure(
        LayoutNode& node,
        Vec2 available,
        LayoutStats* stats = nullptr) const;

    void Arrange(
        LayoutNode& node,
        Rect bounds,
        LayoutStats* stats = nullptr) const;

private:
    [[nodiscard]] Vec2 MeasureImpl(
        LayoutNode& node,
        Vec2 available,
        LayoutStats* stats) const;

    void ArrangeImpl(
        LayoutNode& node,
        Rect bounds,
        LayoutStats* stats) const;
};

} // namespace ToolEngine::SpriteUi
