#pragma once

// Operacoes sobre as linhas de um TextLayoutResult, usadas pela area de texto (Widgets::TextArea) e pelo
// campo multilinha (Elements::TextField): faixa de linhas visiveis, glifos dessas linhas, retangulo de selecao
// por linha e movimento vertical do caret.
//
// Coluna: subir e descer mantem a coluna. Ela (goalX) e guardada entre movimentos verticais seguidos e refeita
// quando o caret sai do offset que o ultimo movimento vertical produziu (digitacao, clique, setas laterais).

#include "SpriteUi/Core/Math.hpp"
#include "SpriteUi/Text/TextLayout.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace ToolEngine::SpriteUi::Text {

// Faixa [first, end) das linhas que tocam [top, bottom) na coordenada y do layout.
[[nodiscard]] std::pair<std::size_t, std::size_t> VisibleLineRange(const TextLayoutResult& layout, float top,
                                                                   float bottom);

// Layout com so os glifos das linhas [firstLine, endLine). O gerador de itens le so os quads.
[[nodiscard]] TextLayoutResult VisibleGlyphs(const TextLayoutResult& layout, std::size_t firstLine,
                                             std::size_t endLine);

// Parte de [begin, end) que cai na linha, em coordenadas do layout. Vazio se a linha nao tem selecao.
[[nodiscard]] std::optional<Rect> LineSelectionRect(const TextLayoutResult& layout, const TextLine& line,
                                                    std::size_t begin, std::size_t end);

// Estado do movimento vertical: coluna-alvo e o offset que o ultimo movimento produziu.
struct VerticalCaretMotion {
    float goalX = 0.0f;
    std::uint32_t lastOffset = 0;
    bool active = false;
};

// Offset de byte para o caret subir (direction -1) ou descer (+1) uma linha, na coluna do movimento. Subir a
// partir da primeira linha vai ao inicio do texto; descer da ultima vai ao fim (textBytes).
[[nodiscard]] std::uint32_t MoveCaretVertically(VerticalCaretMotion& motion, const TextLayoutResult& layout,
                                                std::uint32_t caret, int direction, std::uint32_t textBytes);

} // namespace ToolEngine::SpriteUi::Text
