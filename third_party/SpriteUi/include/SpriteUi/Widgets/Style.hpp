#pragma once

// Estruturas de estilo da SpriteUI. Este arquivo e o unico lugar onde os defaults visuais dos widgets sao
// escritos. Cada struct documenta seus campos; os valores abaixo sao os de referencia e nao mudam sem
// atualizar os testes (ver SpriteUI.TextField, secao de defaults).
//
// Convencao: cores em RGBA linear com alfa premultiplicado (mesmo formato de tintPremultipliedLinear).
// Um widget guarda a sua struct de estilo como membro ou recebe uma por chamada; trocar o estilo nao exige
// mudar a logica do widget.
//
// Fora deste arquivo, por serem parametros de layout ou de um componente especifico, ficam onde estao:
//  - Alturas de linha, controle e botao: Layout/LayoutPatterns.hpp (LayoutTokens).
//  - Altura da barra de titulo de janela: UIWindowComponent::titleBarHeight (Widgets/Widgets.hpp).
//  - Cor da linha de grafico: Widgets/Graphs.hpp (uma por tipo de grafico).

#include "SpriteUi/Core/Math.hpp"
#include "SpriteUi/Text/FontId.hpp"

namespace ToolEngine::SpriteUi::Widgets {

// Campo de texto de uma linha (TextField). Os valores padrao sao os do editor de referencia.
struct TextFieldStyle {
    Vec4 background{0.12f, 0.12f, 0.14f, 1.0f};      // Fundo sem foco.
    Vec4 focusBackground{0.16f, 0.16f, 0.20f, 1.0f}; // Fundo com foco.
    Vec4 text{0.92f, 0.92f, 0.94f, 1.0f};            // Texto.
    Vec4 selection{0.15f, 0.22f, 0.37f, 0.6f}; // Selecao (premultiplicada: rgb ja vem multiplicado por alfa).
    Vec4 caret{0.95f, 0.95f, 1.0f, 1.0f};      // Caret (so com foco).
    float paddingX = 8.0f;                     // Recuo horizontal entre a borda da caixa e o texto.
    float height = 30.0f;                      // Altura padrao para o chamador dimensionar a caixa.
    float fontSize = 16.0f;                    // Tamanho em pixels usado no layout do texto.
    float caretWidth = 1.5f;                   // Largura do caret em pixels.
    Text::FontId fontId{};                     // Fonte escolhida pelo chamador (ver FontRegistry).
};

// Area de texto de varias linhas (TextArea). Cores como no campo de uma linha, com recuo vertical.
struct TextAreaStyle {
    Vec4 background{0.12f, 0.12f, 0.14f, 1.0f};      // Fundo sem foco.
    Vec4 focusBackground{0.16f, 0.16f, 0.20f, 1.0f}; // Fundo com foco.
    Vec4 text{0.92f, 0.92f, 0.94f, 1.0f};            // Texto.
    Vec4 selection{0.15f, 0.22f, 0.37f, 0.6f};       // Selecao (premultiplicada).
    Vec4 caret{0.95f, 0.95f, 1.0f, 1.0f};            // Caret (so com foco).
    float paddingX = 8.0f;                           // Recuo horizontal entre a borda da caixa e o texto.
    float paddingY = 6.0f;   // Recuo vertical entre a borda da caixa e a primeira linha.
    float caretWidth = 1.5f; // Largura do caret em pixels.
};

// Interruptor (UIToggleComponent). Cor do trilho quando ligado e quando desligado.
struct ToggleStyle {
    Vec4 onColor{0.2f, 0.8f, 0.2f, 1.0f};  // Ligado.
    Vec4 offColor{0.5f, 0.5f, 0.5f, 1.0f}; // Desligado.
};

} // namespace ToolEngine::SpriteUi::Widgets
