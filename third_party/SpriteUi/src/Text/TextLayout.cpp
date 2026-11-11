#include "SpriteUi/Text/TextLayout.hpp"

#include "SpriteUi/Rendering/RenderFlags.hpp"
#include "SpriteUi/Text/LineBreak.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ToolEngine::SpriteUi::Text {

namespace {

struct LayoutScale {
    float font = 1.0f;       // font units -> layout units
    float lineHeight = 0.0f;
    float ascent = 0.0f;
};

struct LineInfo {
    std::size_t startQuad = 0;
    std::size_t endQuad = 0;
    float width = 0.0f;
    float y = 0.0f;
    float height = 0.0f;
    std::uint32_t byteBegin = 0;
    std::uint32_t byteEnd = 0;
    bool endsWithNewline = false;
};

[[nodiscard]] float FiniteOrZero(float value) noexcept {
    return IsFinite(value) ? value : 0.0f;
}

[[nodiscard]] LayoutScale ComputeScale(const FontMetrics& metrics, const TextLayoutConfig& config) noexcept {
    const float fontSize = (config.fontSize > 0.0f && IsFinite(config.fontSize))
        ? config.fontSize
        : metrics.fontSize;
    LayoutScale scale;
    scale.font = (metrics.unitsPerEm > 0.0f) ? (fontSize / metrics.unitsPerEm) : 1.0f;
    const float multiplier = IsFinite(config.lineHeightMultiplier) ? std::max(0.1f, config.lineHeightMultiplier) : 1.0f;
    scale.lineHeight = metrics.GetLineHeight() * scale.font * multiplier;
    scale.ascent = metrics.ascent * scale.font;
    return scale;
}

// Quad for one glyph whose pen is at penX on a line whose top is lineTop. glyph.yOffset (HarfBuzz
// raises combining marks with it) shifts the box up; positive = up, like planeBounds.
[[nodiscard]] GlyphQuad MakeQuad(const GlyphInstance& glyph, float penX, float lineTop, const LayoutScale& scale) noexcept {
    GlyphQuad quad;
    quad.glyphId = glyph.glyphId;
    quad.clusterByteOffset = glyph.clusterByteOffset;
    quad.atlasPage = glyph.atlasPage;
    for (int k = 0; k < 4; ++k) {
        quad.uvRect[k] = glyph.uvRect[k];
    }

    const float quadW = (glyph.planeBounds[2] - glyph.planeBounds[0]) * scale.font;
    const float quadH = (glyph.planeBounds[3] - glyph.planeBounds[1]) * scale.font;
    if (quadW > 0.0f && quadH > 0.0f && IsFinite(quadW) && IsFinite(quadH)) {
        const float quadX = penX + FiniteOrZero(glyph.xOffset) * scale.font + glyph.planeBounds[0] * scale.font;
        // Baseline is at lineTop + ascent; planeBounds[3] is the distance above the baseline.
        const float quadY = lineTop + scale.ascent - (glyph.planeBounds[3] + FiniteOrZero(glyph.yOffset)) * scale.font;
        quad.bounds = {{quadX, quadY}, {quadW, quadH}};
    } else {
        quad.bounds = {{penX, lineTop}, {0.0f, 0.0f}};
    }
    return quad;
}

// Shifts each line for Center/Right alignment and publishes the TextLine table.
void FinalizeLines(
    TextLayoutResult& result,
    const std::vector<LineInfo>& lines,
    const LayoutScale& scale,
    const TextLayoutConfig& config,
    float containerWidth)
{
    result.lines.clear();
    result.lines.reserve(lines.size());
    for (const LineInfo& line : lines) {
        float shiftX = 0.0f;
        if (containerWidth > 0.0f) {
            if (config.alignment == TextAlignment::Center) {
                shiftX = std::max(0.0f, containerWidth - line.width) * 0.5f;
            } else if (config.alignment == TextAlignment::Right) {
                shiftX = std::max(0.0f, containerWidth - line.width);
            }
        }
        if (shiftX > 0.0f) {
            for (std::size_t i = line.startQuad; i < line.endQuad; ++i) {
                result.quads[i].bounds.position.x += shiftX;
                result.quads[i].penOrigin.x += shiftX;
            }
        }
        TextLine out;
        out.firstQuad = line.startQuad;
        out.quadCount = line.endQuad - line.startQuad;
        out.x = shiftX;
        out.y = line.y;
        out.width = line.width;
        out.height = line.height;
        out.baseline = line.y + scale.ascent;
        out.byteBegin = line.byteBegin;
        out.byteEnd = line.byteEnd;
        out.endsWithNewline = line.endsWithNewline;
        result.lines.push_back(out);
    }
    result.lineHeight = scale.lineHeight;
    result.containerWidth = containerWidth;
    result.alignment = config.alignment;
    result.lineCount = lines.size();
}

} // namespace

TextLayoutResult LayoutText(
    std::span<const GlyphInstance> shapedGlyphs,
    const FontMetrics& metrics,
    const TextLayoutConfig& config)
{
    TextLayoutResult result;
    if (shapedGlyphs.empty() || !metrics.IsValid()) {
        return result;
    }

    const LayoutScale scale = ComputeScale(metrics, config);
    result.lineHeight = scale.lineHeight;

    std::vector<LineInfo> lines;
    LineInfo currentLine;
    currentLine.startQuad = 0;
    currentLine.y = 0.0f;
    currentLine.height = scale.lineHeight;

    float penX = 0.0f;
    float penY = 0.0f;
    float maxLineWidth = 0.0f;

    for (const auto& glyph : shapedGlyphs) {
        const float xAdv = FiniteOrZero(glyph.xAdvance) * scale.font;
        const float yAdv = FiniteOrZero(glyph.yAdvance) * scale.font;

        // Wrap check
        if (config.maxWidth > 0.0f && config.wrap != TextWrap::None) {
            if (penX + xAdv > config.maxWidth && penX > 0.0f) {
                // End current line
                currentLine.endQuad = result.quads.size();
                currentLine.width = penX;
                maxLineWidth = std::max(maxLineWidth, penX);
                lines.push_back(currentLine);

                // Start new line
                penX = 0.0f;
                penY += scale.lineHeight;
                currentLine.startQuad = result.quads.size();
                currentLine.y = penY;
                currentLine.height = scale.lineHeight;
            }
        }

        GlyphQuad quad = MakeQuad(glyph, penX, penY, scale);
        quad.penOrigin = {penX, penY + scale.ascent};
        quad.advance = xAdv;
        quad.lineIndex = static_cast<std::uint32_t>(lines.size());
        result.quads.push_back(quad);

        penX += xAdv;
        penY += yAdv;
    }

    currentLine.endQuad = result.quads.size();
    currentLine.width = penX;
    maxLineWidth = std::max(maxLineWidth, penX);
    lines.push_back(currentLine);

    // Without the source text only cluster START offsets are known: a line spans from its first
    // glyph's cluster to the first glyph of the next line (the last line ends at its last cluster).
    for (std::size_t li = 0; li < lines.size(); ++li) {
        LineInfo& line = lines[li];
        if (line.endQuad > line.startQuad) {
            line.byteBegin = result.quads[line.startQuad].clusterByteOffset;
            line.byteEnd = (li + 1 < lines.size() && lines[li + 1].endQuad > lines[li + 1].startQuad)
                ? result.quads[lines[li + 1].startQuad].clusterByteOffset
                : result.quads[line.endQuad - 1].clusterByteOffset;
        }
    }

    const float containerWidth = (config.maxWidth > 0.0f) ? config.maxWidth : maxLineWidth;
    FinalizeLines(result, lines, scale, config, containerWidth);

    result.size = {containerWidth, penY + scale.lineHeight};
    return result;
}

TextLayoutResult LayoutText(
    std::string_view utf8,
    std::span<const GlyphInstance> shapedGlyphs,
    const FontMetrics& metrics,
    const TextLayoutConfig& config)
{
    if (shapedGlyphs.empty() || !metrics.IsValid()) {
        return {};
    }
    if (utf8.empty()) {
        return LayoutText(shapedGlyphs, metrics, config);
    }

    const std::vector<TextCluster> clusters = AnalyzeClusters(utf8);
    const std::size_t clusterCount = clusters.size();
    if (clusterCount == 0) {
        return LayoutText(shapedGlyphs, metrics, config);
    }

    // Group the glyphs by cluster. The mapping must be monotonic (logical == visual order).
    std::vector<std::size_t> firstGlyph(clusterCount, 0);
    std::vector<std::size_t> glyphCount(clusterCount, 0);
    std::size_t previousCluster = 0;
    for (std::size_t g = 0; g < shapedGlyphs.size(); ++g) {
        const std::uint32_t offset = shapedGlyphs[g].clusterByteOffset;
        const auto it = std::upper_bound(
            clusters.begin(), clusters.end(), offset,
            [](std::uint32_t value, const TextCluster& c) { return value < c.byteBegin; });
        const std::size_t cluster = (it == clusters.begin()) ? 0 : static_cast<std::size_t>(it - clusters.begin()) - 1;
        if (cluster < previousCluster) {
            return LayoutText(shapedGlyphs, metrics, config); // Not LTR-monotonic: out of scope.
        }
        if (glyphCount[cluster] == 0) firstGlyph[cluster] = g;
        ++glyphCount[cluster];
        previousCluster = cluster;
    }

    const LayoutScale scale = ComputeScale(metrics, config);

    std::vector<float> clusterAdvance(clusterCount, 0.0f);
    for (std::size_t c = 0; c < clusterCount; ++c) {
        float sum = 0.0f;
        for (std::size_t k = 0; k < glyphCount[c]; ++k) {
            sum += FiniteOrZero(shapedGlyphs[firstGlyph[c] + k].xAdvance) * scale.font;
        }
        clusterAdvance[c] = sum;
    }

    const bool constrained = config.wrap != TextWrap::None && config.maxWidth > 0.0f && IsFinite(config.maxWidth);
    const float maxWidth = constrained ? config.maxWidth : 0.0f;
    constexpr float Epsilon = 1.0e-4f;
    constexpr std::size_t None = std::numeric_limits<std::size_t>::max();

    // A soft break is allowed before cluster i. Never before spaces or newlines (they hang).
    const auto opportunity = [&](std::size_t i) -> bool {
        if (i == 0 || i >= clusterCount) return false;
        if (clusters[i].kind != ClusterKind::Normal) return false;
        return config.wrap == TextWrap::Char ? true : clusters[i].breakBefore;
    };

    struct ClusterLine {
        std::size_t first = 0;
        std::size_t end = 0;
        float content = 0.0f;
        bool newline = false;
    };
    std::vector<ClusterLine> clusterLines;

    std::size_t i = 0;
    while (i < clusterCount) {
        const std::size_t start = i;
        float content = 0.0f;
        float pending = 0.0f;       // Width of spaces since the last non-space cluster (they hang).
        float breakContent = 0.0f;  // Content width if we break at lastBreak.
        std::size_t lastBreak = None;
        std::size_t lineEnd = clusterCount;
        bool hasContent = false;
        bool newline = false;

        std::size_t j = start;
        for (; j < clusterCount; ++j) {
            const TextCluster& cluster = clusters[j];
            if (cluster.kind == ClusterKind::Newline) {
                newline = true;
                lineEnd = j + 1; // The newline belongs to the line it terminates.
                break;
            }
            if (cluster.kind == ClusterKind::Space) {
                pending += clusterAdvance[j];
                if (opportunity(j + 1)) {
                    lastBreak = j + 1;
                    breakContent = content;
                }
                continue;
            }
            if (constrained && hasContent && content + pending + clusterAdvance[j] > maxWidth + Epsilon) {
                if (lastBreak != None && lastBreak > start) {
                    lineEnd = lastBreak;
                    content = breakContent;
                } else {
                    lineEnd = j; // Emergency break: a single word wider than the line.
                }
                break;
            }
            content += pending + clusterAdvance[j];
            pending = 0.0f;
            hasContent = true;
            if (opportunity(j + 1)) {
                lastBreak = j + 1;
                breakContent = content;
            }
        }
        if (j >= clusterCount && !newline) lineEnd = clusterCount;

        clusterLines.push_back({start, lineEnd, content, newline});
        i = lineEnd; // Always > start: the loop above consumes at least one cluster.
    }

    TextLayoutResult result;
    std::vector<LineInfo> lines;
    lines.reserve(clusterLines.size());
    float maxLineWidth = 0.0f;
    float lineTop = 0.0f;
    for (std::size_t li = 0; li < clusterLines.size(); ++li) {
        const ClusterLine& cl = clusterLines[li];
        LineInfo info;
        info.startQuad = result.quads.size();
        info.y = lineTop;
        info.height = scale.lineHeight;
        info.width = cl.content;
        info.byteBegin = clusters[cl.first].byteBegin;
        info.byteEnd = clusters[cl.end - 1].byteEnd;
        info.endsWithNewline = cl.newline;

        float penX = 0.0f;
        for (std::size_t c = cl.first; c < cl.end; ++c) {
            for (std::size_t k = 0; k < glyphCount[c]; ++k) {
                const GlyphInstance& glyph = shapedGlyphs[firstGlyph[c] + k];
                const float advance = FiniteOrZero(glyph.xAdvance) * scale.font;
                GlyphQuad quad = MakeQuad(glyph, penX, lineTop, scale);
                quad.penOrigin = {penX, lineTop + scale.ascent};
                quad.advance = advance;
                quad.lineIndex = static_cast<std::uint32_t>(li);
                result.quads.push_back(quad);
                penX += advance;
            }
        }
        info.endQuad = result.quads.size();
        maxLineWidth = std::max(maxLineWidth, info.width);
        lines.push_back(info);
        lineTop += scale.lineHeight;
    }

    const float containerWidth = (config.maxWidth > 0.0f && IsFinite(config.maxWidth)) ? config.maxWidth : maxLineWidth;
    FinalizeLines(result, lines, scale, config, containerWidth);
    result.size = {containerWidth, lineTop};
    return result;
}

Vec2 GetCaretPosition(
    const TextLayoutResult& layout,
    std::uint32_t clusterByteOffset,
    [[maybe_unused]] const FontMetrics& metrics) noexcept
{
    if (layout.quads.empty()) {
        return {0.0f, 0.0f};
    }

    for (std::size_t i = 0; i < layout.quads.size(); ++i) {
        if (layout.quads[i].clusterByteOffset >= clusterByteOffset) {
            return {layout.quads[i].bounds.position.x, layout.quads[i].bounds.position.y};
        }
    }

    // After the last glyph
    const auto& last = layout.quads.back();
    return {last.bounds.position.x + last.bounds.size.x, last.bounds.position.y};
}

namespace {

// Index of the line that owns byteOffset: the LAST line whose byteBegin <= offset.
[[nodiscard]] std::size_t FindLineForOffset(const TextLayoutResult& layout, std::uint32_t byteOffset) noexcept {
    std::size_t line = 0;
    for (std::size_t i = 0; i < layout.lines.size(); ++i) {
        if (layout.lines[i].byteBegin <= byteOffset && layout.lines[i].quadCount + (layout.lines[i].endsWithNewline ? 1u : 0u) > 0) {
            line = i;
        }
    }
    return line;
}

[[nodiscard]] float LineEndX(const TextLayoutResult& layout, const TextLine& line) noexcept {
    if (line.quadCount == 0) return line.x;
    const GlyphQuad& last = layout.quads[line.firstQuad + line.quadCount - 1];
    return last.penOrigin.x + last.advance;
}

[[nodiscard]] float VirtualLineX(const TextLayoutResult& layout) noexcept {
    switch (layout.alignment) {
    case TextAlignment::Center: return layout.containerWidth * 0.5f;
    case TextAlignment::Right:  return layout.containerWidth;
    case TextAlignment::Left:
    default:                    return 0.0f;
    }
}

} // namespace

Rect GetCaretRect(const TextLayoutResult& layout, std::uint32_t byteOffset) noexcept {
    if (layout.lines.empty()) {
        return Rect{{0.0f, 0.0f}, {0.0f, layout.lineHeight}};
    }

    const TextLine& last = layout.lines.back();
    // After a final newline the caret sits at the start of a virtual empty line below the text.
    if (last.endsWithNewline && byteOffset >= last.byteEnd) {
        return Rect{{VirtualLineX(layout), last.y + last.height}, {0.0f, layout.lineHeight}};
    }

    const std::size_t lineIndex = FindLineForOffset(layout, byteOffset);
    const TextLine& line = layout.lines[lineIndex];
    for (std::size_t q = 0; q < line.quadCount; ++q) {
        const GlyphQuad& quad = layout.quads[line.firstQuad + q];
        if (quad.clusterByteOffset >= byteOffset) {
            return Rect{{quad.penOrigin.x, line.y}, {0.0f, line.height}};
        }
    }
    return Rect{{LineEndX(layout, line), line.y}, {0.0f, line.height}};
}

std::uint32_t FindInsertionOffset(const TextLayoutResult& layout, Vec2 localPos) noexcept {
    if (layout.lines.empty()) return 0;

    const float height = (layout.lineHeight > 0.0f && IsFinite(layout.lineHeight)) ? layout.lineHeight : 1.0f;
    const float y = IsFinite(localPos.y) ? localPos.y : 0.0f;
    const float x = IsFinite(localPos.x) ? localPos.x : 0.0f;
    const float maxIndex = static_cast<float>(layout.lines.size() - 1);
    const std::size_t lineIndex = static_cast<std::size_t>(std::clamp(std::floor(y / height), 0.0f, maxIndex));
    const TextLine& line = layout.lines[lineIndex];

    std::uint32_t bestOffset = line.byteBegin;
    float bestDistance = std::numeric_limits<float>::infinity();
    bool anyStop = false;
    const auto consider = [&](float stopX, std::uint32_t offset) {
        const float d = std::abs(x - stopX);
        if (d < bestDistance) { // Strict '<': ties keep the earlier stop.
            bestDistance = d;
            bestOffset = offset;
            anyStop = true;
        }
    };

    bool hasPrevious = false;
    std::uint32_t previousOffset = 0;
    for (std::size_t q = 0; q < line.quadCount; ++q) {
        const GlyphQuad& quad = layout.quads[line.firstQuad + q];
        if (hasPrevious && quad.clusterByteOffset == previousOffset) continue; // Same cluster.
        consider(quad.penOrigin.x, quad.clusterByteOffset);
        previousOffset = quad.clusterByteOffset;
        hasPrevious = true;
    }
    if (!line.endsWithNewline) {
        consider(LineEndX(layout, line), line.byteEnd);
    }
    return anyStop ? bestOffset : line.byteBegin;
}

std::uint32_t FindClusterAtPosition(
    const TextLayoutResult& layout,
    Vec2 localPos) noexcept
{
    if (layout.quads.empty()) {
        return 0;
    }

    std::uint32_t closestCluster = layout.quads.front().clusterByteOffset;
    float closestDistSq = std::numeric_limits<float>::infinity();

    for (const auto& quad : layout.quads) {
        const Vec2 center = quad.bounds.position + quad.bounds.size * 0.5f;
        const float dx = center.x - localPos.x;
        const float dy = center.y - localPos.y;
        const float distSq = dx * dx + dy * dy;
        if (distSq < closestDistSq) {
            closestDistSq = distSq;
            closestCluster = quad.clusterByteOffset;
        }
    }

    return closestCluster;
}

std::vector<Rendering::UIRenderItem> GenerateTextRenderItems(
    const TextLayoutResult& layout,
    const UILayoutRect& rect,
    Vec4 colorPremultiplied,
    std::uint32_t atlasTextureSlice,
    std::uint32_t clipIndex,
    float zOrder,
    std::uint64_t basePaintOrder,
    UIHandle owner,
    std::uint32_t renderFlags)
{
    std::vector<Rendering::UIRenderItem> items;
    items.reserve(layout.quads.size());

    std::uint64_t currentPaint = basePaintOrder;
    for (const auto& quad : layout.quads) {
        if (quad.bounds.size.x <= 0.0f || quad.bounds.size.y <= 0.0f) {
            continue; // Skip non-printable or whitespace quads
        }

        Rendering::UIRenderItem item;
        item.handle = owner;
        item.layout.resolvedPosition = rect.resolvedPosition + quad.bounds.position;
        item.layout.resolvedSize = quad.bounds.size;
        item.layout.visible = rect.visible;

        item.transform.anchorMin = {0.0f, 0.0f};
        item.transform.anchorMax = {0.0f, 0.0f};
        item.transform.offsetMin = {0.0f, 0.0f};
        item.transform.offsetMax = quad.bounds.size;
        item.transform.pivot = {0.0f, 0.0f};
        item.transform.scale = {1.0f, 1.0f};

        item.uvRect[0] = quad.uvRect[0];
        item.uvRect[1] = quad.uvRect[1];
        item.uvRect[2] = quad.uvRect[2];
        item.uvRect[3] = quad.uvRect[3];

        item.tintPremultipliedLinear = colorPremultiplied;
        item.textureSlice = atlasTextureSlice;
        item.materialIndex = 0;
        item.clipIndex = clipIndex;
        item.flags = renderFlags;
        item.zOrder = zOrder;
        item.paintOrder = ++currentPaint;
        item.visible = rect.visible;

        items.push_back(item);
    }

    return items;
}

} // namespace ToolEngine::SpriteUi::Text
