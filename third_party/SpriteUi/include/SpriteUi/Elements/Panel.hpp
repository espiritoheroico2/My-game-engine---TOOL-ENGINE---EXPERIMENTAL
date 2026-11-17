#pragma once

// Painel (child window): um contêiner com moldura e uma area de conteudo que recorta os filhos e pode rolar.
//
// Partes: a moldura (fundo e contorno, na caixa externa), a area de conteudo (ContentRect, que e o recorte dos
// filhos) e uma barra fina de rolagem a direita, desenhada so quando o conteudo e maior que a area.
//
// Conteudo: contentSize e o tamanho do conteudo, sem limite fixo. Se for maior que a area, o painel rola; se
// couber, nao rola. A rolagem e uma so, aplicada a todos os filhos: ArrangeChildren arranja cada um no espaco do
// conteudo deslocado. Assim o deslocamento nunca fica so no primeiro filho.
//
// Recorte: a area de conteudo corta os filhos. Um filho totalmente fora dela nao e desenhado nem recebe clique;
// ChildVisible diz quais sao. Nada fora da caixa externa e desenhado.
//
// Rolagem: scroll (>= 0) e quanto o conteudo andou para cima e para a esquerda. Valores nao finitos sao
// descartados. Depois de mudar contentSize ou o tamanho do painel, chame ArrangeChildren.
//
// Exemplo:
//   auto id = tree.Create<Panel>(window);
//   auto* panel = tree.GetAs<Panel>(id);
//   panel->contentSize = {200.0f, 1200.0f};   // Conteudo mais alto que a area: rola.
//   panel->ScrollBy({0.0f, 60.0f});
//   panel->ArrangeChildren(tree);

#include "SpriteUi/Core/Math.hpp"
#include "SpriteUi/Elements/Element.hpp"
#include "SpriteUi/Elements/ElementTree.hpp"
#include "SpriteUi/Rendering/DrawList.hpp"

#include <cstdint>
#include <vector>

namespace ToolEngine::SpriteUi::Elements {

struct PanelStyle {
    float cornerRadius = 0.0f; // Raio dos cantos do painel; zero = retos.
    Vec4 background{0.11f, 0.12f, 0.15f, 1.0f};
    Vec4 border{0.3f, 0.32f, 0.38f, 1.0f};
    Vec4 thumb{0.45f, 0.48f, 0.55f, 1.0f};
    float borderWidth = 1.0f;
    float thumbWidth = 4.0f;
    float thumbInset = 2.0f;  // Espaco entre a barra e a borda direita da area de conteudo.
};

class Panel final : public Element {
  public:
    Vec2 contentSize{0.0f, 0.0f};  // Tamanho do conteudo; maior que a area faz o painel rolar.
    Vec2 scroll{0.0f, 0.0f};       // Rolagem atual, dentro de 0 e MaxScroll().
    float wheelStep = 24.0f;       // Pixels por passo da roda.

    // Soma delta a rolagem e a prende no intervalo valido. Delta nao finito e ignorado.
    void ScrollBy(Vec2 delta) noexcept;

    // Passos da roda do mouse: positivo rola para baixo. Passo nao finito e ignorado.
    void ScrollByWheel(float steps) noexcept;

    // Maior rolagem em cada eixo: o conteudo que nao cabe na area de conteudo. Zero quando cabe.
    [[nodiscard]] Vec2 MaxScroll() const noexcept;

    // Arranja os filhos no espaco do conteudo deslocado pela rolagem. Prende a rolagem antes, se o conteudo encolheu.
    void ArrangeChildren(ElementTree& tree);

    // Filho com alguma parte dentro da area de conteudo. Filho fora dela nao e desenhado nem recebe clique.
    [[nodiscard]] bool ChildVisible(const Element& child) const noexcept;
};

// Itens do painel: fundo e contorno na caixa externa; barra de rolagem so quando o conteudo rola.
[[nodiscard]] std::vector<Rendering::UIRenderItem> BuildPanelItems(const Panel& panel, const PanelStyle& style,
                                                                   float zOrder, std::uint64_t paintOrder);

} // namespace ToolEngine::SpriteUi::Elements
