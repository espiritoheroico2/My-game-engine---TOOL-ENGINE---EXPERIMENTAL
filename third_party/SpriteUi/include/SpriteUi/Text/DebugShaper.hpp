#pragma once

#include "SpriteUi/Text/FontMetrics.hpp"
#include "SpriteUi/Text/GlyphInstance.hpp"

#include <string_view>
#include <vector>

namespace ToolEngine::SpriteUi::Text {

struct DebugShaperConfig {
    float advanceEm = 0.6f;      // Narrow characters (Latin, digits, punctuation).
    float wideAdvanceEm = 1.0f;  // CJK / emoji.
    float tabSpaces = 4.0f;      // A tab advances this many space widths.
};

// Reference shaper for tests, tools and screenshots. NOT a replacement for HarfBuzz: no kerning,
// ligatures or bidi. It reproduces the contract LayoutText relies on:
//  - glyphs are in logical (= visual, LTR) order;
//  - GlyphInstance::clusterByteOffset is the byte offset of the cluster start in the source text;
//  - every cluster gets >= 1 glyph; combining marks add a zero-advance glyph to the cluster of their
//    base, like HarfBuzz does (a cluster with several glyphs);
//  - planeBounds are in font units (y up from the baseline): visible glyphs get a box, spaces and
//    controls get none (zero-size quad).
[[nodiscard]] std::vector<GlyphInstance> ShapeDebug(
    std::string_view utf8,
    const FontMetrics& metrics,
    const DebugShaperConfig& config = {});

} // namespace ToolEngine::SpriteUi::Text
