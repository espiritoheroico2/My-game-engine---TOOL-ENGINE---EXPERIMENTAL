#include "SpriteUi/Widgets/TextEdit.hpp"

#include <algorithm>

namespace ToolEngine::SpriteUi::Widgets {

namespace {

bool IsContinuation(char c) noexcept {
    return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

bool IsControl(char c) noexcept {
    const auto u = static_cast<unsigned char>(c);
    return u < 0x20 || u == 0x7F;
}

} // namespace

TextEditState::TextEditState(std::string text) : text_{std::move(text)} {
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

std::size_t TextEditState::PrevBoundary(std::size_t pos) const noexcept {
    if (pos == 0) return 0;
    --pos;
    while (pos > 0 && IsContinuation(text_[pos])) --pos;
    return pos;
}

std::size_t TextEditState::NextBoundary(std::size_t pos) const noexcept {
    if (pos >= text_.size()) return text_.size();
    ++pos;
    while (pos < text_.size() && IsContinuation(text_[pos])) ++pos;
    return pos;
}

void TextEditState::SetCaret(std::size_t pos, bool extend) noexcept {
    caret_ = pos;
    if (!extend) anchor_ = pos;
}

void TextEditState::InsertText(std::string_view utf8) {
    std::string clean;
    clean.reserve(utf8.size());
    for (char c : utf8) {
        if (!IsControl(c)) clean.push_back(c);
    }
    if (clean.empty() && !HasSelection()) return;

    Record();
    if (HasSelection()) DeleteSelection();
    text_.insert(caret_, clean);
    caret_ = anchor_ = caret_ + clean.size();
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

void TextEditState::SelectAll() noexcept {
    anchor_ = 0;
    caret_ = text_.size();
}

} // namespace ToolEngine::SpriteUi::Widgets
