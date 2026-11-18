#pragma once

// Elemento base da SpriteUI: caixa, espacamento, ancoras, estados e ligacoes com a arvore.
//
// Esta classe guarda dados e geometria. Comportamento (transicoes de estado e eventos) e apresentacao
// (itens de desenho) ficam nas subclasses, cada uma no seu arquivo. Um elemento nunca e referenciado por
// ponteiro de fora da arvore: use UIHandle (ElementTree::Get).
//
// Modelo de caixa: a caixa externa (border) vem da ancora e dos offsets dentro da area do pai, descontada a
// margem. A area util (content rect) e a caixa menos o padding. Filhos ficam dentro da area util do pai.
//
// Coordenadas: logicas (pixels independentes de DPI), origem no canto superior esquerdo.

#include "SpriteUi/Core/Handle.hpp"
#include "SpriteUi/Core/Math.hpp"
#include "SpriteUi/Core/Transform2D.hpp"

#include <limits>
#include <vector>

namespace ToolEngine::SpriteUi::Elements {

// Espacamento nos quatro lados (padding ou margin).
struct Insets {
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
    float left = 0.0f;
};

// Estados independentes. Um elemento pode estar focado e pressionado ao mesmo tempo.
struct ElementStates {
    bool hovered = false;
    bool pressed = false;
    bool focused = false;
    bool disabled = false;
};

// Valor de maximo "sem limite".
inline constexpr float NoMaxSize = std::numeric_limits<float>::max();

// Reduz o retangulo pelos quatro lados. Insets negativos ou nao finitos contam como zero; a largura e a
// altura nunca ficam negativas.
[[nodiscard]] Rect ShrinkRect(Rect rect, Insets insets) noexcept;

// Limita um tamanho entre min e max, componente a componente. Se min passa de max, vale max. O resultado
// nunca e negativo, e valores nao finitos viram zero.
[[nodiscard]] Vec2 ClampSize(Vec2 size, Vec2 minSize, Vec2 maxSize) noexcept;

class Element {
  public:
    Element() = default;
    Element(const Element&) = delete;
    Element& operator=(const Element&) = delete;
    virtual ~Element() = default;

    // Identidade e arvore. Preenchidos pelo ElementTree; nao editar a mao.
    UIHandle handle{};
    UIHandle parent{};
    std::vector<UIHandle> children;

    // Posicao dentro da area do pai: ancoras (anchorMin, anchorMax) e offsets. O pivo nao entra no layout.
    UIRectTransform transform{};

    Insets padding{};  // Reduz a area util. Nao e pintado.
    Insets margin{};   // Espaco reservado no pai, descontado antes da ancora. Nao e pintado.
    Vec2 minSize{0.0f, 0.0f};
    Vec2 maxSize{NoMaxSize, NoMaxSize};

    ElementStates states{};

    // Caixa externa calculada pelo ElementTree::Arrange.
    Rect border{};

    // Area util: caixa externa menos o padding. Filhos e conteudo se posicionam dentro dela.
    [[nodiscard]] Rect ContentRect() const noexcept { return ShrinkRect(border, padding); }

    // Caixa externa resolvida dentro da area dada (area do pai, ja com a margem do elemento descontada).
    // Ancoras invalidas devolvem retangulo vazio na origem.
    [[nodiscard]] Rect ArrangeIn(Rect parentContent) const noexcept;
};

} // namespace ToolEngine::SpriteUi::Elements
