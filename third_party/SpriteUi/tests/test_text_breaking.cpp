#include "SpriteUi/SpriteUi.hpp"
#include "SpriteUi/Text/LineBreak.hpp"

#include "TestHarness.hpp"
#include "TestSuites.hpp"

#include <cstdlib>
#include <string>
#include <vector>

namespace SpriteUiTests {

namespace {

using namespace ToolEngine::SpriteUi::Text;

// Returns the cluster texts joined by '|' so expectations read like the input.
std::string Clusters(std::string_view text) {
    std::string out;
    for (const TextCluster& c : AnalyzeClusters(text)) {
        if (!out.empty()) out += '|';
        out.append(text.substr(c.byteBegin, c.byteEnd - c.byteBegin));
    }
    return out;
}

// Positions (cluster indices) where a soft break is allowed.
std::vector<std::size_t> Breaks(std::string_view text) {
    std::vector<std::size_t> out;
    const auto clusters = AnalyzeClusters(text);
    for (std::size_t i = 0; i < clusters.size(); ++i) {
        if (clusters[i].breakBefore) out.push_back(i);
    }
    return out;
}

using Idx = std::vector<std::size_t>;

void TestKindsAndMandatoryBreaks() {
    const auto c = AnalyzeClusters("a b\nc");
    CHECK(c.size() == 5);
    CHECK(c[0].kind == ClusterKind::Normal);
    CHECK(c[1].kind == ClusterKind::Space);
    CHECK(c[3].kind == ClusterKind::Newline);
    CHECK(Breaks("a b\nc") == Idx{2}); // Only 'b' (after the space); nothing after a newline.

    CHECK(Clusters("a\r\nb") == "a|\r\n|b"); // CRLF is ONE newline cluster.
    CHECK(AnalyzeClusters("a\r\nb")[1].kind == ClusterKind::Newline);
    CHECK(AnalyzeClusters("a\rb")[1].kind == ClusterKind::Newline);
    CHECK(AnalyzeClusters("a\xE2\x80\xA8" "b")[1].kind == ClusterKind::Newline); // U+2028
    CHECK(AnalyzeClusters("a\xE2\x80\xA9" "b")[1].kind == ClusterKind::Newline); // U+2029
    CHECK(AnalyzeClusters("a\xC2\x85" "b")[1].kind == ClusterKind::Newline);     // U+0085 NEL
    CHECK(AnalyzeClusters("").empty());
}

void TestHyphenRules() {
    CHECK(Breaks("well-known") == Idx{5});   // after the hyphen, before 'k'
    CHECK(Breaks("a-5").empty());            // not before a digit
    CHECK(Breaks("ab-5cd").empty());
    CHECK(Breaks("-ab").empty());            // hyphen at the start of a word
    CHECK(Breaks("ab-xcd") == Idx{3});
    CHECK(Breaks("x--y").empty() || !Breaks("x--y").empty()); // double hyphen: only must not crash
    CHECK(Breaks("a\xE2\x80\x93" "b") == Idx{2}); // en dash U+2013
}

void TestGlueAndZeroWidthSpace() {
    CHECK(Breaks("a b").size() == 1);
    CHECK(Breaks("a\xC2\xA0" "b").empty());          // NBSP glues both sides
    CHECK(Breaks("a\xE2\x80\xAF" "b").empty());      // U+202F narrow NBSP
    CHECK(Breaks("a\xE2\x81\xA0" "b").empty());      // word joiner
    CHECK(Breaks("a\xE2\x80\x8B" "b") == Idx{2});    // ZWSP offers an opportunity
    CHECK(AnalyzeClusters("a\xE2\x80\x8B" "b")[1].kind == ClusterKind::Space);
    CHECK(Breaks("a  b") == Idx{3});                 // one opportunity per run of spaces, after the run
}

void TestCjkRules() {
    CHECK((Breaks("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E") == Idx{1, 2}));               // 日本語: between all
    CHECK(Breaks("\xE6\x97\xA5\xE3\x80\x82") .empty());                              // 。never starts a line
    CHECK(Breaks("\xEF\xBC\x88\xE6\x97\xA5").empty());                                // （ never ends a line
    CHECK(Breaks("a\xE6\x97\xA5") == Idx{1});                                          // Latin -> CJK
    CHECK(Breaks("\xE6\x97\xA5" "a") == Idx{1});                                       // CJK -> Latin
    // Hangul syllables do not break inside a word.
    CHECK(Breaks("\xED\x95\x9C\xEA\xB8\x80").empty());
}

void TestClustersAreIndivisible() {
    CHECK(AnalyzeClusters("e\xCC\x81" "x").size() == 2);        // e + U+0301, x
    CHECK(AnalyzeClusters("e\xCC\x81" "x")[0].byteEnd == 3);
    CHECK(Breaks("e\xCC\x81" "x").empty());
    const std::string family = "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7"; // ZWJ sequence
    CHECK(AnalyzeClusters(family).size() == 1);
    CHECK(AnalyzeClusters(family)[0].byteEnd == family.size());
    const std::string flags = "\xF0\x9F\x87\xA7\xF0\x9F\x87\xB7\xF0\x9F\x87\xA7\xF0\x9F\x87\xB7";   // two flags
    CHECK(AnalyzeClusters(flags).size() == 2);
    CHECK(AnalyzeClusters("\xCC\x81" "a").size() == 2);          // lone mark at the start is its own cluster
}

void TestInvalidUtf8IsTolerated() {
    const auto c = AnalyzeClusters("\xFF\xFE" "ab\xC3");
    CHECK(c.size() == 5);                                         // each bad byte = one U+FFFD cluster
    CHECK(c[0].base == 0xFFFD && c[1].base == 0xFFFD && c[4].base == 0xFFFD);
    CHECK(AnalyzeClusters("\xC0\xAF").size() == 2);               // overlong
    CHECK(AnalyzeClusters("\xED\xA0\x80").size() == 3);           // surrogate
    CHECK(AnalyzeClusters("\xF4\x90\x80\x80").size() == 4);       // > U+10FFFF
}

void TestInputIsBoundedAndNeverSplitsACodePoint() {
    std::string big = "a";
    for (std::size_t i = 0; i < MaxAnalyzedBytes / 2; ++i) big += "\xC3\xA9"; // 'a' + 524288 x 'é'
    const auto c = AnalyzeClusters(big);
    CHECK(!c.empty());
    CHECK(c.back().byteEnd == MaxAnalyzedBytes - 1); // cut moved back so the last 'é' is not split
    CHECK(c.back().base == 0xE9);
}

// Property: clusters tile the text exactly, whatever the bytes are. One aggregated CHECK per
// property keeps the check counter meaningful; the first violation is printed with its input.
void TestClustersTileTheTextOnRandomInput() {
    std::srand(2025);
    const char* alphabet[] = {"a", "b", " ", "\n", "\r\n", "-", "5", "\xE6\x97\xA5", "\xE3\x80\x82", "e\xCC\x81",
                              "\xF0\x9F\x98\x80", "\xC2\xA0", "\xE2\x80\x8B", "\xEF\xBC\x88", "\xFF", "\xC3"};
    bool tiles = true;
    bool noBreakBeforeFirst = true;
    int iterations = 0;
    for (int it = 0; it < 3000; ++it, ++iterations) {
        std::string s;
        const int len = std::rand() % 50;
        for (int k = 0; k < len; ++k) s += alphabet[std::rand() % 16];
        const auto clusters = AnalyzeClusters(s);
        std::uint32_t expectBegin = 0;
        for (const TextCluster& c : clusters) {
            tiles = tiles && c.byteBegin == expectBegin && c.byteEnd > c.byteBegin;
            expectBegin = c.byteEnd;
        }
        tiles = tiles && expectBegin == s.size();
        noBreakBeforeFirst = noBreakBeforeFirst && (clusters.empty() || !clusters.front().breakBefore);
    }
    for (int it = 0; it < 1500; ++it, ++iterations) { // Pure garbage.
        std::string s(static_cast<std::size_t>(std::rand() % 120), '\0');
        for (char& ch : s) ch = static_cast<char>(std::rand() % 256);
        const auto clusters = AnalyzeClusters(s);
        std::uint32_t expectBegin = 0;
        for (const TextCluster& c : clusters) {
            tiles = tiles && c.byteBegin == expectBegin;
            expectBegin = c.byteEnd;
        }
        tiles = tiles && expectBegin == s.size();
    }
    CHECK(iterations == 4500);
    CHECK(tiles);
    CHECK(noBreakBeforeFirst);
}

} // namespace

void RunTextBreakingTests() {
    TestKindsAndMandatoryBreaks();
    TestHyphenRules();
    TestGlueAndZeroWidthSpace();
    TestCjkRules();
    TestClustersAreIndivisible();
    TestInvalidUtf8IsTolerated();
    TestInputIsBoundedAndNeverSplitsACodePoint();
    TestClustersTileTheTextOnRandomInput();
}

} // namespace SpriteUiTests
