#pragma once

// Area de texto de varias linhas (widget generico). Como o TextField, nao conhece fontes nem atlas: o
// chamador faz o shaping e o layout com quebra de palavras e entrega o resultado. A area cuida de edicao,
// caret, selecao, rolagem vertical e recorte.
//
// Regras:
//  - Enter insere quebra de linha (InsertNewline). A quebra automatica vem do layout (TextWrap::Word).
//  - O chamador refaz o layout quando NeedsLayout() diz: o texto mudou ou a largura util mudou.
//    Mover o caret, selecionar ou rolar nunca pede layout novo. Antes de tratar clique ou tecla, refaca o
//    layout se preciso.
//  - Subir e descer (MoveUp, MoveDown) andam pelas linhas do layout e mantem a coluna do caret.
//  - A rolagem vertical segue a linha do caret. Sem foco, mostra o inicio do texto.
//  - Texto, selecao e caret saem recortados a caixa. So as linhas visiveis geram itens de desenho.
//  - Coordenadas: a caixa tem origem no canto superior esquerdo. O texto comeca em
//    (paddingX, paddingY) menos a rolagem.

#include "SpriteUi/Core/Handle.hpp"
#include "SpriteUi/Core/Math.hpp"
#include "SpriteUi/Rendering/DrawList.hpp"
#include "SpriteUi/Text/TextLayout.hpp"
#include "SpriteUi/Text/TextLines.hpp"
#include "SpriteUi/Widgets/Style.hpp"
#include "SpriteUi/Widgets/TextEdit.hpp"
#include "SpriteUi/Widgets/TextField.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ToolEngine::SpriteUi::Widgets {

class TextArea {
  public:
    TextArea() = default;
    explicit TextArea(std::string text);

    [[nodiscard]] const std::string& Text() const noexcept { return edit_.Text(); }
    [[nodiscard]] const TextEditState& Edit() const noexcept { return edit_; }

    // Foco. Ao perder o foco a rolagem volta ao inicio do texto.
    void SetFocus(bool focused) noexcept;
    [[nodiscard]] bool Focused() const noexcept { return focused_; }
    [[nodiscard]] float ScrollY() const noexcept { return scrollY_; }

    // Largura util para o texto: largura da caixa menos os recuos laterais (nunca negativa).
    [[nodiscard]] static float ContentWidth(Vec2 boxSize, const TextAreaStyle& style) noexcept;

    // Layout. NeedsLayout() e verdadeiro ate SetLayout() ser chamado com o texto e a largura atuais.
    [[nodiscard]] bool NeedsLayout(float contentWidth) const noexcept;
    void SetLayout(Text::TextLayoutResult layout, float contentWidth);
    [[nodiscard]] const Text::TextLayoutResult& Layout() const noexcept { return layout_; }

    // Edicao. Cada uma grava um ponto de desfazer quando muda o texto.
    void InsertText(std::string_view utf8);
    void InsertNewline() { InsertText("\n"); }
    [[nodiscard]] bool Backspace();
    [[nodiscard]] bool Delete();
    [[nodiscard]] bool Undo();

    // Movimento. extend = true estende a selecao. Home e End vao ao inicio e ao fim da linha do caret.
    void MoveLeft(bool extend = false) noexcept;
    void MoveRight(bool extend = false) noexcept;
    void MoveHome(bool extend = false) noexcept;
    void MoveEnd(bool extend = false) noexcept;
    void SelectAll() noexcept;

    // Seta para cima e para baixo. Retorna true se o caret mudou.
    [[nodiscard]] bool MoveUp(bool extend = false);
    [[nodiscard]] bool MoveDown(bool extend = false);

    // Clique (ou arrasto com extend) em posicao local da caixa: foca a area e posiciona o caret.
    void PointerDown(Vec2 localPos, Vec2 boxSize, const TextAreaStyle& style, bool extend = false);

    // Itens da area na ordem de desenho. O recorte da caixa e o ultimo elemento de clips.
    [[nodiscard]] std::vector<Rendering::UIRenderItem> Build(Rect box, const TextAreaStyle& style,
                                                             const TextFieldRenderTarget& target,
                                                             std::vector<Rendering::ClipRectGPU>& clips);

  private:
    [[nodiscard]] static float ViewportHeight(Vec2 boxSize, const TextAreaStyle& style) noexcept;
    [[nodiscard]] Vec2 TextOrigin(const TextAreaStyle& style) const noexcept;
    void FollowCaret(Vec2 boxSize, const TextAreaStyle& style) noexcept;
    [[nodiscard]] const Text::TextLine* LineContainingCaret() const noexcept;
    [[nodiscard]] bool MoveVertically(int direction, bool extend);
    void MarkTextChanged(bool changed) noexcept;

    TextEditState edit_{std::string{}, true};
    Text::TextLayoutResult layout_;
    float layoutWidth_ = 0.0f;
    bool layoutValid_ = false;
    bool focused_ = false;
    float scrollY_ = 0.0f;
    Text::VerticalCaretMotion verticalMotion_;
};

} // namespace ToolEngine::SpriteUi::Widgets
