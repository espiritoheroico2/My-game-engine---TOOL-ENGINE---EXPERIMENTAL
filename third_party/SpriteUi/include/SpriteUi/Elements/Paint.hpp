#pragma once

// Auxiliares de apresentacao compartilhados pelos elementos: variacao de cor por estado, contorno e formas arredondadas.

#include "SpriteUi/Core/Math.hpp"
#include "SpriteUi/Elements/PointerEvent.hpp"
#include "SpriteUi/Rendering/DrawList.hpp"

#include <cstdint>
#include <vector>

namespace ToolEngine::SpriteUi::Elements {

// Clareia (Lift) ou escurece (Shade) a cor em rgb pela fracao dada. O alfa nao muda.
[[nodiscard]] Vec4 LightenColor(Vec4 color, float amount) noexcept;
[[nodiscard]] Vec4 DarkenColor(Vec4 color, float amount) noexcept;

// Quatro retangulos finos de largura width que formam o contorno de box, por dentro da caixa. Cada item usa
// paint e avanca um; zOrder e comum a todos.
void AppendOutlineItems(UIHandle owner, Rect box, float width, Vec4 color, float zOrder, std::uint64_t& paint,
                        std::vector<Rendering::UIRenderItem>& out);

// Retangulo de cantos totalmente arredondados (circulo ou pilula): raio = metade do menor lado. Um item.
void AppendRoundedItem(UIHandle owner, Rect rect, Vec4 color, float zOrder, std::uint64_t& paint,
                       std::vector<Rendering::UIRenderItem>& out);

// Retangulo crescido em todos os lados pela mesma quantidade (negativa encolhe). Usado no anel de foco.
[[nodiscard]] Rect GrowRect(Rect rect, float amount) noexcept;

// Parte de row que cai dentro do contorno de popup (recuado pela largura). Linhas de lista usam isso: o contorno
// e desenhado antes das linhas, e a linha nao pode cobri-lo. Entre linhas vizinhas nao sobra fresta.
[[nodiscard]] Rect RowInsideBorder(Rect row, Rect popup, float borderWidth) noexcept;

// Area desenhada de um controle: o retangulo com cantos de raio radius (zero = reto; o raio e limitado a metade
// do menor lado, entao raio grande vira pilula). Nos cantos, so conta o interior do arco. Usado para que o clique
// siga o desenho e nao a caixa do elemento.
[[nodiscard]] bool ContainsDrawn(Rect drawn, float radius, Vec2 point) noexcept;

// Evento visto pelo controle, restrito a area desenhada. Fora dela o ponteiro nao esta 'dentro': Down nao
// inicia pressao, Up nao gera Click, e Enter ou Move viram Leave, para o hover sumir. Sem posicao finita, fora.
[[nodiscard]] PointerEvent WithinDrawn(const PointerEvent& event, Rect drawn, float radius) noexcept;

// Fundo sem contorno: retangulo reto quando radius e zero, retangulo arredondado (MakeRoundedRect) quando
// positivo. Um item; paint avanca um.
void AppendFill(UIHandle owner, Rect rect, float radius, Vec4 color, std::uint32_t clipIndex, float zOrder,
                std::uint64_t& paint, std::vector<Rendering::UIRenderItem>& out);

// Fundo com contorno de largura borderWidth. Com radius zero sao o fundo reto e as quatro faixas de
// AppendOutlineItems. Com radius positivo sao duas caixas arredondadas: a da cor do contorno e, por dentro,
// a do fundo, recuada pela largura. Itens depois desta chamada que pintam por cima da borda devem ficar
// dentro dela, senao cobrem o contorno.
void AppendBox(UIHandle owner, Rect box, float radius, float borderWidth, Vec4 fill, Vec4 border,
               std::uint32_t clipIndex, float zOrder, std::uint64_t& paint,
               std::vector<Rendering::UIRenderItem>& out);

} // namespace ToolEngine::SpriteUi::Elements
