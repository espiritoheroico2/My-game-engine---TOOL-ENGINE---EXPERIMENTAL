#pragma once

// Controles de interface como modelos puros: estado mais operacoes explicitas. Nao desenha, nao abre janela
// e nao conhece entrada de plataforma. O chamador traduz eventos (clique, tecla, posicao do ponteiro) em
// chamadas destes metodos e desenha a partir do estado (ControlState e os campos publicos).
//
// Regras comuns a todos os controles:
//  - Controle desabilitado ignora a interacao do usuario: os metodos que respondem a eventos retornam false
//    e nao mudam o estado. Chamadas de programa (SetValue, Select) continuam valendo.
//  - Indice invalido (>= tamanho da lista) nunca muda o estado. NoControlIndex marca "nenhum item".
//  - Retorno bool: true quando o estado mudou. Serve para o chamador decidir se redesenha ou dispara evento.
//
// Reuso (nada aqui duplica o que ja existe):
//  - Slider: UISliderComponent (Widgets.hpp) ja guarda valor, faixa, passo e posicao do ponteiro.
//  SliderControl
//    o envolve e acrescenta so as teclas. UISliderComponent sozinho nao basta porque nao tem teclado.
//  - ListView: UIVirtualList (VirtualList.hpp) fornece altura de item, faixa visivel e desloca conteudo.
//    ListViewControl acrescenta selecao e rolagem minima para manter o item selecionado visivel.
//  - Button: UIButtonState (Widgets.hpp) ja tem hovered, pressed e disabled. Nao ganha modelo novo;
//    ResolveControlState traduz esse estado para a forma de desenho.

#include "SpriteUi/Core/Math.hpp"
#include "SpriteUi/Widgets/VirtualList.hpp"
#include "SpriteUi/Widgets/Widgets.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace ToolEngine::SpriteUi::Widgets {

inline constexpr std::size_t NoControlIndex = std::numeric_limits<std::size_t>::max();

// Forma de desenho de um controle. Prioridade: Disabled > Pressed > Hover > Normal.
enum class ControlState : std::uint8_t { Normal, Hover, Pressed, Disabled };

[[nodiscard]] ControlState ResolveControlState(const UIButtonState& button) noexcept;

// Caixa de selecao. Clique = Toggle().
struct CheckboxControl {
    bool checked = false;
    UIButtonState button{}; // hovered/pressed/disabled vem do chamador a cada quadro.

    // Alterna o valor se habilitado. Retorna true se mudou.
    [[nodiscard]] bool Toggle() noexcept;

    [[nodiscard]] ControlState State() const noexcept { return ResolveControlState(button); }
};

enum class SliderKey : std::uint8_t { Left, Right, Down, Up, Home, End };

// Controle deslizante. Teclas: Setas (Left/Down reduzem, Right/Up aumentam) andam um passo; Home/End vao
// aos extremos da faixa.
// Passo: se slider.step > 0, as setas andam esse passo. Se step == 0 (continuo), as setas andam
// kContinuousKeyFraction da faixa (1 por cento), para que o teclado funcione tambem sem passo definido.
struct SliderControl {
    static constexpr float kContinuousKeyFraction = 0.01f;

    UISliderComponent slider{};
    bool disabled = false;

    // Posicao do ponteiro dentro da trilha (x local, largura da trilha). Retorna true se o valor mudou.
    [[nodiscard]] bool SetFromPointer(float localX, float trackWidth) noexcept;

    // Tecla. Retorna true se o valor mudou (no limite, uma seta nao muda nada).
    [[nodiscard]] bool Key(SliderKey key) noexcept;

    // Valor de programa (ignora disabled). Retorna true se mudou.
    [[nodiscard]] bool SetValue(float value) noexcept;

    [[nodiscard]] ControlState State() const noexcept {
        return disabled ? ControlState::Disabled : ControlState::Normal;
    }

  private:
    [[nodiscard]] float KeyStep() const noexcept;
};

// Lista suspensa (combo). Estados: fechada (mostra a selecao) e aberta (mostra as opcoes e um destaque).
// Escolhas:
//  - Setas com a lista fechada mudam a selecao; com a lista aberta movem o destaque. Em ambos os casos o
//    movimento para nas pontas (sem volta circular). Sem nenhuma selecao, a primeira seta leva ao item 0.
//  - Confirm() (Enter) escolhe o destaque e fecha. Close() (Escape) fecha sem mudar a selecao.
//  - Select(i) escolhe um indice valido e fecha; indice invalido nao muda nada, nem o estado aberto.
struct DropdownControl {
    std::vector<std::string> options;
    std::size_t selectedIndex = NoControlIndex;
    std::size_t highlightedIndex = NoControlIndex; // Valido so com a lista aberta.
    bool open = false;
    bool disabled = false;

    // Abre a lista (nao abre se desabilitada ou sem opcoes) e destaca a selecao atual. Retorna true se abriu.
    [[nodiscard]] bool Open() noexcept;

    // Fecha sem mudar a selecao. Retorna true se estava aberta.
    bool Close() noexcept;

    // Escolhe o indice e fecha. Retorna true se a selecao mudou.
    [[nodiscard]] bool Select(std::size_t index) noexcept;

    // Seta para baixo / cima. Ver a regra no topo da struct.
    [[nodiscard]] bool MoveDown() noexcept;
    [[nodiscard]] bool MoveUp() noexcept;

    // Enter: escolhe o destaque se a lista estiver aberta. Retorna true se a selecao mudou.
    [[nodiscard]] bool Confirm() noexcept;

    [[nodiscard]] ControlState State() const noexcept {
        return disabled ? ControlState::Disabled : ControlState::Normal;
    }

  private:
    [[nodiscard]] bool Move(int delta) noexcept;
};

// Abas. Ordem = ordem de entrada dos rotulos; nada reordena. A aba ativa comeca em 0 se houver rotulos.
struct TabBarControl {
    std::vector<std::string> labels;
    std::size_t activeIndex = NoControlIndex;
    bool disabled = false;

    TabBarControl() = default;
    explicit TabBarControl(std::vector<std::string> tabLabels);

    // Ativa a aba i. Retorna true se a aba ativa mudou. Indice invalido ou aba ja ativa: false.
    [[nodiscard]] bool Select(std::size_t index) noexcept;

    [[nodiscard]] ControlState State() const noexcept {
        return disabled ? ControlState::Disabled : ControlState::Normal;
    }
};

enum class ListKey : std::uint8_t { Up, Down, Home, End };

using ListItemId = std::uint64_t;
// Valor reservado para "nenhum item". Um item nunca usa este identificador.
inline constexpr ListItemId NoListItem = std::numeric_limits<ListItemId>::max();

// Lista de selecao simples (uma por vez). A selecao guarda o identificador do item, nao a posicao: inserir ou
// remover itens nao muda o item escolhido. Identificadores devem ser unicos. Altura de linha vem de UIVirtualList.
// Rolagem: EnsureVisible() faz o minimo de rolagem para o item selecionado caber na area visivel. Se o item
// ja esta visivel, a rolagem nao muda. Com area visivel de altura zero ou menos, nao rola.
struct ListViewControl {
    UIVirtualList items{};
    ListItemId selectedId = NoListItem;
    float scrollOffsetY = 0.0f;
    float viewportHeight = 0.0f;
    bool disabled = false;

    // Lista com identificadores 0..count-1 (posicao = identificador).
    void SetItemCount(std::size_t count) noexcept;
    // Troca os itens. A selecao continua no mesmo item se ele ainda existe; senao e limpa.
    void SetItemIds(std::vector<ListItemId> ids) noexcept;
    void SetItemHeight(float height) noexcept;
    void SetViewportHeight(float height) noexcept;

    // Posicao do item selecionado, ou NoControlIndex.
    [[nodiscard]] std::size_t SelectedIndex() const noexcept;

    [[nodiscard]] float MaxScrollY() const noexcept;
    void ClampScroll() noexcept;

    // Seleciona o item na posicao index e garante que ele esta visivel. Retorna true se a selecao mudou.
    [[nodiscard]] bool Select(std::size_t index) noexcept;
    // Seleciona pelo identificador. Id desconhecido nao muda nada. Retorna true se a selecao mudou.
    [[nodiscard]] bool SelectId(ListItemId id) noexcept;

    // Setas e Home/End. Sem selecao, Up/Down levam ao item 0. Retorna true se a selecao mudou.
    [[nodiscard]] bool Key(ListKey key) noexcept;

    // Faixa de itens visiveis (com a sobra de overscan do UIVirtualList).
    [[nodiscard]] VirtualListRange VisibleRange() const noexcept;

    [[nodiscard]] ControlState State() const noexcept {
        return disabled ? ControlState::Disabled : ControlState::Normal;
    }

  private:
    [[nodiscard]] std::size_t IndexOf(ListItemId id) const noexcept;
    void EnsureVisible() noexcept;

    std::vector<ListItemId> ids_;
};

} // namespace ToolEngine::SpriteUi::Widgets
