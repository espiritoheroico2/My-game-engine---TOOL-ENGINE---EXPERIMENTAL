#pragma once

#include "SpriteUi/Core/Handle.hpp"
#include "SpriteUi/Core/Math.hpp"
#include "SpriteUi/Core/Transform2D.hpp"
#include "SpriteUi/Rendering/DrawList.hpp"
#include "SpriteUi/Rendering/RenderFlags.hpp"
#include "SpriteUi/Text/FontMetrics.hpp"
#include "SpriteUi/Text/GlyphInstance.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace ToolEngine::SpriteUi::Text {

enum class TextAlignment : std::uint8_t {
    Left,
    Center,
    Right
};

enum class TextWrap : std::uint8_t {
    None,
    Word,
    Char
};

struct GlyphQuad {
    std::uint32_t glyphId = 0;
    std::uint32_t clusterByteOffset = 0;
    Rect bounds{};
    float uvRect[4]{0.0f, 0.0f, 0.0f, 0.0f};
    std::uint16_t atlasPage = 0;

    // Pen position where this glyph starts (x) and the baseline it sits on (y), with the line's
    // alignment shift already applied. Unlike `bounds`, it does not depend on the glyph's bearing.
    Vec2 penOrigin{0.0f, 0.0f};
    float advance = 0.0f;           // Horizontal advance in layout units.
    std::uint32_t lineIndex = 0;
};

// One laid-out line. With the source-text overload, lines partition the text: line k covers
// [byteBegin, byteEnd) and byteBegin of line k+1 == byteEnd of line k.
struct TextLine {
    std::size_t firstQuad = 0;
    std::size_t quadCount = 0;
    float x = 0.0f;         // Alignment shift applied to the line.
    float y = 0.0f;         // Top of the line box.
    float width = 0.0f;     // Content width (trailing spaces excluded in the source-text overload).
    float height = 0.0f;
    float baseline = 0.0f;  // y of the baseline.
    std::uint32_t byteBegin = 0;
    std::uint32_t byteEnd = 0;
    bool endsWithNewline = false;
};

struct TextLayoutConfig {
    float fontSize = 16.0f;
    float maxWidth = 0.0f; // <= 0.0f means unconstrained width
    float lineHeightMultiplier = 1.0f;
    TextAlignment alignment = TextAlignment::Left;
    TextWrap wrap = TextWrap::None;
};

struct TextLayoutResult {
    std::vector<GlyphQuad> quads;
    Vec2 size{0.0f, 0.0f};
    std::size_t lineCount = 0;

    std::vector<TextLine> lines;
    float lineHeight = 0.0f;     // Height of one line box (also valid for empty layouts).
    float containerWidth = 0.0f; // Width used for alignment.
    TextAlignment alignment = TextAlignment::Left;
};

// Glyph-only layout (legacy overload). Without the source text there is no way to know which
// glyphs are spaces or break opportunities, so TextWrap::Word behaves exactly like TextWrap::Char
// and newlines are not honoured. Prefer the overload that also receives the UTF-8 text.
[[nodiscard]] TextLayoutResult LayoutText(
    std::span<const GlyphInstance> shapedGlyphs,
    const FontMetrics& metrics,
    const TextLayoutConfig& config = {});

// Layout with the shaped glyphs AND the text they were shaped from (GlyphInstance::clusterByteOffset
// indexes into `utf8`). Enables real word wrapping (see AnalyzeClusters for the rules), mandatory
// line breaks, hanging trailing spaces and an emergency break for words wider than the line.
// Falls back to the glyph-only overload when `utf8` is empty or the glyph order is not monotonic in
// the text (e.g. right-to-left runs, which are out of scope).
[[nodiscard]] TextLayoutResult LayoutText(
    std::string_view utf8,
    std::span<const GlyphInstance> shapedGlyphs,
    const FontMetrics& metrics,
    const TextLayoutConfig& config = {});

// LEGACY caret position: top-left of the glyph BOX, which includes the glyph's bearing, so the caret
// does not sit on the pen position. Kept for compatibility; use GetCaretRect for text input.
[[nodiscard]] Vec2 GetCaretPosition(
    const TextLayoutResult& layout,
    std::uint32_t clusterByteOffset,
    const FontMetrics& metrics) noexcept;

// Caret rectangle (zero width, one line tall) in layout space for a byte offset: x is the PEN
// origin of the first cluster at/after the offset. An offset at a soft-wrap boundary resolves to the
// start of the next line; an offset after a final newline resolves to a virtual empty line below.
[[nodiscard]] Rect GetCaretRect(
    const TextLayoutResult& layout,
    std::uint32_t byteOffset) noexcept;

// Maps a local click position to the nearest caret stop (a byte offset usable as insertion point).
// Picks the line by y, then the nearest stop by x (cluster starts and, when the line does not end
// with a newline, the end of the line).
[[nodiscard]] std::uint32_t FindInsertionOffset(
    const TextLayoutResult& layout,
    Vec2 localPos) noexcept;

// Finds the nearest cluster byte offset for a given local click position (legacy: nearest glyph
// centre in 2D, so a click on the right half of a glyph returns that glyph, not the next stop).
[[nodiscard]] std::uint32_t FindClusterAtPosition(
    const TextLayoutResult& layout,
    Vec2 localPos) noexcept;

// Converts layout quads into standard UIRenderItem entries for GPU extraction.
[[nodiscard]] std::vector<Rendering::UIRenderItem> GenerateTextRenderItems(
    const TextLayoutResult& layout,
    const UILayoutRect& rect,
    Vec4 colorPremultiplied,
    std::uint32_t atlasTextureSlice,
    std::uint32_t clipIndex = Rendering::NoClipIndex,
    float zOrder = 0.0f,
    std::uint64_t basePaintOrder = 0,
    UIHandle owner = {}, // Handle valido e obrigatorio para o RenderExtractor aceitar os itens.
    std::uint32_t renderFlags = Rendering::FlagMsdfText);

} // namespace ToolEngine::SpriteUi::Text
