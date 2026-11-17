#pragma once

// Lista (ListBox): uma pilha vertical de linhas dentro de um contêiner que rola. A lista nao tem numero maximo
// de itens: SetItems e AddItem aceitam quantos forem dados.
//
// Partes: o fundo (com uma grade de linhas separadoras), as linhas visiveis (so elas sao desenhadas), a
// faixa de selecao em cada linha escolhida, o contorno de foco na linha do cursor e a barra de rolagem na
// borda direita quando o conteudo e maior que a area.
//
// Selecao: um conjunto de identificadores de item, nao de posicoes. Inserir um item antes de outro nao muda o
// item escolhido. Itens removidos saem da selecao. O cursor (linha do teclado) e um item a parte.
//   - Click: escolhe so aquele item.
//   - Ctrl+Click: alterna o item na selecao.
//   - Shift+Click: seleciona exatamente o intervalo entre a ancora e o clique (com Ctrl, soma ao que ja existe).
//   - Setas, Home, End, PageUp e PageDown movem o cursor; sem Shift, a selecao vira o item do cursor; com Shift,
//     a selecao vira o intervalo entre a ancora e o cursor.
//
// Altura: PreferredHeight(limite) cresce com a lista ate o limite que o chamador der; acima disso, rola.
// Texto: o rotulo de cada linha e desenhado pelo chamador na faixa RowRect(). Chame UpdateViewport() depois de
// mudar o layout, antes de desenhar ou tratar teclas (a area visivel muda com o tamanho do elemento).
//
// Exemplo:
//   auto id = tree.Create<ListBox>(parent);
//   auto* list = tree.GetAs<ListBox>(id);
//   list->SetItems({1, 2, 3});
//   list->Click(2, {});                 // Selecao: so o item 2.
//   list->Key(ListBoxKey::Down, {false, true}); // Shift+seta: intervalo de 2 ate 3.

#include "SpriteUi/Core/Math.hpp"
#include "SpriteUi/Elements/Element.hpp"
#include "SpriteUi/Elements/PointerEvent.hpp"
#include "SpriteUi/Elements/PressGesture.hpp"
#include "SpriteUi/Rendering/DrawList.hpp"
#include "SpriteUi/Widgets/Controls.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ToolEngine::SpriteUi::Elements {

using ListBoxItemId = Widgets::ListItemId;

struct ListBoxStyle {
    Vec4 background{0.1f, 0.1f, 0.12f, 1.0f};
    Vec4 gridLine{0.18f, 0.18f, 0.2f, 1.0f};
    Vec4 selected{0.05f, 0.2f, 0.55f, 1.0f};
    Vec4 scrollTrack{0.15f, 0.15f, 0.17f, 1.0f};
    Vec4 scrollThumb{0.45f, 0.48f, 0.55f, 1.0f};
    Vec4 focusRing{0.95f, 0.8f, 0.3f, 1.0f};
    float gridWidth = 1.0f;
    float focusRingWidth = 2.0f;
    float scrollWidth = 6.0f;
    float minThumbHeight = 16.0f;
};

struct ListModifiers {
    bool ctrl = false;
    bool shift = false;
};

enum class ListBoxKey : std::uint8_t { Up, Down, Home, End, PageUp, PageDown };

struct ListBoxEvents {
    bool pressStarted = false;
    bool pressEnded = false;
    bool clicked = false;
    bool selectionChanged = false;
};

class ListBox final : public Element {
  public:
    static constexpr float kDefaultRowHeight = 24.0f;

    // Controle de rolagem, cursor e altura: a mesma base dos controles de lista (Controls.hpp).
    Widgets::ListViewControl view;

    ListBox();

    // Troca os itens. Identificadores devem ser unicos. A selecao e o cursor continuam nos itens que existem.
    void SetItems(std::vector<ListBoxItemId> ids) noexcept;

    // Adiciona um item no fim. Identificador repetido ou reservado: false.
    bool AddItem(ListBoxItemId id);

    [[nodiscard]] std::size_t ItemCount() const noexcept { return ids_.size(); }

    // Identificador da linha i, ou NoListItem fora da lista.
    [[nodiscard]] ListBoxItemId ItemAt(std::size_t index) const noexcept;

    [[nodiscard]] float RowHeight() const noexcept { return view.items.GetItemHeight(); }
    [[nodiscard]] bool IsSelected(ListBoxItemId id) const noexcept;
    [[nodiscard]] const std::vector<ListBoxItemId>& Selection() const noexcept { return selected_; }

    // Area visivel = altura da area util. Chame depois de mudar o layout.
    void UpdateViewport() noexcept;

    // Altura pedida: o conteudo inteiro, limitado pelo maximo dado (nunca negativa).
    [[nodiscard]] float PreferredHeight(float maxHeight) const noexcept;

    // Ponteiro: Click numa linha aplica a regra de selecao com os modificadores de teclado do momento.
    [[nodiscard]] ListBoxEvents OnPointer(const PointerEvent& event, ListModifiers modifiers) noexcept;

    // Clique em um item (ja sabendo qual e). Retorna true se a selecao mudou.
    [[nodiscard]] bool Click(ListBoxItemId id, ListModifiers modifiers) noexcept;

    // Tecla de navegacao. Retorna true se a selecao mudou.
    [[nodiscard]] bool Key(ListBoxKey key, ListModifiers modifiers) noexcept;

    // Linha sob o ponto (coordenadas do elemento), ou NoControlIndex.
    [[nodiscard]] std::size_t RowAt(Vec2 point) const noexcept;

    // Retangulo da linha i na tela do elemento, ja com a rolagem. Fora da area visivel, ainda assim retorna o
    // retangulo (o chamador descarta o que nao aparece).
    [[nodiscard]] Rect RowRect(std::size_t index) const noexcept;

  private:
    [[nodiscard]] std::size_t IndexOf(ListBoxItemId id) const noexcept;
    [[nodiscard]] std::size_t TargetIndex(ListBoxKey key, std::size_t current) const noexcept;
    [[nodiscard]] std::size_t PageRows() const noexcept;
    void AddRange(std::vector<ListBoxItemId>& out, std::size_t from, std::size_t to) const;
    [[nodiscard]] bool ApplySelection(std::vector<ListBoxItemId> next, std::size_t cursor) noexcept;

    std::vector<ListBoxItemId> ids_;
    std::vector<ListBoxItemId> selected_;
    ListBoxItemId anchor_ = Widgets::NoListItem;
    PressGesture gesture_;
};

// Itens de desenho: fundo, grade, selecao, cursor e barra de rolagem. So as linhas visiveis entram.
// clipIndex recorta as linhas parcialmente visiveis na area do elemento (o chamador cria o recorte).
[[nodiscard]] std::vector<Rendering::UIRenderItem> BuildListBoxItems(const ListBox& list, const ListBoxStyle& style,
                                                                     std::uint32_t clipIndex, float zOrder,
                                                                     std::uint64_t paintOrder);

} // namespace ToolEngine::SpriteUi::Elements
