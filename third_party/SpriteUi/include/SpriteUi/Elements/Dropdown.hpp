#pragma once

// Caixa de combinacao (combo, ou dropdown): um botao que mostra a opcao atual e uma lista aberta com as opcoes.
//
// Partes: o botao (fundo, seta em chevron a direita) e a lista (fundo, faixa do destaque, faixa da opcao
// escolhida e contorno). O estado (opcoes, escolhida, destaque e se esta aberta) vem do modelo DropdownControl
// (Controls.hpp). Este elemento acrescenta a geometria da lista.
//
// Posicao: a lista abre abaixo do botao. Se nao couber, abre acima. Ela nunca sai da janela: se nem assim couber,
// ganha altura do espaco disponivel e rolagem. A lista cresce com o numero de opcoes, sem limite fixo.
//
// Eventos: Click no botao abre ou fecha. Click numa opcao escolhe e fecha (valueChanged so se mudou). Escape e
// clique fora fecham sem escolher. Teclas (com foco): Seta para baixo abre, ou desce o destaque se ja aberta;
// Seta para cima sobe o destaque; Enter escolhe o destaque (ou abre, se fechada); Escape fecha.
// O chamador faz o hit test: ponteiro fora do botao e da lista aberta chama CloseOutside().
// Os textos (valor do botao e de cada opcao) sao desenhados pelo chamador.
//
// Exemplo:
//   auto id = tree.Create<Dropdown>(parent);
//   auto* combo = tree.GetAs<Dropdown>(id);
//   combo->control.options = {"Vermelho", "Verde", "Azul"};
//   combo->control.selectedIndex = 0;
//   auto events = combo->KeyPressed(DropdownKey::Down, window);   // Abre a lista.

#include "SpriteUi/Core/Math.hpp"
#include "SpriteUi/Elements/Element.hpp"
#include "SpriteUi/Elements/PointerEvent.hpp"
#include "SpriteUi/Elements/PressGesture.hpp"
#include "SpriteUi/Rendering/DrawList.hpp"
#include "SpriteUi/Widgets/Controls.hpp"

#include <cstdint>
#include <vector>

namespace ToolEngine::SpriteUi::Elements {

struct DropdownStyle {
    float cornerRadius = 0.0f; // Raio dos cantos do botao e do popup; zero = retos.
    Vec4 button{0.12f, 0.12f, 0.14f, 1.0f};
    Vec4 disabledButton{0.15f, 0.15f, 0.16f, 1.0f};
    Vec4 popup{0.1f, 0.1f, 0.12f, 1.0f};
    Vec4 popupBorder{0.3f, 0.32f, 0.38f, 1.0f};
    Vec4 highlight{0.05f, 0.2f, 0.55f, 1.0f};
    Vec4 selectedRow{0.17f, 0.19f, 0.24f, 1.0f};
    Vec4 chevron{0.85f, 0.87f, 0.92f, 1.0f};
    Vec4 focusRing{0.95f, 0.8f, 0.3f, 1.0f};
    float arrowAreaWidth = 24.0f;     // Faixa da seta, a direita do botao; fora dela fica o valor.
    float chevronHalfWidth = 5.0f;
    float chevronHalfHeight = 3.0f;
    float chevronThickness = 2.0f;
    float focusRingWidth = 2.0f;
    float popupBorderWidth = 1.0f;
    float hoverAmount = 0.10f;
};

enum class DropdownKey : std::uint8_t { Up, Down, Enter, Escape };

struct DropdownEvents {
    bool opened = false;
    bool closed = false;
    bool valueChanged = false;
};

class Dropdown final : public Element {
  public:
    Widgets::DropdownControl control;
    float rowHeight = 24.0f;
    float popupScrollY = 0.0f;

    // Click no botao abre ou fecha. Janela e a area onde a lista pode aparecer (coordenadas do canvas).
    [[nodiscard]] DropdownEvents OnPointer(const PointerEvent& event, Rect window) noexcept;

    // Click numa linha da lista aberta (ponto nas coordenadas do canvas): escolhe a opcao e fecha.
    [[nodiscard]] DropdownEvents ChooseAt(Vec2 point, Rect window) noexcept;

    // Ponteiro fora do botao e da lista: fecha sem escolher.
    [[nodiscard]] DropdownEvents CloseOutside() noexcept;

    // Tecla com foco. Ver a regra no topo do arquivo.
    [[nodiscard]] DropdownEvents KeyPressed(DropdownKey key, Rect window) noexcept;

    // Recalcula a lista aberta para a janela dada (a janela pode ter mudado de tamanho). Fechada: nada.
    void Layout(Rect window) noexcept;

    // Retangulo da lista aberta no canvas. Vazio quando fechada.
    [[nodiscard]] Rect Popup() const noexcept { return popup_; }

  private:
    void OpenAt(Rect window) noexcept;
    void EnsureHighlightVisible() noexcept;

    PressGesture gesture_;
    Rect popup_{};
};

// Lista aberta para a janela: abaixo do botao se couber, senao acima; largura do botao; altura do conteudo ou do
// espaco disponivel (o menor); horizontalmente dentro da janela.
[[nodiscard]] Rect DropdownPopupRect(const Dropdown& dropdown, Rect window) noexcept;

// Retangulo da opcao i na tela, ja com a rolagem da lista. Fora da lista aberta, retorna o retangulo mesmo assim.
[[nodiscard]] Rect DropdownRowRect(const Dropdown& dropdown, std::size_t index) noexcept;

// Area do valor atual dentro do botao: o botao menos a faixa da seta.
[[nodiscard]] Rect DropdownValueRect(const Dropdown& dropdown, const DropdownStyle& style) noexcept;

// Itens do botao: fundo (conforme o estado), chevron (virado para cima quando aberto) e contorno de foco.
[[nodiscard]] std::vector<Rendering::UIRenderItem> BuildDropdownButtonItems(const Dropdown& dropdown,
                                                                            const DropdownStyle& style,
                                                                            float zOrder, std::uint64_t paintOrder);

// Itens da lista aberta: fundo, faixas do destaque e da escolhida (so as linhas visiveis) e contorno.
// clipIndex recorta as linhas na area da lista. Fechada: nenhum item.
[[nodiscard]] std::vector<Rendering::UIRenderItem> BuildDropdownPopupItems(const Dropdown& dropdown,
                                                                           const DropdownStyle& style,
                                                                           std::uint32_t clipIndex, float zOrder,
                                                                           std::uint64_t paintOrder);

} // namespace ToolEngine::SpriteUi::Elements
