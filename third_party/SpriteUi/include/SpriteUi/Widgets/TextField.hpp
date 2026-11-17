#pragma once

// Campo de texto de uma linha (widget generico). Sem ImGui, SDL ou JSON: entrada por chamadas, saida por
// UIRenderItem e ClipRectGPU, como os demais widgets da biblioteca.
//
// Responsabilidades:
//  - Edicao e selecao: delegadas a TextEditState (TextEdit.hpp). O campo so muda o texto por essa classe.
//  - Rolagem horizontal: o deslocamento segue o caret (digitacao, clique, Home/End). Sem foco, o campo
//    mostra o inicio do texto (rolagem zero).
//  - Recorte: texto, selecao e caret saem recortados a caixa do campo. O recorte entra no vetor de clips
//    do chamador, e os itens apontam para ele pelo clipIndex.
//  - Layout: o chamador faz shaping e layout do texto com a fonte indicada em TextFieldStyle::fontId e
//    entrega o TextLayoutResult. O campo nao conhece atlas nem fontes.
//
// Coordenadas: a caixa tem origem no canto superior esquerdo. O texto fica centralizado verticalmente e
// comeca em paddingX menos a rolagem.

#include "SpriteUi/Core/Handle.hpp"
#include "SpriteUi/Core/Math.hpp"
#include "SpriteUi/Rendering/DrawList.hpp"
#include "SpriteUi/Text/TextLayout.hpp"
#include "SpriteUi/Widgets/Style.hpp"
#include "SpriteUi/Widgets/TextEdit.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace ToolEngine::SpriteUi::Widgets {

// Onde os itens sao registrados. O chamador preenche owner e textureSlice da fonte usada no layout.
struct TextFieldRenderTarget {
    UIHandle owner{};                                          // Handle valido exigido pelo extrator.
    std::uint32_t textureSlice = Rendering::WhiteTextureSlice; // Slice do atlas da fonte do texto.
    std::uint32_t textRenderFlags = 0u;                        // 0 = atlas de alfa (sem MSDF).
    float zOrder = 0.5f;                                       // Ordem entre os fundos (0) e a tela.
    std::uint64_t paintOrder = 0;                              // Base; cada item sobe um.
};

class TextField {
  public:
    TextField() = default;
    explicit TextField(std::string text);

    [[nodiscard]] TextEditState& Edit() noexcept { return edit_; }
    [[nodiscard]] const TextEditState& Edit() const noexcept { return edit_; }

    // Foco. Ao perder o foco a rolagem volta a zero (o campo mostra o inicio do texto).
    void SetFocus(bool focused) noexcept;
    [[nodiscard]] bool Focused() const noexcept { return focused_; }

    // Deslocamento horizontal atual, em pixels de layout.
    [[nodiscard]] float ScrollX() const noexcept { return scrollX_; }

    // Largura util do texto: largura da caixa menos o recuo dos dois lados (nunca negativa).
    [[nodiscard]] static float ViewportWidth(Vec2 boxSize, const TextFieldStyle& style) noexcept;

    // Origem do texto em coordenadas locais da caixa, ja com a rolagem aplicada.
    [[nodiscard]] Vec2 TextOrigin(Vec2 boxSize, const Text::TextLayoutResult& layout,
                                  const TextFieldStyle& style) const noexcept;

    // Atualiza a rolagem para que o caret (com a largura do caret) fique dentro da area util.
    // Sem foco, zera a rolagem. A rolagem fica entre zero e o excedente do texto.
    void FollowCaret(const Text::TextLayoutResult& layout, float viewportWidth, float caretWidth) noexcept;

    // Clique (ou arrasto com extend): caret no offset mais proximo da posicao local, lendo a rolagem atual.
    // Atualiza a rolagem depois de mover o caret.
    void PointerDown(Vec2 localPos, Vec2 boxSize, const Text::TextLayoutResult& layout,
                     const TextFieldStyle& style, bool extend = false);

    // Gera os itens do campo (fundo, selecao, texto, caret) e empilha um recorte da caixa em clips. Os itens
    // de texto, selecao e caret usam esse recorte. Chama FollowCaret antes de gerar, entao o caret aparece.
    // Retorna os itens na ordem de desenho; o recorte e o ultimo elemento de clips.
    [[nodiscard]] std::vector<Rendering::UIRenderItem> Build(Rect box, const Text::TextLayoutResult& layout,
                                                             const TextFieldStyle& style,
                                                             const TextFieldRenderTarget& target,
                                                             std::vector<Rendering::ClipRectGPU>& clips);

  private:
    TextEditState edit_;
    bool focused_ = false;
    float scrollX_ = 0.0f;
};

} // namespace ToolEngine::SpriteUi::Widgets
