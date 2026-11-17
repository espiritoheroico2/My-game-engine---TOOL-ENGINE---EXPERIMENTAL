#pragma once

// Painel da moldura de janela: transforma WindowFrame (geometria e estados) em itens de render. Desenha, nesta
// ordem: corpo com borda, cabecalho (barra ou aba), tracos dos botoes. Nao desenha o texto do titulo: o chamador
// escreve no retangulo de WindowFrameTitleRect. Nao desenha o conteudo: o chamador o recorta em WindowFrameContent.
//
// Cores: RGBA linear premultiplicado, mesmo formato de Primitives.hpp. O tema (WindowFrameTheme.hpp) monta as cores.
//
// Exemplo: a janela "Console" encaixada usa a mesma aba que o "Inspector da Caixa azul": mesma altura, mesmo
// raio, mesma cor quando ativa. So o titulo muda.

#include "SpriteUi/Core/Handle.hpp"
#include "SpriteUi/Core/Math.hpp"
#include "SpriteUi/Rendering/DrawList.hpp"
#include "SpriteUi/Widgets/WindowFrame.hpp"

#include <cstdint>
#include <vector>

namespace ToolEngine::SpriteUi::Widgets {

struct WindowFrameColors {
    Vec4 body{0.13f, 0.14f, 0.17f, 1.0f};
    Vec4 border{0.3f, 0.32f, 0.38f, 1.0f};
    Vec4 borderDragging{0.55f, 0.65f, 0.9f, 1.0f};
    Vec4 headerActive{0.18f, 0.2f, 0.26f, 1.0f};
    Vec4 headerInactive{0.12f, 0.13f, 0.16f, 1.0f};
    Vec4 headerDragging{0.22f, 0.25f, 0.34f, 1.0f};
    Vec4 glyph{0.9f, 0.92f, 0.95f, 1.0f};
};

// Espessura e tamanho dos tracos dos botoes (linha de minimizar e X de fechar).
inline constexpr float kWindowFrameGlyphHalfSize = 4.0f;
inline constexpr float kWindowFrameGlyphThickness = 1.5f;

// Itens da moldura, na ordem de pintura. Cada item usa paintOrder e avanca um. Sem clip (a moldura fica por fora
// do conteudo).
[[nodiscard]] std::vector<Rendering::UIRenderItem> BuildWindowFrameItems(UIHandle owner, Rect frame,
                                                                         const WindowFrameMetrics& metrics,
                                                                         const WindowFrameState& state,
                                                                         const WindowFrameColors& colors, float zOrder,
                                                                         std::uint64_t paintOrder);

} // namespace ToolEngine::SpriteUi::Widgets
