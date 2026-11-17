#pragma once

// Campo de texto como elemento. O texto fica dentro da area util (content rect), alinhado a esquerda, com o
// padding real do elemento. A edicao (texto, caret, selecao, desfazer) e feita pelo TextEditState; este
// elemento guarda a rolagem e gera os itens de desenho.
//
// Modo: de uma linha (padrao) ou multilinha. A escolha vem na criacao (TextFieldOptions) e nao muda depois.
//  - De uma linha: o texto nao quebra. Quando passa da largura, a rolagem horizontal segue o caret.
//  - Multilinha: o texto quebra por palavras (e entre clusters, para palavra maior que a linha) na largura util
//    menos a faixa da barra de rolagem. A rolagem vertical segue o caret; subir e descer mantem a coluna.
//
// Regras:
//  - Layout: o chamador monta o layout com MakeTextFieldLayoutConfig e o entrega a cada chamada. Depois de
//    editar o texto, refaca o layout antes de desenhar, clicar ou mover o caret.
//  - Rolagem: o deslocamento acompanha o caret. Chame KeepCaretVisible depois de cada edicao ou movimento.
//    Desenhar nao muda a rolagem.
//  - Caret: fica no pen origin do glifo seguinte (nunca no meio de um caractere de varios bytes).
//  - Selecao: retangulo atras do texto, uma faixa por linha. Placeholder: aparece so enquanto o texto esta vazio.
//  - Multilinha: so as linhas visiveis geram itens de desenho. A barra de rolagem vertical fica na borda direita;
//    o polegar e proporcional a parte visivel. Ponteiro e roda passam por PointerDown, PointerMove, PointerUp e
//    Wheel.
//  - Recorte: texto, selecao e caret saem recortados pela area util (clipIndex fornecido pelo chamador).

#include "SpriteUi/Core/Math.hpp"
#include "SpriteUi/Elements/Element.hpp"
#include "SpriteUi/Elements/Label.hpp"
#include "SpriteUi/Elements/PointerEvent.hpp"
#include "SpriteUi/Rendering/DrawList.hpp"
#include "SpriteUi/Text/TextLayout.hpp"
#include "SpriteUi/Text/TextLines.hpp"
#include "SpriteUi/Widgets/ScrollBar.hpp"
#include "SpriteUi/Widgets/TextEdit.hpp"
#include "SpriteUi/Widgets/Widgets.hpp"

#include <string>
#include <vector>

namespace ToolEngine::SpriteUi::Elements {

// Cores em RGBA linear premultiplicado.
struct TextFieldStyle {
    float cornerRadius = 0.0f; // Raio dos cantos do fundo do campo; zero = retos.
    Vec4 background{0.12f, 0.12f, 0.14f, 1.0f};
    Vec4 focusBackground{0.16f, 0.16f, 0.20f, 1.0f};
    Vec4 text{0.92f, 0.92f, 0.94f, 1.0f};
    Vec4 placeholder{0.45f, 0.45f, 0.5f, 1.0f};
    Vec4 selection{0.15f, 0.22f, 0.37f, 0.6f};
    Vec4 caret{0.95f, 0.95f, 1.0f, 1.0f};
    Vec4 scrollTrack{0.1f, 0.1f, 0.12f, 1.0f};
    Vec4 scrollThumb{0.4f, 0.4f, 0.46f, 1.0f};
    float caretWidth = 1.5f;
    float scrollBarWidth = 6.0f; // Faixa reservada a direita no campo multilinha, fora da quebra de linha.
};

// Escolhas feitas na criacao do campo: tree.Create<TextField>(parent, TextFieldOptions{.multiline = true}).
struct TextFieldOptions {
    bool multiline = false;
};

class TextField final : public Element {
  public:
    explicit TextField(TextFieldOptions options = {});

    std::string placeholder;
    float fontSize = 16.0f; // Usado pelo chamador ao montar o layout do texto.

    [[nodiscard]] bool Multiline() const noexcept { return multiline_; }

    [[nodiscard]] Widgets::TextEditState& Edit() noexcept { return edit_; }
    [[nodiscard]] const Widgets::TextEditState& Edit() const noexcept { return edit_; }

    // Deslocamento horizontal (campo de uma linha) e vertical (multilinha), em pixels de layout.
    [[nodiscard]] float ScrollX() const noexcept { return scroll_.scrollOffset.x; }
    [[nodiscard]] float ScrollY() const noexcept { return scroll_.scrollOffset.y; }

    // Barra de rolagem (multilinha): visivel so quando o texto passa da area util. Trilho e polegar em coordenadas
    // da area util. Valores validos depois de KeepCaretVisible.
    [[nodiscard]] bool ScrollBarVisible() const noexcept;
    [[nodiscard]] Rect ScrollBarTrack() const noexcept { return scrollBar_.Track(); }
    [[nodiscard]] Rect ScrollThumb() const noexcept { return scrollBar_.ThumbRect(scroll_); }

    // Foco. Sem foco, as rolagens voltam a zero e o caret nao e desenhado.
    void SetFocused(bool focused) noexcept;

    // Clique (ou arrasto com extend) em um ponto relativo a area util. Recebe o foco e move o caret para o
    // offset mais proximo. Depois atualiza a rolagem.
    void PointerDown(Vec2 pointInContent, Vec2 contentSize, const Text::TextLayoutResult& textLayout,
                     const TextFieldStyle& style, bool extend = false);

    // Atualiza a rolagem e a barra para a area util (contentSize) e, com foco, mantem o caret visivel. Multilinha
    // rola na vertical; de uma linha, na horizontal. Nunca rola alem do fim do texto. Chame depois de cada edicao,
    // movimento ou quadro.
    void KeepCaretVisible(const Text::TextLayoutResult& textLayout, Vec2 contentSize,
                          const TextFieldStyle& style) noexcept;

    // Seta para cima e para baixo (so multilinha). Mantem a coluna do caret. Retorna true se o caret mudou.
    // Depois, chame KeepCaretVisible.
    [[nodiscard]] bool MoveUp(const Text::TextLayoutResult& textLayout, bool extend = false);
    [[nodiscard]] bool MoveDown(const Text::TextLayoutResult& textLayout, bool extend = false);

    // Barra (multilinha): arrasto do polegar e roda do mouse. Retornam true se a rolagem mudou. PointerUp solta o
    // polegar. Fora do multilinha, nada muda.
    [[nodiscard]] bool PointerMove(Vec2 pointInContent) noexcept;
    void PointerUp() noexcept;
    [[nodiscard]] bool Wheel(float delta) noexcept;

  private:
    [[nodiscard]] bool MoveVertically(const Text::TextLayoutResult& textLayout, int direction, bool extend);
    void UpdateScrollView(const Text::TextLayoutResult& textLayout, Vec2 contentSize,
                          const TextFieldStyle& style) noexcept;
    void FollowCaret(const Text::TextLayoutResult& textLayout, Vec2 viewport, float caretWidth) noexcept;

    bool multiline_ = false;
    Widgets::TextEditState edit_;
    Widgets::UIScrollViewComponent scroll_;
    Widgets::ScrollBarControl scrollBar_;
    Text::VerticalCaretMotion verticalMotion_;
};

// Configuracao do layout do texto. De uma linha: sem quebra. Multilinha: quebra por palavras na largura util
// menos a faixa da barra de rolagem; uma largura zero no layout significa sem limite, entao o campo nunca
// passa zero para a quebra.
[[nodiscard]] Text::TextLayoutConfig MakeTextFieldLayoutConfig(const TextField& field, Vec2 contentSize,
                                                               const TextFieldStyle& style) noexcept;

// Origem do texto dentro da area util: alinhado a esquerda; de uma linha, centralizado na vertical; recuado
// pela rolagem.
[[nodiscard]] Vec2 TextFieldTextOrigin(const TextField& field, Vec2 contentSize,
                                       const Text::TextLayoutResult& textLayout) noexcept;

// Itens do campo: fundo, selecao, texto (ou placeholder quando vazio) e caret com foco. O texto e o caret
// saem recortados pelo clipIndex, que o chamador cria para a area util.
[[nodiscard]] std::vector<Rendering::UIRenderItem> BuildTextFieldItems(
    const TextField& field, const TextFieldStyle& style, const Text::TextLayoutResult& textLayout,
    const Text::TextLayoutResult& placeholderLayout, Rect content, const TextTarget& target,
    std::uint32_t clipIndex);

} // namespace ToolEngine::SpriteUi::Elements
