#include "SpriteUi/Widgets/TextEdit.hpp"

#include <algorithm>

namespace ToolEngine::SpriteUi::Widgets {

namespace {

using Utf8Text = std::string_view;

bool IsContinuation(char c) noexcept {
    return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

bool IsControl(char c) noexcept {
    const auto u = static_cast<unsigned char>(c);
    return u < 0x20 || u == 0x7F;
}

// Bytes do codepoint que comeca com `lead`; 0 se `lead` nao pode começar um codepoint valido.
std::size_t SequenceLength(unsigned char lead) noexcept {
    if (lead < 0x80) return 1;
    if (lead >= 0xC2 && lead <= 0xDF) return 2;
    if (lead >= 0xE0 && lead <= 0xEF) return 3;
    if (lead >= 0xF0 && lead <= 0xF4) return 4;
    return 0;
}

// Confere continuacoes e os intervalos proibidos (overlong, surrogates, acima de U+10FFFF).
bool IsValidSequenceAt(Utf8Text text, std::size_t pos, std::size_t length) noexcept {
    if (length == 0 || pos + length > text.size()) return false;
    for (std::size_t i = 1; i < length; ++i) {
        if (!IsContinuation(text[pos + i])) return false;
    }
    if (length == 1) return true;
    const auto lead = static_cast<unsigned char>(text[pos]);
    const auto second = static_cast<unsigned char>(text[pos + 1]);
    if (lead == 0xE0 && second < 0xA0) return false;
    if (lead == 0xED && second >= 0xA0) return false;
    if (lead == 0xF0 && second < 0x90) return false;
    if (lead == 0xF4 && second >= 0x90) return false;
    return true;
}

bool IsValidUtf8(Utf8Text text) noexcept {
    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t length = SequenceLength(static_cast<unsigned char>(text[pos]));
        if (!IsValidSequenceAt(text, pos, length)) return false;
        pos += length;
    }
    return true;
}

// Codepoint do texto ja validado que comeca em pos (com `length` bytes).
std::uint32_t DecodeAt(const std::string& text, std::size_t pos) noexcept {
    const auto lead = static_cast<unsigned char>(text[pos]);
    const std::size_t length = SequenceLength(lead);
    if (length == 1) return lead;
    std::uint32_t codepoint = lead & (0xFFu >> (length + 1));
    for (std::size_t i = 1; i < length; ++i) {
        codepoint = (codepoint << 6) | (static_cast<unsigned char>(text[pos + i]) & 0x3Fu);
    }
    return codepoint;
}

// Marcas combinantes entram no caractere anterior: "e" + acento e um unico caractere para edicao.
bool IsCombiningMark(std::uint32_t codepoint) noexcept {
    return (codepoint >= 0x0300 && codepoint <= 0x036F) ||
           (codepoint >= 0x1AB0 && codepoint <= 0x1AFF) ||
           (codepoint >= 0x1DC0 && codepoint <= 0x1DFF) ||
           (codepoint >= 0x20D0 && codepoint <= 0x20FF) ||
           (codepoint >= 0xFE20 && codepoint <= 0xFE2F);
}

bool StartsCombiningMark(const std::string& text, std::size_t pos) noexcept {
    return pos < text.size() && IsCombiningMark(DecodeAt(text, pos));
}

// Maior prefixo de `utf8` que cabe em `room` bytes sem cortar um codepoint.
Utf8Text FitWholeCodepoints(Utf8Text utf8, std::size_t room) noexcept {
    if (utf8.size() <= room) return utf8;
    std::size_t cut = room;
    while (cut > 0 && IsContinuation(utf8[cut])) --cut;
    return utf8.substr(0, cut);
}

std::string WithoutControls(Utf8Text utf8, bool acceptNewlines) {
    std::string clean;
    clean.reserve(utf8.size());
    for (char c : utf8) {
        if (!IsControl(c) || (acceptNewlines && c == '\n')) clean.push_back(c);
    }
    return clean;
}

} // namespace

TextEditState::TextEditState(std::string text) : TextEditState(std::move(text), false) {}

TextEditState::TextEditState(std::string text, bool acceptNewlines)
    : text_{IsValidUtf8(text) ? std::string(FitWholeCodepoints(text, kMaxTextBytes)) : std::string()},
      acceptNewlines_{acceptNewlines} {
    caret_ = anchor_ = text_.size();
}

std::pair<std::size_t, std::size_t> TextEditState::SelectionRange() const noexcept {
    return {std::min(anchor_, caret_), std::max(anchor_, caret_)};
}

std::string TextEditState::SelectedText() const {
    const auto [begin, end] = SelectionRange();
    return text_.substr(begin, end - begin);
}

void TextEditState::Record() {
    undo_.push_back(Snapshot{text_, caret_, anchor_});
    if (undo_.size() > kMaxUndo) undo_.erase(undo_.begin());
}

void TextEditState::DeleteSelection() {
    const auto [begin, end] = SelectionRange();
    text_.erase(begin, end - begin);
    caret_ = anchor_ = begin;
}

std::size_t TextEditState::PrevCodepointStart(std::size_t pos) const noexcept {
    if (pos == 0) return 0;
    --pos;
    while (pos > 0 && IsContinuation(text_[pos])) --pos;
    return pos;
}

std::size_t TextEditState::PrevBoundary(std::size_t pos) const noexcept {
    if (pos == 0) return 0;
    std::size_t start = PrevCodepointStart(pos);
    while (start > 0 && StartsCombiningMark(text_, start)) start = PrevCodepointStart(start);
    return start;
}

std::size_t TextEditState::NextBoundary(std::size_t pos) const noexcept {
    if (pos >= text_.size()) return text_.size();
    // O texto e UTF-8 valido e pos esta em fronteira: o codepoint em pos tem ao menos um byte.
    std::size_t next = pos + SequenceLength(static_cast<unsigned char>(text_[pos]));
    while (next < text_.size() && StartsCombiningMark(text_, next)) {
        next += SequenceLength(static_cast<unsigned char>(text_[next]));
    }
    return next;
}

std::size_t TextEditState::ClusterStart(std::size_t pos) const noexcept {
    if (pos >= text_.size()) return text_.size();
    while (pos > 0 && IsContinuation(text_[pos])) --pos;
    while (pos > 0 && StartsCombiningMark(text_, pos)) pos = PrevCodepointStart(pos);
    return pos;
}

void TextEditState::SetCaret(std::size_t pos, bool extend) noexcept {
    caret_ = ClusterStart(pos);
    if (!extend) anchor_ = caret_;
}

void TextEditState::InsertText(std::string_view utf8) {
    if (!IsValidUtf8(utf8)) return;
    const std::string clean = WithoutControls(utf8, acceptNewlines_);
    const auto [begin, end] = SelectionRange();
    const std::size_t roomAfterSelection = kMaxTextBytes - (text_.size() - (end - begin));
    const std::string_view fitting = FitWholeCodepoints(clean, roomAfterSelection);
    if (fitting.empty() && !HasSelection()) return;

    Record();
    if (HasSelection()) DeleteSelection();
    text_.insert(caret_, fitting);
    caret_ = anchor_ = caret_ + fitting.size();
}

bool TextEditState::Backspace() {
    if (!HasSelection() && caret_ == 0) return false;
    Record();
    if (HasSelection()) {
        DeleteSelection();
    } else {
        const std::size_t from = PrevBoundary(caret_);
        text_.erase(from, caret_ - from);
        caret_ = anchor_ = from;
    }
    return true;
}

bool TextEditState::Delete() {
    if (!HasSelection() && caret_ >= text_.size()) return false;
    Record();
    if (HasSelection()) {
        DeleteSelection();
    } else {
        const std::size_t to = NextBoundary(caret_);
        text_.erase(caret_, to - caret_);
    }
    return true;
}

std::string TextEditState::Cut() {
    if (!HasSelection()) return {};
    std::string removed = SelectedText();
    Record();
    DeleteSelection();
    return removed;
}

bool TextEditState::Undo() {
    if (undo_.empty()) return false;
    Snapshot s = std::move(undo_.back());
    undo_.pop_back();
    text_ = std::move(s.text);
    caret_ = std::min(s.caret, text_.size());
    anchor_ = std::min(s.anchor, text_.size());
    return true;
}

void TextEditState::MoveLeft(bool extend) noexcept {
    if (!extend && HasSelection()) {
        SetCaret(SelectionRange().first, false);
        return;
    }
    SetCaret(PrevBoundary(caret_), extend);
}

void TextEditState::MoveRight(bool extend) noexcept {
    if (!extend && HasSelection()) {
        SetCaret(SelectionRange().second, false);
        return;
    }
    SetCaret(NextBoundary(caret_), extend);
}

void TextEditState::MoveHome(bool extend) noexcept {
    SetCaret(0, extend);
}

void TextEditState::MoveEnd(bool extend) noexcept {
    SetCaret(text_.size(), extend);
}

void TextEditState::MoveCaretTo(std::size_t byteOffset, bool extend) noexcept {
    SetCaret(std::min(byteOffset, text_.size()), extend);
}

void TextEditState::SelectAll() noexcept {
    anchor_ = 0;
    caret_ = text_.size();
}

} // namespace ToolEngine::SpriteUi::Widgets
