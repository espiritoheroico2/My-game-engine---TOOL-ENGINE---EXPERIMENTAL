#include "SpriteUi/Widgets/TextArea.hpp"

#include "SpriteUi/Rendering/Primitives.hpp"
#include "SpriteUi/Text/TextLines.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <utility>

namespace ToolEngine::SpriteUi::Widgets {


TextArea::TextArea(std::string text) : edit_{std::move(text), true} {}

void TextArea::SetFocus(bool focused) noexcept {
    focused_ = focused;
    if (!focused_) scrollY_ = 0.0f;
}

float TextArea::ContentWidth(Vec2 boxSize, const TextAreaStyle& style) noexcept {
    if (!IsFinite(boxSize) || !IsFinite(style.paddingX)) return 0.0f;
    return std::max(0.0f, boxSize.x - 2.0f * style.paddingX);
}

float TextArea::ViewportHeight(Vec2 boxSize, const TextAreaStyle& style) noexcept {
    if (!IsFinite(boxSize) || !IsFinite(style.paddingY)) return 0.0f;
    return std::max(0.0f, boxSize.y - 2.0f * style.paddingY);
}

Vec2 TextArea::TextOrigin(const TextAreaStyle& style) const noexcept {
    return {style.paddingX, style.paddingY - scrollY_};
}

bool TextArea::NeedsLayout(float contentWidth) const noexcept {
    return !layoutValid_ || contentWidth != layoutWidth_;
}

void TextArea::SetLayout(Text::TextLayoutResult layout, float contentWidth) {
    layout_ = std::move(layout);
    layoutWidth_ = contentWidth;
    layoutValid_ = true;
}

void TextArea::MarkTextChanged(bool changed) noexcept {
    if (changed) layoutValid_ = false;
}

void TextArea::InsertText(std::string_view utf8) {
    edit_.InsertText(utf8);
    MarkTextChanged(true);
}

bool TextArea::Backspace() {
    const bool changed = edit_.Backspace();
    MarkTextChanged(changed);
    return changed;
}

bool TextArea::Delete() {
    const bool changed = edit_.Delete();
    MarkTextChanged(changed);
    return changed;
}

bool TextArea::Undo() {
    const bool changed = edit_.Undo();
    MarkTextChanged(changed);
    return changed;
}

void TextArea::MoveLeft(bool extend) noexcept {
    edit_.MoveLeft(extend);
}

void TextArea::MoveRight(bool extend) noexcept {
    edit_.MoveRight(extend);
}

void TextArea::SelectAll() noexcept {
    edit_.SelectAll();
}

const Text::TextLine* TextArea::LineContainingCaret() const noexcept {
    if (layout_.lines.empty()) return nullptr;
    const auto caret = static_cast<std::uint32_t>(edit_.Caret());
    for (const Text::TextLine& line : layout_.lines) {
        if (caret >= line.byteBegin && caret < line.byteEnd) return &line;
    }
    // Fim do texto sem quebra no fim: pertence a ultima linha. Depois de uma quebra final, nao ha linha (o
    // caret esta na linha vazia de baixo, que o layout nao lista).
    const Text::TextLine& last = layout_.lines.back();
    if (caret == last.byteEnd && !last.endsWithNewline) return &last;
    return nullptr;
}

void TextArea::MoveHome(bool extend) noexcept {
    const Text::TextLine* line = LineContainingCaret();
    if (line != nullptr) edit_.MoveCaretTo(line->byteBegin, extend);
}

void TextArea::MoveEnd(bool extend) noexcept {
    const Text::TextLine* line = LineContainingCaret();
    if (line == nullptr) return;
    // A quebra de linha fica no fim da linha; o fim visivel e antes dela.
    const std::uint32_t end = line->endsWithNewline ? line->byteEnd - 1 : line->byteEnd;
    edit_.MoveCaretTo(end, extend);
}

bool TextArea::MoveUp(bool extend) {
    return MoveVertically(-1, extend);
}

bool TextArea::MoveDown(bool extend) {
    return MoveVertically(1, extend);
}

bool TextArea::MoveVertically(int direction, bool extend) {
    const std::size_t before = edit_.Caret();
    const auto target = Text::MoveCaretVertically(verticalMotion_, layout_, static_cast<std::uint32_t>(before),
                                                  direction, static_cast<std::uint32_t>(edit_.Text().size()));
    edit_.MoveCaretTo(target, extend);
    return edit_.Caret() != before;
}

void TextArea::PointerDown(Vec2 localPos, Vec2 boxSize, const TextAreaStyle& style, bool extend) {
    SetFocus(true);
    if (!IsFinite(localPos)) return;
    const Vec2 origin = TextOrigin(style);
    const std::uint32_t offset =
        Text::FindInsertionOffset(layout_, {localPos.x - origin.x, localPos.y - origin.y});
    edit_.MoveCaretTo(offset, extend);
    verticalMotion_ = {};
    FollowCaret(boxSize, style);
}

void TextArea::FollowCaret(Vec2 boxSize, const TextAreaStyle& style) noexcept {
    if (!focused_) {
        scrollY_ = 0.0f;
        return;
    }
    const float viewport = ViewportHeight(boxSize, style);
    const Rect caret = Text::GetCaretRect(layout_, static_cast<std::uint32_t>(edit_.Caret()));
    const float caretTop = caret.position.y;
    const float caretBottom = caretTop + caret.size.y;

    // Ajuste minimo: o caret entra pela borda de cima ou de baixo, sem rolar alem do necessario.
    if (caretTop < scrollY_) scrollY_ = caretTop;
    if (caretBottom - scrollY_ > viewport) scrollY_ = caretBottom - viewport;

    // Limite: nao rola alem do fim do texto (ou do caret, se ele estiver na linha vazia de baixo).
    const float contentHeight = std::max(layout_.size.y, caretBottom);
    const float maxScroll = std::max(0.0f, contentHeight - viewport);
    scrollY_ = std::clamp(scrollY_, 0.0f, maxScroll);
}

std::vector<Rendering::UIRenderItem> TextArea::Build(Rect box, const TextAreaStyle& style,
                                                     const TextFieldRenderTarget& target,
                                                     std::vector<Rendering::ClipRectGPU>& clips) {
    std::vector<Rendering::UIRenderItem> out;
    if (!IsFinite(box.position) || !IsFinite(box.size)) return out;

    FollowCaret(box.size, style);

    const Vec2 local = TextOrigin(style);
    const Vec2 origin{box.position.x + local.x, box.position.y + local.y};
    const Vec4 background = focused_ ? style.focusBackground : style.background;

    std::uint64_t paint = target.paintOrder;
    // Fundo: a propria caixa, sem recorte (ele e o recorte).
    out.push_back(
        Rendering::MakeSolidRect(target.owner, box, background, Rendering::NoClipIndex, 0.0f, paint++));

    // Recorte da caixa: texto, selecao e caret ficam dentro dela. E o ultimo elemento de clips.
    clips.push_back({box.position.x, box.position.y, box.Max().x, box.Max().y});
    const auto clip = static_cast<std::uint32_t>(clips.size() - 1);

    const float top = scrollY_;
    const float bottom = scrollY_ + ViewportHeight(box.size, style);
    const auto [firstLine, endLine] = Text::VisibleLineRange(layout_, top, bottom);

    if (focused_ && edit_.HasSelection()) {
        const auto [begin, end] = edit_.SelectionRange();
        for (std::size_t i = firstLine; i < endLine; ++i) {
            const std::optional<Rect> part = Text::LineSelectionRect(layout_, layout_.lines[i], begin, end);
            if (!part) continue;
            const Rect onScreen{{origin.x + part->position.x, origin.y + part->position.y}, part->size};
            out.push_back(Rendering::MakeSolidRect(target.owner, onScreen, style.selection, clip,
                                                   target.zOrder, paint++));
        }
    }

    UILayoutRect textBox;
    textBox.resolvedPosition = origin;
    textBox.resolvedSize = layout_.size;
    const Text::TextLayoutResult glyphs = Text::VisibleGlyphs(layout_, firstLine, endLine);
    auto glyphItems =
        Text::GenerateTextRenderItems(glyphs, textBox, style.text, target.textureSlice, clip, target.zOrder,
                                      paint, target.owner, target.textRenderFlags);
    paint += glyphItems.size();
    out.insert(out.end(), glyphItems.begin(), glyphItems.end());

    if (focused_) {
        const Rect caret = Text::GetCaretRect(layout_, static_cast<std::uint32_t>(edit_.Caret()));
        const Rect onScreen{{origin.x + caret.position.x, origin.y + caret.position.y},
                            {style.caretWidth, caret.size.y}};
        out.push_back(
            Rendering::MakeSolidRect(target.owner, onScreen, style.caret, clip, target.zOrder, paint++));
    }
    return out;
}

} // namespace ToolEngine::SpriteUi::Widgets
