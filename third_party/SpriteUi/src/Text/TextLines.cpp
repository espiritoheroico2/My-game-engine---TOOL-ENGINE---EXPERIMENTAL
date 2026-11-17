#include "SpriteUi/Text/TextLines.hpp"

#include <algorithm>

namespace ToolEngine::SpriteUi::Text {

std::pair<std::size_t, std::size_t> VisibleLineRange(const TextLayoutResult& layout, float top, float bottom) {
    // As linhas estao ordenadas por y, entao a busca e binaria.
    const auto first = std::partition_point(layout.lines.begin(), layout.lines.end(),
                                            [top](const TextLine& line) { return line.y + line.height <= top; });
    const auto last = std::partition_point(first, layout.lines.end(),
                                           [bottom](const TextLine& line) { return line.y < bottom; });
    return {static_cast<std::size_t>(first - layout.lines.begin()),
            static_cast<std::size_t>(last - layout.lines.begin())};
}

TextLayoutResult VisibleGlyphs(const TextLayoutResult& layout, std::size_t firstLine, std::size_t endLine) {
    TextLayoutResult part;
    part.size = layout.size;
    part.lineHeight = layout.lineHeight;
    for (std::size_t i = firstLine; i < endLine; ++i) {
        const TextLine& line = layout.lines[i];
        const std::size_t last = std::min(layout.quads.size(), line.firstQuad + line.quadCount);
        for (std::size_t q = line.firstQuad; q < last; ++q)
            part.quads.push_back(layout.quads[q]);
    }
    return part;
}

std::optional<Rect> LineSelectionRect(const TextLayoutResult& layout, const TextLine& line, std::size_t begin,
                                      std::size_t end) {
    const std::size_t from = std::max(begin, static_cast<std::size_t>(line.byteBegin));
    const std::size_t to = std::min(end, static_cast<std::size_t>(line.byteEnd));
    if (from >= to) return std::nullopt;

    const float x0 = GetCaretRect(layout, static_cast<std::uint32_t>(from)).position.x;
    // Ate o fim da linha usa a largura da linha: o caret desse offset cairia no inicio da linha seguinte.
    const float x1 = to == line.byteEnd ? line.x + line.width
                                        : GetCaretRect(layout, static_cast<std::uint32_t>(to)).position.x;
    return Rect{{x0, line.y}, {x1 - x0, line.height}};
}

std::uint32_t MoveCaretVertically(VerticalCaretMotion& motion, const TextLayoutResult& layout,
                                  std::uint32_t caret, int direction, std::uint32_t textBytes) {
    const bool continuing = motion.active && motion.lastOffset == caret;
    const Rect caretRect = GetCaretRect(layout, caret);
    if (!continuing) motion.goalX = caretRect.position.x;

    // Centro da linha vizinha: a partir do topo do caret, um passo para cima ou para baixo.
    const float lineHeight = layout.lineHeight;
    const float targetY = caretRect.position.y + static_cast<float>(direction) * lineHeight + 0.5f * lineHeight;
    std::uint32_t target = 0;
    if (targetY < 0.0f) {
        target = 0;
    } else if (targetY >= layout.size.y) {
        target = textBytes;
    } else {
        target = FindInsertionOffset(layout, {motion.goalX, targetY});
    }
    motion.active = true;
    motion.lastOffset = target;
    return target;
}

} // namespace ToolEngine::SpriteUi::Text
