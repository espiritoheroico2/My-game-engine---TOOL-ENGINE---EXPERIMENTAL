#include "SpriteUi/Elements/TextField.hpp"

#include "SpriteUi/Elements/Paint.hpp"
#include "SpriteUi/Rendering/Primitives.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>

namespace ToolEngine::SpriteUi::Elements {

namespace {

std::uint32_t Offset(std::size_t byteOffset) noexcept {
    return static_cast<std::uint32_t>(byteOffset);
}

// Faixa de linhas a desenhar: no multilinha, so as que tocam a area util rolada; de uma linha, todas.
std::pair<std::size_t, std::size_t> DrawnLines(const TextField& field, const Text::TextLayoutResult& layout,
                                               float viewportHeight) {
    if (!field.Multiline()) return {0, layout.lines.size()};
    const float top = field.ScrollY();
    return Text::VisibleLineRange(layout, top, top + std::max(0.0f, viewportHeight));
}

// Trilho da barra: faixa na borda direita da area util.
Rect ScrollTrackRect(Vec2 viewport, const TextFieldStyle& style) noexcept {
    const float width = std::clamp(style.scrollBarWidth, 0.0f, viewport.x);
    return {{viewport.x - width, 0.0f}, {width, viewport.y}};
}

} // namespace

TextField::TextField(TextFieldOptions options)
    : multiline_{options.multiline}, edit_{std::string{}, options.multiline} {}

bool TextField::ScrollBarVisible() const noexcept {
    return multiline_ && scrollBar_.HasOverflow(scroll_);
}

void TextField::SetFocused(bool focused) noexcept {
    states.focused = focused;
    if (focused) return;
    scroll_.scrollOffset = {};
    verticalMotion_ = {};
}

void TextField::PointerDown(Vec2 pointInContent, Vec2 contentSize, const Text::TextLayoutResult& textLayout,
                            const TextFieldStyle& style, bool extend) {
    SetFocused(true);
    // O clique usa a rolagem que esta na tela: so as medidas da barra sao atualizadas aqui.
    UpdateScrollView(textLayout, {std::max(0.0f, contentSize.x), std::max(0.0f, contentSize.y)}, style);
    if (!IsFinite(pointInContent)) return;
    if (ScrollBarVisible() && ScrollBarTrack().Contains(pointInContent)) {
        (void)scrollBar_.PointerDown(pointInContent, scroll_); // Clique na barra nao move o caret.
        return;
    }
    const Vec2 origin = TextFieldTextOrigin(*this, contentSize, textLayout);
    // origin ja inclui a rolagem: o ponto no espaco do layout e a diferenca para ela.
    const std::uint32_t offset =
        Text::FindInsertionOffset(textLayout, {pointInContent.x - origin.x, pointInContent.y - origin.y});
    edit_.MoveCaretTo(offset, extend);
    verticalMotion_ = {};
    KeepCaretVisible(textLayout, contentSize, style);
}

bool TextField::PointerMove(Vec2 pointInContent) noexcept {
    if (!multiline_) return false;
    return scrollBar_.PointerMove(pointInContent, scroll_);
}

void TextField::PointerUp() noexcept {
    scrollBar_.PointerUp();
}

bool TextField::Wheel(float delta) noexcept {
    if (!multiline_) return false;
    return scrollBar_.Wheel(delta, scroll_);
}

void TextField::KeepCaretVisible(const Text::TextLayoutResult& textLayout, Vec2 contentSize,
                                 const TextFieldStyle& style) noexcept {
    if (!IsFinite(contentSize)) contentSize = {};
    const Vec2 viewport{std::max(0.0f, contentSize.x), std::max(0.0f, contentSize.y)};
    UpdateScrollView(textLayout, viewport, style);
    if (states.focused) {
        FollowCaret(textLayout, viewport, style.caretWidth);
    } else {
        scroll_.scrollOffset = {};
    }
}

void TextField::UpdateScrollView(const Text::TextLayoutResult& textLayout, Vec2 viewport,
                                 const TextFieldStyle& style) noexcept {
    // Conteudo: o texto, mais o caret quando ele fica depois dele (linha vazia de baixo, fim da linha).
    const Rect caret = Text::GetCaretRect(textLayout, Offset(edit_.Caret()));
    const float caretRight = caret.position.x + std::max(0.0f, style.caretWidth);
    const float caretBottom = caret.position.y + caret.size.y;

    scroll_.allowScrollX = !multiline_;
    scroll_.allowScrollY = multiline_;
    scroll_.viewportSize = viewport;
    scroll_.contentSize = multiline_ ? Vec2{viewport.x, std::max(textLayout.size.y, caretBottom)}
                                     : Vec2{std::max(textLayout.size.x, caretRight), viewport.y};
    scrollBar_.SetTrack(ScrollTrackRect(viewport, style));
    scroll_.ClampScroll();
}

void TextField::FollowCaret(const Text::TextLayoutResult& textLayout, Vec2 viewport, float caretWidth) noexcept {
    const Rect caret = Text::GetCaretRect(textLayout, Offset(edit_.Caret()));
    Vec2& offset = scroll_.scrollOffset;
    // Ajuste minimo: o caret entra pela borda de cima ou de baixo (multilinha) ou pela esquerda ou direita.
    if (multiline_) {
        const float caretTop = caret.position.y;
        const float caretBottom = caretTop + caret.size.y;
        if (caretTop < offset.y) offset.y = caretTop;
        if (caretBottom - offset.y > viewport.y) offset.y = caretBottom - viewport.y;
    } else {
        const float caretLeft = caret.position.x;
        const float caretRight = caretLeft + std::max(0.0f, caretWidth);
        if (caretLeft < offset.x) offset.x = caretLeft;
        if (caretRight - offset.x > viewport.x) offset.x = caretRight - viewport.x;
    }
    scroll_.ClampScroll();
}

bool TextField::MoveUp(const Text::TextLayoutResult& textLayout, bool extend) {
    return MoveVertically(textLayout, -1, extend);
}

bool TextField::MoveDown(const Text::TextLayoutResult& textLayout, bool extend) {
    return MoveVertically(textLayout, 1, extend);
}

bool TextField::MoveVertically(const Text::TextLayoutResult& textLayout, int direction, bool extend) {
    if (!multiline_) return false;
    const std::size_t before = edit_.Caret();
    const std::uint32_t target = Text::MoveCaretVertically(verticalMotion_, textLayout, Offset(before), direction,
                                                           Offset(edit_.Text().size()));
    edit_.MoveCaretTo(target, extend);
    return edit_.Caret() != before;
}

Text::TextLayoutConfig MakeTextFieldLayoutConfig(const TextField& field, Vec2 contentSize,
                                                 const TextFieldStyle& style) noexcept {
    Text::TextLayoutConfig config;
    config.fontSize = field.fontSize;
    if (!field.Multiline()) return config;
    config.wrap = Text::TextWrap::Word;
    if (IsFinite(contentSize) && IsFinite(style.scrollBarWidth))
        config.maxWidth = std::max(0.0f, contentSize.x - style.scrollBarWidth);
    return config;
}

Vec2 TextFieldTextOrigin(const TextField& field, Vec2 contentSize,
                         const Text::TextLayoutResult& textLayout) noexcept {
    if (field.Multiline()) return {0.0f, -field.ScrollY()};
    const float freeHeight = contentSize.y - textLayout.lineHeight;
    return {-field.ScrollX(), 0.5f * freeHeight};
}

std::vector<Rendering::UIRenderItem> BuildTextFieldItems(
    const TextField& field, const TextFieldStyle& style, const Text::TextLayoutResult& textLayout,
    const Text::TextLayoutResult& placeholderLayout, Rect content, const TextTarget& target,
    std::uint32_t clipIndex) {
    std::vector<Rendering::UIRenderItem> out;
    if (!IsFinite(content.position) || !IsFinite(content.size) || !IsFinite(textLayout.size)) return out;

    const bool focused = field.states.focused;
    const Widgets::TextEditState& edit = field.Edit();
    std::uint64_t paint = target.paintOrder;

    const Vec4 background = focused ? style.focusBackground : style.background;
    AppendFill(field.handle, field.border, style.cornerRadius, background, Rendering::NoClipIndex, 0.0f, paint, out);

    const Vec2 local = TextFieldTextOrigin(field, content.size, textLayout);
    const Vec2 origin{content.position.x + local.x, content.position.y + local.y};
    const auto [firstLine, endLine] = DrawnLines(field, textLayout, content.size.y);

    if (focused && edit.HasSelection()) {
        const auto [begin, end] = edit.SelectionRange();
        for (std::size_t i = firstLine; i < endLine; ++i) {
            const std::optional<Rect> part = Text::LineSelectionRect(textLayout, textLayout.lines[i], begin, end);
            if (!part) continue;
            const Rect onScreen{{origin.x + part->position.x, origin.y + part->position.y}, part->size};
            out.push_back(Rendering::MakeSolidRect(field.handle, onScreen, style.selection, clipIndex,
                                                   target.zOrder, paint++));
        }
    }

    const bool showPlaceholder = edit.Text().empty();
    const bool cullLines = field.Multiline() && !showPlaceholder;
    const Text::TextLayoutResult visibleGlyphs =
        cullLines ? Text::VisibleGlyphs(textLayout, firstLine, endLine) : Text::TextLayoutResult{};
    const Text::TextLayoutResult& shown = showPlaceholder ? placeholderLayout : textLayout;
    const Text::TextLayoutResult& drawn = cullLines ? visibleGlyphs : shown;
    const Vec4 color = showPlaceholder ? style.placeholder : style.text;
    UILayoutRect textBox;
    textBox.resolvedPosition = origin;
    textBox.resolvedSize = shown.size;
    auto glyphs = Text::GenerateTextRenderItems(drawn, textBox, color, target.textureSlice, clipIndex,
                                                target.zOrder, paint, field.handle, target.renderFlags);
    paint += glyphs.size() + 1;
    out.insert(out.end(), glyphs.begin(), glyphs.end());

    if (field.ScrollBarVisible()) {
        const Rect track = field.ScrollBarTrack();
        const Rect thumb = field.ScrollThumb();
        const Vec2 trackOrigin{content.position.x + track.position.x, content.position.y + track.position.y};
        const Vec2 thumbOrigin{content.position.x + thumb.position.x, content.position.y + thumb.position.y};
        out.push_back(Rendering::MakeSolidRect(field.handle, {trackOrigin, track.size}, style.scrollTrack,
                                               clipIndex, target.zOrder, paint++));
        out.push_back(Rendering::MakeSolidRect(field.handle, {thumbOrigin, thumb.size}, style.scrollThumb,
                                               clipIndex, target.zOrder, paint++));
    }

    if (focused) {
        const Rect caret = Text::GetCaretRect(textLayout, Offset(edit.Caret()));
        out.push_back(Rendering::MakeSolidRect(field.handle,
                                               {{origin.x + caret.position.x, origin.y + caret.position.y},
                                                {style.caretWidth, textLayout.lineHeight}},
                                               style.caret, clipIndex, target.zOrder, paint++));
    }
    return out;
}

} // namespace ToolEngine::SpriteUi::Elements
