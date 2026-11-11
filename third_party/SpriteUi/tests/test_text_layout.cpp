#include "SpriteUi/SpriteUi.hpp"
#include "SpriteUi/Text/DebugShaper.hpp"
#include "SpriteUi/Text/LineBreak.hpp"

#include "TestHarness.hpp"
#include "TestSuites.hpp"

#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace SpriteUiTests {

namespace {

using namespace ToolEngine::SpriteUi;
using namespace ToolEngine::SpriteUi::Text;

// fontSize 10, unitsPerEm 1000, advanceEm 0.5 -> narrow = 5px, wide (CJK/emoji) = 10px, line = 12px.
FontMetrics Metrics() {
    FontMetrics m;
    m.fontSize = 10.0f;
    m.unitsPerEm = 1000.0f;
    m.ascent = 800.0f;
    m.descent = -200.0f;
    m.lineGap = 200.0f;
    return m;
}

DebugShaperConfig Shaper() {
    DebugShaperConfig c;
    c.advanceEm = 0.5f;
    c.wideAdvanceEm = 1.0f;
    return c;
}

TextLayoutResult Layout(std::string_view text, float maxWidth, TextWrap wrap = TextWrap::Word,
                        TextAlignment align = TextAlignment::Left) {
    const FontMetrics m = Metrics();
    const auto glyphs = ShapeDebug(text, m, Shaper());
    TextLayoutConfig config;
    config.fontSize = 10.0f;
    config.maxWidth = maxWidth;
    config.wrap = wrap;
    config.alignment = align;
    return LayoutText(text, glyphs, m, config);
}

// Line contents with trailing spaces and the newline removed, for readable expectations.
std::vector<std::string> Lines(std::string_view text, const TextLayoutResult& layout) {
    std::vector<std::string> out;
    for (const TextLine& line : layout.lines) {
        std::string s(text.substr(line.byteBegin, line.byteEnd - line.byteBegin));
        while (!s.empty() && (s.back() == ' ' || s.back() == '\n' || s.back() == '\r')) s.pop_back();
        out.push_back(s);
    }
    return out;
}

using V = std::vector<std::string>;

bool LinesTileText(std::string_view text, const TextLayoutResult& layout) {
    std::uint32_t expect = 0;
    for (const TextLine& line : layout.lines) {
        if (line.byteBegin != expect || line.byteEnd <= line.byteBegin) return false;
        expect = line.byteEnd;
    }
    return expect == text.size();
}

void TestWordWrapBreaksAtSpacesNotInsideWords() {
    const std::string text = "hello world foo bar";
    const auto layout = Layout(text, 55.0f);
    CHECK((Lines(text, layout) == V{"hello world", "foo bar"}));
    CHECK(layout.lineCount == 2);
    CHECK(LinesTileText(text, layout));
    CHECK(Near(layout.lines[0].width, 55.0f));   // "hello world" = 11 chars * 5
    CHECK(Near(layout.lines[1].y, 12.0f));
    CHECK(Near(layout.size.y, 24.0f));
    CHECK(Near(layout.lineHeight, 12.0f));
}

void TestWordVersusCharWrapDiffer() {
    const std::string text = "aa bbbbb";
    CHECK((Lines(text, Layout(text, 30.0f, TextWrap::Word)) == V{"aa", "bbbbb"}));
    CHECK((Lines(text, Layout(text, 30.0f, TextWrap::Char)) == V{"aa bbb", "bb"}));
    CHECK(Lines(text, Layout(text, 30.0f, TextWrap::None)).size() == 1);
}

void TestTrailingSpacesHangAndNeverOverflow() {
    const std::string text = "ab   cd";
    const auto layout = Layout(text, 10.0f);
    CHECK((Lines(text, layout) == V{"ab", "cd"}));
    CHECK(Near(layout.lines[0].width, 10.0f));      // spaces are not part of the content width
    CHECK(layout.lines[0].byteEnd == 5);            // ...but they still belong to the line (caret mapping)
    CHECK(LinesTileText(text, layout));
}

void TestEmergencyBreakForLongWords() {
    const std::string text = "abcdefghij";
    CHECK((Lines(text, Layout(text, 20.0f)) == V{"abcd", "efgh", "ij"}));
    CHECK((Lines("abc", Layout("abc", 2.0f)) == V{"a", "b", "c"})); // narrower than one glyph: still progresses
}

void TestMandatoryNewlines() {
    const std::string text = "a\nb\n\nc";
    const auto layout = Layout(text, 100.0f);
    CHECK((Lines(text, layout) == V{"a", "b", "", "c"}));
    CHECK(LinesTileText(text, layout));
    CHECK(layout.lines[0].endsWithNewline);
    CHECK(!layout.lines[3].endsWithNewline);
    CHECK(Near(layout.lines[2].y, 24.0f));          // the blank line still occupies a line box
    CHECK(Lines("a\r\nb", Layout("a\r\nb", 100.0f)).size() == 2);
    CHECK(Lines("a\n", Layout("a\n", 100.0f)).size() == 1); // a trailing newline adds no layout line
    CHECK(Lines("\n\n", Layout("\n\n", 100.0f)).size() == 2);
}

void TestWrapNoneStillHonoursNewlines() {
    const std::string text = "aaaa bbbb\ncccc";
    const auto layout = Layout(text, 5.0f, TextWrap::None);
    CHECK((Lines(text, layout) == V{"aaaa bbbb", "cccc"}));
}

void TestCjkAndPunctuationRules() {
    const std::string cjk = "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E\xE3\x81\xAE\xE3\x83\x86\xE3\x82\xAD\xE3\x82\xB9\xE3\x83\x88"; // 日本語のテキスト
    const auto layout = Layout(cjk, 30.0f);
    CHECK(layout.lineCount == 3);                   // 3 wide glyphs (10px each) per line
    CHECK(Near(layout.lines[0].width, 30.0f));
    CHECK(Near(layout.lines[2].width, 20.0f));
    CHECK(LinesTileText(cjk, layout));

    const std::string punct = "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E\xE3\x80\x82\xE6\x97\xA5"; // 日本語。日
    const auto p = Layout(punct, 30.0f);
    CHECK(Lines(punct, p)[1].rfind("\xE3\x80\x82", 0) != 0); // 。never starts a line
}

void TestAlignmentUsesContentWidthNotHangingSpaces() {
    const std::string text = "ab  ";
    const auto center = Layout(text, 100.0f, TextWrap::Word, TextAlignment::Center);
    CHECK(Near(center.lines[0].x, 45.0f));          // (100 - 10) / 2: the two trailing spaces are ignored
    CHECK(Near(center.quads[0].penOrigin.x, 45.0f));
    const auto right = Layout(text, 100.0f, TextWrap::Word, TextAlignment::Right);
    CHECK(Near(right.lines[0].x, 90.0f));
    const auto left = Layout(text, 100.0f, TextWrap::Word, TextAlignment::Left);
    CHECK(Near(left.lines[0].x, 0.0f));
}

void TestClustersWithSeveralGlyphsStayTogether() {
    const std::string text = "e\xCC\x81" "e\xCC\x81"; // two clusters, each base + combining mark = 2 glyphs
    const FontMetrics m = Metrics();
    const auto glyphs = ShapeDebug(text, m, Shaper());
    CHECK(glyphs.size() == 4);
    const auto layout = Layout(text, 5.0f);         // fits exactly one cluster per line
    CHECK(layout.lineCount == 2);
    CHECK(layout.quads.size() == 4);
    CHECK(layout.lines[0].quadCount == 2 && layout.lines[1].quadCount == 2);
    CHECK(layout.quads[0].lineIndex == 0 && layout.quads[1].lineIndex == 0);
    CHECK(layout.quads[2].lineIndex == 1);
    CHECK(layout.lines[0].byteEnd == 3 && layout.lines[1].byteBegin == 3);
}

void TestFallbackToLegacyPath() {
    const FontMetrics m = Metrics();
    const auto glyphs = ShapeDebug("abc", m, Shaper());
    TextLayoutConfig config;
    config.fontSize = 10.0f;
    // Empty text -> legacy per-glyph overload.
    const auto viaEmpty = LayoutText(std::string_view{}, glyphs, m, config);
    CHECK(viaEmpty.quads.size() == 3 && viaEmpty.lineCount == 1);
    // Non-monotonic cluster order (e.g. an RTL run) -> legacy path, never a crash.
    std::vector<GlyphInstance> reversed(glyphs.rbegin(), glyphs.rend());
    const auto rtl = LayoutText("abc", reversed, m, config);
    CHECK(rtl.quads.size() == 3);
    // Glyph offsets beyond the text are clamped to the last cluster.
    std::vector<GlyphInstance> far = glyphs;
    far[2].clusterByteOffset = 999;
    CHECK(LayoutText("abc", far, m, config).quads.size() == 3);
    // Invalid metrics / no glyphs -> empty result.
    FontMetrics bad = m;
    bad.fontSize = -1.0f;
    CHECK(LayoutText("abc", glyphs, bad, config).quads.empty());
    CHECK(LayoutText("abc", std::span<const GlyphInstance>{}, m, config).quads.empty());
}

void TestYOffsetRaisesTheGlyphBox() {
    // Regression: GlyphInstance::yOffset (HarfBuzz raises combining marks with it) was ignored.
    const FontMetrics m = Metrics();
    auto base = ShapeDebug("a", m, Shaper());
    auto raised = base;
    raised[0].yOffset = 100.0f; // 1px at fontSize 10
    TextLayoutConfig config;
    config.fontSize = 10.0f;
    const auto a = LayoutText("a", base, m, config);
    const auto b = LayoutText("a", raised, m, config);
    CHECK(Near(a.quads[0].bounds.position.y - b.quads[0].bounds.position.y, 1.0f));
    // ...in the glyph-only overload too.
    const auto c = LayoutText(base, m, config);
    const auto d = LayoutText(raised, m, config);
    CHECK(Near(c.quads[0].bounds.position.y - d.quads[0].bounds.position.y, 1.0f));
}

void TestNonFiniteMetricsStayFinite() {
    const FontMetrics m = Metrics();
    auto glyphs = ShapeDebug("abc", m, Shaper());
    glyphs[1].xAdvance = std::nanf("");
    glyphs[2].xOffset = INFINITY;
    glyphs[2].yOffset = std::nanf("");
    TextLayoutConfig config;
    config.fontSize = 10.0f;
    config.maxWidth = std::nanf("");
    const auto layout = LayoutText("abc", glyphs, m, config);
    bool finite = std::isfinite(layout.size.x) && std::isfinite(layout.size.y);
    for (const GlyphQuad& q : layout.quads) {
        finite = finite && std::isfinite(q.bounds.position.x) && std::isfinite(q.bounds.position.y) &&
                 std::isfinite(q.penOrigin.x) && std::isfinite(q.advance);
    }
    CHECK(finite);
}

// Differential test: for text without spaces/newlines, the source-text overload in Char mode must
// produce exactly the geometry of the glyph-only overload.
void TestSourceOverloadMatchesLegacyGeometry() {
    std::srand(4242);
    const char* alphabet[] = {"a", "b", "c", "d", "e", "\xE6\x97\xA5", "\xE6\x9C\xAC", "\xF0\x9F\x98\x80"};
    const FontMetrics m = Metrics();
    bool identical = true;
    for (int it = 0; it < 600; ++it) {
        std::string text;
        const int len = 1 + std::rand() % 30;
        for (int k = 0; k < len; ++k) text += alphabet[std::rand() % 8];
        const auto glyphs = ShapeDebug(text, m, Shaper());
        TextLayoutConfig config;
        config.fontSize = 10.0f;
        config.maxWidth = static_cast<float>(5 + std::rand() % 90);
        config.wrap = TextWrap::Char;
        config.alignment = static_cast<TextAlignment>(std::rand() % 3);
        const auto legacy = LayoutText(glyphs, m, config);
        const auto modern = LayoutText(text, glyphs, m, config);
        identical = identical && legacy.quads.size() == modern.quads.size() && legacy.lineCount == modern.lineCount &&
                    Near(legacy.size.x, modern.size.x) && Near(legacy.size.y, modern.size.y);
        for (std::size_t i = 0; identical && i < legacy.quads.size(); ++i) {
            identical = Near(legacy.quads[i].bounds.position.x, modern.quads[i].bounds.position.x) &&
                        Near(legacy.quads[i].bounds.position.y, modern.quads[i].bounds.position.y) &&
                        Near(legacy.quads[i].penOrigin.x, modern.quads[i].penOrigin.x) &&
                        legacy.quads[i].lineIndex == modern.quads[i].lineIndex;
        }
    }
    CHECK(identical);
}

// Property: for any text/width/mode, lines tile the text, quads are never lost and finite.
void TestLayoutInvariantsOnRandomText() {
    std::srand(909);
    const char* alphabet[] = {"a", "b", "c", " ", " ", "\n", "-", "\xE6\x97\xA5", "\xE3\x80\x82", "e\xCC\x81",
                              "\xF0\x9F\x98\x80", "\xC2\xA0", "\xE2\x80\x8B", "\t"};
    const FontMetrics m = Metrics();
    bool tiles = true, quadsComplete = true, finite = true, fits = true, progresses = true;
    for (int it = 0; it < 2500; ++it) {
        std::string text;
        const int len = 1 + std::rand() % 40;
        for (int k = 0; k < len; ++k) text += alphabet[std::rand() % 14];
        const auto glyphs = ShapeDebug(text, m, Shaper());
        TextLayoutConfig config;
        config.fontSize = 10.0f;
        config.maxWidth = static_cast<float>(std::rand() % 90);
        config.wrap = static_cast<TextWrap>(std::rand() % 3);
        config.alignment = static_cast<TextAlignment>(std::rand() % 3);
        const auto layout = LayoutText(text, glyphs, m, config);
        tiles = tiles && LinesTileText(text, layout);
        quadsComplete = quadsComplete && layout.quads.size() == glyphs.size();
        progresses = progresses && layout.lineCount >= 1 && layout.lineCount <= text.size();
        finite = finite && std::isfinite(layout.size.x) && std::isfinite(layout.size.y);
        const bool constrained = config.wrap != TextWrap::None && config.maxWidth > 0.0f;
        for (const TextLine& line : layout.lines) {
            finite = finite && std::isfinite(line.x) && std::isfinite(line.width);
            // A line wider than the container must be a single unbreakable unit (one cluster).
            if (constrained && line.width > config.maxWidth + 1.0e-3f) {
                std::uint32_t distinct = 0, last = 0xFFFFFFFFu;
                for (std::size_t q = 0; q < line.quadCount; ++q) {
                    const std::uint32_t off = layout.quads[line.firstQuad + q].clusterByteOffset;
                    if (off != last && layout.quads[line.firstQuad + q].bounds.size.x > 0.0f) ++distinct; // visible glyphs only: hanging spaces do not count
                    last = off;
                }
                fits = fits && distinct <= 1;
            }
        }
    }
    CHECK(tiles);
    CHECK(quadsComplete);
    CHECK(progresses);
    CHECK(finite);
    CHECK(fits);
}

void TestCaretRectUsesPenOrigin() {
    const std::string text = "abc";
    const FontMetrics m = Metrics();
    const auto glyphs = ShapeDebug(text, m, Shaper());
    TextLayoutConfig config;
    config.fontSize = 10.0f;
    const auto layout = LayoutText(text, glyphs, m, config);

    for (std::uint32_t i = 0; i <= 3; ++i) {
        const Rect caret = GetCaretRect(layout, i);
        CHECK(Near(caret.position.x, 5.0f * static_cast<float>(i)));
        CHECK(Near(caret.position.y, 0.0f));
        CHECK(Near(caret.size.x, 0.0f));
        CHECK(Near(caret.size.y, 12.0f));
    }
    // The legacy caret sits on the glyph BOX (pen + bearing); the new one on the pen.
    const Vec2 legacy = GetCaretPosition(layout, 0, m);
    CHECK(legacy.x > 0.0f);
    CHECK(Near(GetCaretRect(layout, 0).position.x, 0.0f));
    CHECK(GetCaretRect(layout, 999).position.x == GetCaretRect(layout, 3).position.x); // clamps to the end
}

void TestCaretAtWrapBoundariesAndNewlines() {
    const std::string text = "hello world";
    const auto wrapped = Layout(text, 30.0f);       // "hello " / "world"
    CHECK(wrapped.lineCount == 2);
    const Rect atSpace = GetCaretRect(wrapped, 5);  // right after "hello"
    CHECK(Near(atSpace.position.x, 25.0f) && Near(atSpace.position.y, 0.0f));
    const Rect atWrap = GetCaretRect(wrapped, 6);   // soft-wrap boundary -> start of the next line
    CHECK(Near(atWrap.position.x, 0.0f) && Near(atWrap.position.y, 12.0f));
    const Rect atEnd = GetCaretRect(wrapped, 11);
    CHECK(Near(atEnd.position.x, 25.0f) && Near(atEnd.position.y, 12.0f));

    const std::string nl = "ab\n";
    const auto afterNewline = Layout(nl, 100.0f);
    const Rect virtualLine = GetCaretRect(afterNewline, 3);
    CHECK(Near(virtualLine.position.x, 0.0f) && Near(virtualLine.position.y, 12.0f)); // a new empty line below
    const auto centered = Layout(nl, 100.0f, TextWrap::Word, TextAlignment::Center);
    CHECK(Near(GetCaretRect(centered, 3).position.x, 50.0f));

    const std::string blank = "a\n\nb";
    const auto b = Layout(blank, 100.0f);
    const Rect onBlank = GetCaretRect(b, 2);        // the empty middle line
    CHECK(Near(onBlank.position.x, 0.0f) && Near(onBlank.position.y, 12.0f));

    const std::string accent = "e\xCC\x81x";
    const auto a = Layout(accent, 100.0f);
    CHECK(Near(GetCaretRect(a, 1).position.x, 5.0f)); // mid-cluster offset snaps forward to the next cluster

    TextLayoutResult empty;
    CHECK(Near(GetCaretRect(empty, 0).size.y, 0.0f)); // no crash on an empty layout
}

void TestFindInsertionOffsetPicksNearestCaretStop() {
    const std::string text = "abc";
    const auto layout = Layout(text, 100.0f);
    CHECK(FindInsertionOffset(layout, {0.0f, 5.0f}) == 0);
    CHECK(FindInsertionOffset(layout, {2.0f, 5.0f}) == 0);   // nearer to stop 0 (0) than stop 1 (5)
    CHECK(FindInsertionOffset(layout, {3.0f, 5.0f}) == 1);   // right half of 'a' -> caret AFTER it
    CHECK(FindInsertionOffset(layout, {100.0f, 5.0f}) == 3); // beyond the end -> end of text
    CHECK(FindInsertionOffset(layout, {-50.0f, 5.0f}) == 0);
    CHECK(FindInsertionOffset(layout, {7.4f, -20.0f}) == 1); // y above the text clamps to the first line

    const std::string two = "ab\ncd";
    const auto l2 = Layout(two, 100.0f);
    CHECK(FindInsertionOffset(l2, {100.0f, 5.0f}) == 2);     // end of line 0 is BEFORE the newline
    CHECK(FindInsertionOffset(l2, {100.0f, 15.0f}) == 5);    // last line has no newline: true end
    CHECK(FindInsertionOffset(l2, {100.0f, 999.0f}) == 5);   // y below clamps to the last line
    CHECK(FindInsertionOffset(l2, {std::nanf(""), std::nanf("")}) <= two.size());
    CHECK(FindInsertionOffset(TextLayoutResult{}, {1.0f, 1.0f}) == 0);
}

// Round trip: clicking exactly on a caret stop returns that offset.
void TestInsertionOffsetRoundTrip() {
    const std::string text = "hello world, wrapped text here";
    const auto layout = Layout(text, 60.0f);
    bool roundTrips = true;
    for (const TextLine& line : layout.lines) {
        for (std::size_t q = 0; q < line.quadCount; ++q) {
            const GlyphQuad& quad = layout.quads[line.firstQuad + q];
            const Vec2 click{quad.penOrigin.x, line.y + 1.0f};
            roundTrips = roundTrips && FindInsertionOffset(layout, click) == quad.clusterByteOffset;
        }
    }
    CHECK(roundTrips);
}

} // namespace

void RunTextWrapLayoutTests() {
    TestWordWrapBreaksAtSpacesNotInsideWords();
    TestWordVersusCharWrapDiffer();
    TestTrailingSpacesHangAndNeverOverflow();
    TestEmergencyBreakForLongWords();
    TestMandatoryNewlines();
    TestWrapNoneStillHonoursNewlines();
    TestCjkAndPunctuationRules();
    TestAlignmentUsesContentWidthNotHangingSpaces();
    TestClustersWithSeveralGlyphsStayTogether();
    TestFallbackToLegacyPath();
    TestYOffsetRaisesTheGlyphBox();
    TestNonFiniteMetricsStayFinite();
    TestSourceOverloadMatchesLegacyGeometry();
    TestLayoutInvariantsOnRandomText();
}

void RunCaretAndHitTests() {
    TestCaretRectUsesPenOrigin();
    TestCaretAtWrapBoundariesAndNewlines();
    TestFindInsertionOffsetPicksNearestCaretStop();
    TestInsertionOffsetRoundTrip();
}

} // namespace SpriteUiTests
