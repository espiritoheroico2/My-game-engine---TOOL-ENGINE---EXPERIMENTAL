#include "SpriteUi/Widgets/TextField.hpp"

#include "SpriteUi/Rendering/Primitives.hpp"

#include <algorithm>
#include <utility>

namespace ToolEngine::SpriteUi::Widgets {

TextField::TextField(std::string text) : edit_{std::move(text)} {}

void TextField::SetFocus(bool focused) noexcept {
    focused_ = focused;
    // Sem foco o campo mostra o inicio do texto.
    if (!focused_) scrollX_ = 0.0f;
}

float TextField::ViewportWidth(Vec2 boxSize, const TextFieldStyle& style) noexcept {
    if (!IsFinite(boxSize) || !IsFinite(style.paddingX)) return 0.0f;
    return std::max(0.0f, boxSize.x - 2.0f * style.paddingX);
}

Vec2 TextField::TextOrigin(Vec2 boxSize, const Text::TextLayoutResult& layout,
                           const TextFieldStyle& style) const noexcept {
    const float textY = (boxSize.y - layout.lineHeight) * 0.5f;
    return {style.paddingX - scrollX_, textY};
}

void TextField::FollowCaret(const Text::TextLayoutResult& layout, float viewportWidth,
                            float caretWidth) noexcept {
    if (!focused_) {
        scrollX_ = 0.0f;
        return;
    }
    if (!IsFinite(viewportWidth) || !IsFinite(caretWidth)) return;
    const float viewport = std::max(0.0f, viewportWidth);
    const float caretX = Text::GetCaretRect(layout, static_cast<std::uint32_t>(edit_.Caret())).position.x;
    const float caretRight = caretX + std::max(0.0f, caretWidth);

    // Ajuste minimo: o caret entra pela esquerda ou pela direita, sem rolar alem do necessario.
    if (caretX < scrollX_) scrollX_ = caretX;
    if (caretRight - scrollX_ > viewport) scrollX_ = caretRight - viewport;

    // Limite: nao rola alem do fim do texto (ou do caret, se ele estiver depois do texto).
    const float contentWidth = std::max(layout.size.x, caretRight);
    const float maxScroll = std::max(0.0f, contentWidth - viewport);
    scrollX_ = std::clamp(scrollX_, 0.0f, maxScroll);
}

void TextField::PointerDown(Vec2 localPos, Vec2 boxSize, const Text::TextLayoutResult& layout,
                            const TextFieldStyle& style, bool extend) {
    SetFocus(true);
    if (!IsFinite(localPos)) return;
    const Vec2 origin = TextOrigin(boxSize, layout, style);
    const std::uint32_t offset =
        Text::FindInsertionOffset(layout, {localPos.x - origin.x, localPos.y - origin.y});
    edit_.MoveCaretTo(offset, extend);
    FollowCaret(layout, ViewportWidth(boxSize, style), style.caretWidth);
}

std::vector<Rendering::UIRenderItem> TextField::Build(Rect box, const Text::TextLayoutResult& layout,
                                                      const TextFieldStyle& style,
                                                      const TextFieldRenderTarget& target,
                                                      std::vector<Rendering::ClipRectGPU>& clips) {
    std::vector<Rendering::UIRenderItem> out;
    if (!IsFinite(box.position) || !IsFinite(box.size)) return out;

    FollowCaret(layout, ViewportWidth(box.size, style), style.caretWidth);

    const Vec2 local = TextOrigin(box.size, layout, style);
    const Vec2 origin{box.position.x + local.x, box.position.y + local.y};
    const float lineHeight = layout.lineHeight;
    const Vec4 bg = focused_ ? style.focusBackground : style.background;

    std::uint64_t paint = target.paintOrder;
    // Fundo: a propria caixa, sem recorte (ele e o recorte).
    out.push_back(Rendering::MakeSolidRect(target.owner, box, bg, Rendering::NoClipIndex, 0.0f, paint++));

    // Recorte da caixa: texto, selecao e caret ficam dentro dela. E o ultimo elemento de clips.
    clips.push_back({box.position.x, box.position.y, box.Max().x, box.Max().y});
    const auto clip = static_cast<std::uint32_t>(clips.size() - 1);

    if (focused_ && edit_.HasSelection()) {
        const auto [begin, end] = edit_.SelectionRange();
        const float x0 = Text::GetCaretRect(layout, static_cast<std::uint32_t>(begin)).position.x;
        const float x1 = Text::GetCaretRect(layout, static_cast<std::uint32_t>(end)).position.x;
        out.push_back(Rendering::MakeSolidRect(target.owner,
                                               {{origin.x + x0, origin.y}, {x1 - x0, lineHeight}},
                                               style.selection, clip, target.zOrder, paint++));
    }

    // Texto (um item por glifo). O helper de texto avanca paintOrder pelo numero de itens gerados.
    UILayoutRect textBox;
    textBox.resolvedPosition = origin;
    textBox.resolvedSize = layout.size;
    auto glyphs = Text::GenerateTextRenderItems(layout, textBox, style.text, target.textureSlice, clip,
                                                target.zOrder, paint, target.owner, target.textRenderFlags);
    paint += glyphs.size() + 1;
    out.insert(out.end(), glyphs.begin(), glyphs.end());

    if (focused_) {
        const float caretX = Text::GetCaretRect(layout, static_cast<std::uint32_t>(edit_.Caret())).position.x;
        out.push_back(Rendering::MakeSolidRect(
            target.owner, {{origin.x + caretX, origin.y}, {style.caretWidth, lineHeight}}, style.caret, clip,
            target.zOrder, paint++));
    }
    return out;
}

} // namespace ToolEngine::SpriteUi::Widgets
