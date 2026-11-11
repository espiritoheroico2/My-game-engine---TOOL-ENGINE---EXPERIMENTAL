#pragma once

// FROZEN copy of the pre-V0.6 Flexbox engine (git: "Harden V4.2 compliance and expand UI core").
// Test-only reference: the differential tests prove the refactored LayoutEngine produces identical
// results whenever wrap/baseline are not used. Do not "fix" this file.

#include "SpriteUi/Layout/Layout.hpp"

namespace ToolEngine::SpriteUi::Legacy {

class LegacyLayoutEngine {
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

} // namespace ToolEngine::SpriteUi::Legacy
