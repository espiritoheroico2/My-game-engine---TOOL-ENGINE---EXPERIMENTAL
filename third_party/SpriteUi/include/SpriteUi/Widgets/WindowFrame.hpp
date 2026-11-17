#pragma once

// Moldura de janela (WindowFrame): modelo puro, sem desenho e sem ponteiro. E a unica fonte de geometria das
// janelas do editor: barra (flutuante) ou aba (encaixada), botoes, area de conteudo, cantos e rolagem.
//
// Partes da moldura, de fora para dentro:
//  - Cabecalho (Header): faixa do topo com o titulo. Flutuando, ocupa a largura toda (barra de titulo). Encaixada,
//    ocupa so tracos da largura de uma aba (tabWidth). Mesma altura nos dois casos.
//  - Botoes: minimizar e fechar, a direita do cabecalho, recuados pela borda interna.
//  - Conteudo (Content): o que sobra dentro da borda, abaixo do cabecalho. O conteudo deve ser recortado nessa area.
//  - Borda: faixa de borderWidth por fora do conteudo. Os cantos sao arredondados com um raio so.
//
// Estados (WindowFrameState): ativa ou inativa (cor do cabecalho), arrastando (realce da borda) e encaixada ou
// flutuante (cabecalho em barra ou em aba). Os estados sao independentes: uma janela pode estar ativa e
// encaixada ao mesmo tempo.
//
// Exemplo: a janela "Inspector da Caixa azul" (400 x 300 px, barra de 28 px, borda de 1 px) tem conteudo em
// x 1..399, y 29..299 (ou seja, 398 x 270 px). Encaixada, o cabecalho vira uma aba de 140 px no topo esquerdo.

#include "SpriteUi/Core/Math.hpp"

#include <cstdint>

namespace ToolEngine::SpriteUi::Widgets {

struct WindowFrameState {
    bool active = false;   // janela com foco: cabecalho com a cor de destaque
    bool dragging = false; // arrasto em curso: borda com realce
    bool docked = false;   // true: encaixada (cabecalho vira aba); false: flutuante (cabecalho e barra)
};

// Medidas da moldura em pixels logicos. O raio dos cantos vem do estilo (WindowFrameTheme.hpp).
struct WindowFrameMetrics {
    float headerHeight = 28.0f;   // altura do cabecalho (barra ou aba)
    float borderWidth = 1.0f;     // faixa da borda, por dentro da caixa
    float cornerRadius = 0.0f;    // raio pedido; a geometria limita a metade do menor lado
    float buttonSize = 18.0f;     // lado de cada botao
    float buttonGap = 4.0f;       // espaco entre os dois botoes
    float buttonInset = 6.0f;     // recuo dos botoes da borda direita do cabecalho
    float tabWidth = 140.0f;      // largura da aba quando encaixada
};

enum class WindowFrameButton : std::uint8_t { Minimize, Close };

// Parte da moldura sob o ponto. Outside quando o ponto esta fora da caixa.
enum class WindowFramePart : std::uint8_t { Outside, Header, Minimize, Close, Content, Border };

// Cabecalho da moldura: barra (flutuante) ou aba (encaixada), no topo da caixa.
[[nodiscard]] Rect WindowFrameHeader(Rect frame, const WindowFrameMetrics& metrics, bool docked) noexcept;

// Area de conteudo: dentro da borda e abaixo do cabecalho. Nunca tem tamanho negativo.
[[nodiscard]] Rect WindowFrameContent(Rect frame, const WindowFrameMetrics& metrics) noexcept;

// Faixa do titulo: o cabecalho menos os botoes, com recuo de textPadding a esquerda. O texto cabe aqui.
[[nodiscard]] Rect WindowFrameTitleRect(Rect frame, const WindowFrameMetrics& metrics, bool docked,
                                        float textPadding = 8.0f) noexcept;

// Retangulo de um botao de controle, dentro do cabecalho (a direita, recuado). Tamanho zero quando nao cabe.
[[nodiscard]] Rect WindowFrameButtonRect(Rect frame, const WindowFrameMetrics& metrics, bool docked,
                                         WindowFrameButton button) noexcept;

// Raio efetivo: pedido, prendido entre zero e metade do menor lado. Valor nao finito vale zero.
[[nodiscard]] float WindowFrameCornerRadius(Rect frame, float requested) noexcept;

// Parte da moldura sob o ponto. Botoes ganham do cabecalho; cabecalho ganha do conteudo.
[[nodiscard]] WindowFramePart WindowFramePartAt(Rect frame, const WindowFrameMetrics& metrics, bool docked,
                                                Vec2 point) noexcept;

// Rolagem do conteudo: o deslocamento fica entre zero e o que sobra do conteudo na area visivel.
[[nodiscard]] float WindowFrameClampScroll(float offset, float contentExtent, float viewExtent) noexcept;

} // namespace ToolEngine::SpriteUi::Widgets
