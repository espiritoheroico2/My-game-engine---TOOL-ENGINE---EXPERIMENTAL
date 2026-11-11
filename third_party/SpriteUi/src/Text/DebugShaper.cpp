#include "SpriteUi/Text/DebugShaper.hpp"

#include "SpriteUi/Text/LineBreak.hpp"

#include <algorithm>
#include <cmath>

namespace ToolEngine::SpriteUi::Text {

namespace {

[[nodiscard]] bool IsWideCodePoint(char32_t c) noexcept {
    return (c >= 0x3040 && c <= 0x9FFF) || (c >= 0xAC00 && c <= 0xD7AF) || (c >= 0xF900 && c <= 0xFAFF) ||
           (c >= 0xFF01 && c <= 0xFF60) || (c >= 0x1F1E6 && c <= 0x1FAFF) || (c >= 0x20000 && c <= 0x2FFFF);
}

} // namespace

std::vector<GlyphInstance> ShapeDebug(
    std::string_view utf8, const FontMetrics& metrics, const DebugShaperConfig& config)
{
    std::vector<GlyphInstance> glyphs;
    if (!metrics.IsValid()) return glyphs;

    const float narrow = std::isfinite(config.advanceEm) ? std::max(0.0f, config.advanceEm) : 0.6f;
    const float wide = std::isfinite(config.wideAdvanceEm) ? std::max(0.0f, config.wideAdvanceEm) : 1.0f;
    const float tab = std::isfinite(config.tabSpaces) ? std::max(0.0f, config.tabSpaces) : 4.0f;

    const std::vector<TextCluster> clusters = AnalyzeClusters(utf8);
    glyphs.reserve(clusters.size());
    for (const TextCluster& cluster : clusters) {
        GlyphInstance g;
        g.glyphId = static_cast<std::uint32_t>(cluster.base);
        g.clusterByteOffset = cluster.byteBegin;

        if (cluster.kind == ClusterKind::Newline) {
            glyphs.push_back(g); // Zero advance, no box: keeps the cluster addressable (caret mapping).
            continue;
        }

        const bool isTab = cluster.base == '\t';
        const bool isWide = IsWideCodePoint(cluster.base);
        const float advanceEm = isWide ? wide : narrow;
        g.xAdvance = (isTab ? narrow * tab : advanceEm) * metrics.unitsPerEm;
        if (cluster.base == 0x200B || cluster.base == 0x2060 || cluster.base == 0xFEFF) g.xAdvance = 0.0f;

        const bool visible = cluster.kind == ClusterKind::Normal && g.xAdvance > 0.0f;
        if (visible) {
            g.planeBounds[0] = g.xAdvance * 0.1f;
            g.planeBounds[1] = 0.0f;
            g.planeBounds[2] = g.xAdvance * 0.9f;
            g.planeBounds[3] = metrics.ascent * 0.7f;
            g.uvRect[2] = 0.05f;
            g.uvRect[3] = 0.05f;
        }
        glyphs.push_back(g);

        // Combining marks: extra zero-advance glyph in the SAME cluster (HarfBuzz behaviour).
        const std::size_t extra = static_cast<std::size_t>(cluster.byteEnd - cluster.byteBegin);
        if (visible && extra > 1) {
            std::size_t pos = cluster.byteBegin;
            // Count code points after the first one.
            std::size_t first = 1;
            while (first < extra && (static_cast<unsigned char>(utf8[pos + first]) & 0xC0u) == 0x80u) ++first;
            if (pos + first < cluster.byteEnd) {
                GlyphInstance mark;
                mark.glyphId = 0x300; // Debug placeholder: "a combining mark".
                mark.clusterByteOffset = cluster.byteBegin;
                glyphs.push_back(mark);
            }
        }
    }
    return glyphs;
}

} // namespace ToolEngine::SpriteUi::Text
