#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace ToolEngine::SpriteUi::Text {

enum class ClusterKind : std::uint8_t {
    Normal,
    Space,   // Collapsible white space: hangs at the end of a line (never forces an overflow).
    Newline  // Mandatory line break (LF, CRLF, CR, NEL, LS, PS).
};

// A user-perceived character: a base code point plus the combining marks / variation selectors /
// ZWJ sequences / regional-indicator pair that must never be separated by a line break.
struct TextCluster {
    std::uint32_t byteBegin = 0;
    std::uint32_t byteEnd = 0; // Exclusive.
    char32_t base = 0;         // First code point of the cluster.
    ClusterKind kind = ClusterKind::Normal;
    bool breakBefore = false;  // A SOFT line break is allowed immediately before this cluster.
};

// Input larger than this is truncated (at a code point boundary) before analysis.
inline constexpr std::size_t MaxAnalyzedBytes = std::size_t{1} << 20;

// Splits UTF-8 into clusters and marks soft break opportunities.
//
// Pragmatic subset of Unicode UAX #14 (the standard only defines break OPPORTUNITIES; choosing one
// depends on widths, which is LayoutText's job):
//  - mandatory: LF, CRLF, CR, U+0085, U+2028, U+2029
//  - opportunity after spaces/tabs/U+3000 and after ZWSP (U+200B); never before a space
//  - opportunity after a hyphen (-, U+2010, U+2013, U+2014) when it follows a letter/digit and is not
//    followed by a digit (so "-5" and "a-5" stay together)
//  - no break around NBSP (U+00A0, U+2007, U+202F), WJ (U+2060) and U+FEFF
//  - CJK (kana, ideographs): opportunity between any two, except before closing punctuation and
//    after opening punctuation; Hangul is NOT broken inside a word
// Invalid UTF-8 never fails: each bad byte becomes one U+FFFD cluster. RTL/bidi is out of scope.
[[nodiscard]] std::vector<TextCluster> AnalyzeClusters(std::string_view utf8);

} // namespace ToolEngine::SpriteUi::Text
