#include "SpriteUi/Text/LineBreak.hpp"

#include <algorithm>

namespace ToolEngine::SpriteUi::Text {

namespace {

struct Decoded {
    char32_t codePoint = 0xFFFD;
    std::uint32_t length = 1;
};

[[nodiscard]] bool IsContinuation(std::string_view s, std::size_t index) noexcept {
    return index < s.size() && (static_cast<unsigned char>(s[index]) & 0xC0u) == 0x80u;
}

[[nodiscard]] char32_t Payload(std::string_view s, std::size_t index) noexcept {
    return static_cast<char32_t>(static_cast<unsigned char>(s[index]) & 0x3Fu);
}

// Invalid, overlong, surrogate or out-of-range sequences decode to U+FFFD and consume ONE byte.
[[nodiscard]] Decoded DecodeAt(std::string_view s, std::size_t i) noexcept {
    const auto lead = static_cast<unsigned char>(s[i]);
    if (lead < 0x80u) return {static_cast<char32_t>(lead), 1};
    if ((lead & 0xE0u) == 0xC0u && IsContinuation(s, i + 1)) {
        const char32_t cp = (static_cast<char32_t>(lead & 0x1Fu) << 6) | Payload(s, i + 1);
        if (cp >= 0x80u) return {cp, 2};
    }
    if ((lead & 0xF0u) == 0xE0u && IsContinuation(s, i + 1) && IsContinuation(s, i + 2)) {
        const char32_t cp = (static_cast<char32_t>(lead & 0x0Fu) << 12) |
                            (Payload(s, i + 1) << 6) | Payload(s, i + 2);
        if (cp >= 0x800u && (cp < 0xD800u || cp > 0xDFFFu)) return {cp, 3};
    }
    if ((lead & 0xF8u) == 0xF0u && IsContinuation(s, i + 1) && IsContinuation(s, i + 2) &&
        IsContinuation(s, i + 3)) {
        const char32_t cp = (static_cast<char32_t>(lead & 0x07u) << 18) | (Payload(s, i + 1) << 12) |
                            (Payload(s, i + 2) << 6) | Payload(s, i + 3);
        if (cp >= 0x10000u && cp <= 0x10FFFFu) return {cp, 4};
    }
    return {0xFFFD, 1};
}

[[nodiscard]] bool InRange(char32_t c, char32_t lo, char32_t hi) noexcept { return c >= lo && c <= hi; }

[[nodiscard]] bool IsCombining(char32_t c) noexcept {
    return InRange(c, 0x0300, 0x036F) || InRange(c, 0x0483, 0x0489) || InRange(c, 0x0591, 0x05BD) ||
           InRange(c, 0x064B, 0x065F) || InRange(c, 0x1AB0, 0x1AFF) || InRange(c, 0x1DC0, 0x1DFF) ||
           InRange(c, 0x20D0, 0x20FF) || InRange(c, 0xFE00, 0xFE0F) || InRange(c, 0xFE20, 0xFE2F) ||
           InRange(c, 0x1F3FB, 0x1F3FF) || InRange(c, 0xE0100, 0xE01EF);
}

[[nodiscard]] bool IsRegionalIndicator(char32_t c) noexcept { return InRange(c, 0x1F1E6, 0x1F1FF); }

[[nodiscard]] bool IsCjk(char32_t c) noexcept {
    return InRange(c, 0x3040, 0x30FF) || InRange(c, 0x3400, 0x4DBF) || InRange(c, 0x4E00, 0x9FFF) ||
           InRange(c, 0xF900, 0xFAFF) || InRange(c, 0x20000, 0x2FFFF) ||
           (InRange(c, 0x3001, 0x303F)) || InRange(c, 0xFF01, 0xFF60);
}

[[nodiscard]] bool IsCjkClosing(char32_t c) noexcept {
    switch (c) {
    case 0x3001: case 0x3002: case 0xFF0C: case 0xFF0E: case 0xFF01: case 0xFF1F: case 0xFF09:
    case 0xFF1A: case 0xFF1B: case 0x300D: case 0x300F: case 0x3011: case 0x300B: case 0x3009:
    case 0x30FC:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool IsCjkOpening(char32_t c) noexcept {
    switch (c) {
    case 0xFF08: case 0x300C: case 0x300E: case 0x3010: case 0x300A: case 0x3008:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool IsSpaceLike(char32_t c) noexcept {
    return c == ' ' || c == '\t' || c == 0x3000 || c == 0x200B || c == 0x2002 || c == 0x2003 || c == 0x2009;
}

[[nodiscard]] bool IsGlue(char32_t c) noexcept {
    return c == 0x00A0 || c == 0x2007 || c == 0x202F || c == 0x2060 || c == 0xFEFF;
}

[[nodiscard]] bool IsHyphenLike(char32_t c) noexcept {
    return c == '-' || c == 0x2010 || c == 0x2013 || c == 0x2014;
}

[[nodiscard]] bool IsDigit(char32_t c) noexcept { return c >= '0' && c <= '9'; }

[[nodiscard]] bool IsLetterOrDigit(char32_t c) noexcept {
    return IsDigit(c) || InRange(c, 'A', 'Z') || InRange(c, 'a', 'z') || c >= 0x00C0;
}

struct Flags {
    bool noBreakBefore = false;
    bool noBreakAfter = false;
    bool breakAfter = false;
    bool cjk = false;
};

} // namespace

std::vector<TextCluster> AnalyzeClusters(std::string_view text) {
    if (text.size() > MaxAnalyzedBytes) {
        std::size_t cut = MaxAnalyzedBytes;
        while (cut > 0 && IsContinuation(text, cut)) --cut; // Never split a code point.
        text = text.substr(0, cut);
    }

    std::vector<TextCluster> clusters;
    std::vector<Flags> flags;
    clusters.reserve(text.size());
    flags.reserve(text.size());

    for (std::size_t i = 0; i < text.size();) {
        const Decoded first = DecodeAt(text, i);
        std::size_t j = i + first.length;
        TextCluster cluster;
        cluster.byteBegin = static_cast<std::uint32_t>(i);
        cluster.base = first.codePoint;
        Flags f;

        if (first.codePoint == '\r') {
            if (j < text.size() && text[j] == '\n') ++j;
            cluster.kind = ClusterKind::Newline;
        } else if (first.codePoint == '\n' || first.codePoint == 0x0085 || first.codePoint == 0x2028 ||
                   first.codePoint == 0x2029) {
            cluster.kind = ClusterKind::Newline;
        } else {
            if (IsRegionalIndicator(first.codePoint) && j < text.size()) {
                const Decoded next = DecodeAt(text, j);
                if (IsRegionalIndicator(next.codePoint)) j += next.length; // Flag emoji = 2 indicators.
            }
            while (j < text.size()) {
                const Decoded next = DecodeAt(text, j);
                if (IsCombining(next.codePoint)) {
                    j += next.length;
                } else if (next.codePoint == 0x200D) { // ZWJ glues the following code point.
                    j += next.length;
                    if (j < text.size()) j += DecodeAt(text, j).length;
                } else {
                    break;
                }
            }
            if (IsSpaceLike(first.codePoint)) cluster.kind = ClusterKind::Space;
            f.cjk = IsCjk(first.codePoint);
            f.noBreakBefore = cluster.kind == ClusterKind::Space || IsCjkClosing(first.codePoint) ||
                              IsGlue(first.codePoint);
            f.noBreakAfter = IsCjkOpening(first.codePoint) || IsGlue(first.codePoint);
            f.breakAfter = cluster.kind == ClusterKind::Space || f.cjk;
        }
        cluster.byteEnd = static_cast<std::uint32_t>(j);
        clusters.push_back(cluster);
        flags.push_back(f);
        i = j;
    }

    // A hyphen offers an opportunity after it when it follows a letter/digit and precedes a non-digit.
    for (std::size_t k = 1; k + 1 < clusters.size(); ++k) {
        if (clusters[k].kind == ClusterKind::Normal && IsHyphenLike(clusters[k].base) &&
            clusters[k - 1].kind == ClusterKind::Normal && IsLetterOrDigit(clusters[k - 1].base) &&
            !IsDigit(clusters[k + 1].base)) {
            flags[k].breakAfter = true;
        }
    }

    for (std::size_t k = 1; k < clusters.size(); ++k) {
        const Flags& prev = flags[k - 1];
        const Flags& cur = flags[k];
        const bool allowed = !cur.noBreakBefore && !prev.noBreakAfter &&
                             clusters[k].kind != ClusterKind::Newline && (prev.breakAfter || cur.cjk);
        clusters[k].breakBefore = allowed;
    }
    return clusters;
}

} // namespace ToolEngine::SpriteUi::Text
