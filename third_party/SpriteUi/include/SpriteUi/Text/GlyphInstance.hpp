#pragma once

#include <cstdint>

namespace ToolEngine::SpriteUi::Text {

// Shaped glyph placement. HarfBuzz supplies glyphId, clusterByteOffset,
// advances and offsets; the atlas resolver fills uvRect, planeBounds and page.
struct GlyphInstance {
    std::uint32_t glyphId = 0;
    std::uint32_t clusterByteOffset = 0;
    float xAdvance = 0.0f;
    float yAdvance = 0.0f;
    float xOffset = 0.0f;
    float yOffset = 0.0f;
    float uvRect[4]{};
    float planeBounds[4]{};
    std::uint16_t atlasPage = 0;
};

} // namespace ToolEngine::SpriteUi::Text
