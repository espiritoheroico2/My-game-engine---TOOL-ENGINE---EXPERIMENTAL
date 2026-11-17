#pragma once

// Barra de menus (MenuBar): uma faixa horizontal de cabecalhos. Cada cabecalho abre um menu em popup; um item
// com submenu abre outro popup em cascata, ao lado.
//
// Partes: a faixa (fundo), os cabecalhos (o do menu aberto e destacado) e, para cada nivel aberto, um popup com
// fundo, linhas, faixa do destaque, separadores e contorno. O chamador desenha os textos: o rotulo em RowRect()
// e o atalho alinhado a direita da mesma linha. O estado (menus, destaques e niveis) vem do modelo MenuBarControl
// (Widgets/Menu.hpp). Este elemento acrescenta a geometria, o mouse, o Alt e o atraso de submenu.
//
// Sem limite: a faixa rola na horizontal quando os cabecalhos nao cabem, e cada popup rola na vertical quando os
// itens nao cabem na janela. Nao ha numero maximo de menus, itens ou niveis.
//
// Eventos:
//  - Clique num cabecalho abre o menu; de novo, fecha. Com um menu aberto, passar sobre outro cabecalho troca.
//  - Clique num item executa a acao ou abre o submenu. Clique fora de tudo fecha a barra.
//  - Alt (PressAlt) ativa a barra. Com a barra ativa e nada aberto, Esquerda e Direita movem o cabecalho; Enter
//    ou seta para baixo abre o menu; Escape desativa.
//  - Com menu aberto, as teclas vao para o modelo: setas navegam, Direita abre submenu, Esquerda fecha um nivel,
//    Enter ativa e Escape fecha um nivel de cada vez.
//  - Submenu abre sozinho depois de submenuDelay (0,2 s) com o item destacado. Tick(dt) conta o tempo, dado
//    pelo host; o elemento nao le o relogio.
//  - Atalho: FindShortcut(texto) acha a acao com esse atalho. Vale com o menu fechado.
//
// Exemplo:
//   auto id = tree.Create<MenuBar>(root);
//   auto* bar = tree.GetAs<MenuBar>(id);
//   bar->control.SetMenus({{"Arquivo", {{"Salvar", "Ctrl+S"}}}});
//   auto events = bar->PressAlt();                           // Barra ativa.
//   events = bar->OnPointer(pointer, window);                // Clique abre um menu.

#include "SpriteUi/Core/Math.hpp"
#include "SpriteUi/Elements/Element.hpp"
#include "SpriteUi/Elements/PointerEvent.hpp"
#include "SpriteUi/Elements/PressGesture.hpp"
#include "SpriteUi/Rendering/DrawList.hpp"
#include "SpriteUi/Widgets/Menu.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace ToolEngine::SpriteUi::Elements {

struct MenuBarStyle {
    float cornerRadius = 0.0f; // Raio dos cantos do popup e do destaque das linhas; zero = retos.
    Vec4 bar{0.12f, 0.12f, 0.15f, 1.0f};
    Vec4 header{0.12f, 0.12f, 0.15f, 1.0f};
    Vec4 headerHover{0.17f, 0.18f, 0.23f, 1.0f}; // Cabecalho com o ponteiro em cima (menu fechado ou outro aberto).
    Vec4 headerOpen{0.2f, 0.22f, 0.28f, 1.0f};
    Vec4 check{0.9f, 0.9f, 0.95f, 1.0f};         // Marca de uma caixa de selecao ligada.
    float checkSize = 10.0f;                     // Lado da marca (o quadro da caixa tem a mesma folga).
    Vec4 popup{0.1f, 0.1f, 0.12f, 1.0f};
    Vec4 popupBorder{0.3f, 0.32f, 0.38f, 1.0f};
    Vec4 highlight{0.05f, 0.2f, 0.55f, 1.0f};
    Vec4 separator{0.25f, 0.27f, 0.32f, 1.0f};
    float borderWidth = 1.0f;
    float separatorThickness = 1.0f;
};

struct MenuBarEvents {
    bool changed = false;    // Menu aberto, destaque ou nivel mudou.
    bool activated = false;  // Uma acao foi escolhida; ver ActivationPath() do controle.
    bool closed = false;     // A barra fechou por completo.
};

// Acao encontrada por atalho: menu de origem e caminho de indices (um por nivel).
struct MenuShortcut {
    std::size_t menu = 0;
    std::vector<std::size_t> path;
};

class MenuBar final : public Element {
  public:
    Widgets::MenuBarControl control;
    float headerWidth = 80.0f;      // Largura de cada cabecalho.
    float rowHeight = 24.0f;        // Altura de cada linha de item.
    float separatorHeight = 7.0f;   // Altura de um separador.
    float popupWidth = 200.0f;      // Largura de cada popup.
    float submenuDelay = 0.2f;      // Segundos com o item de submenu destacado antes de abrir.
    float headerScrollX = 0.0f;     // Rolagem horizontal dos cabecalhos.

    // Ponteiro: clique em cabecalho, item ou fora; passar sobre cabecalho ou item troca o destaque. Window e a
    // area onde os popups podem aparecer (coordenadas do canvas).
    [[nodiscard]] MenuBarEvents OnPointer(const PointerEvent& event, Rect window) noexcept;

    // Tecla. Com menu aberto vai para o modelo; com a barra ativa, move o cabecalho ou abre o menu.
    [[nodiscard]] MenuBarEvents KeyPressed(Widgets::MenuKey key, Rect window) noexcept;

    // Alt: ativa a barra (sem menu aberto) ou fecha o menu aberto.
    [[nodiscard]] MenuBarEvents PressAlt() noexcept;

    // Passa dt segundos. Abre o submenu destacado quando ele ficou destacado por submenuDelay.
    [[nodiscard]] MenuBarEvents Tick(float seconds, Rect window) noexcept;

    // Acao com esse atalho (texto exato), em qualquer menu, mesmo com a barra fechada. Desabilitadas nao contam.
    [[nodiscard]] std::optional<MenuShortcut> FindShortcut(std::string_view shortcut) const;

    // Retangulo do cabecalho i no canvas, ja com a rolagem da faixa.
    [[nodiscard]] Rect HeaderRect(std::size_t index) const noexcept;

    // Popup do nivel depth no canvas. Vazio quando o nivel nao esta aberto. Chame Layout() se a janela mudar.
    [[nodiscard]] Rect PopupRect(std::size_t depth) const noexcept;

    // Cabecalho com o ponteiro em cima (NoControlIndex se nenhum). Some quando o ponteiro sai da barra.
    [[nodiscard]] std::size_t HoveredHeader() const noexcept { return hoveredHeader_; }

    // Recuo a partir da borda esquerda da linha do popup onde o icone e a caixa de selecao ficam. O rotulo do item
    // comeca em RowRect().x + ContentInsetX(), para que a coluna de icone nunca seja coberta pelo texto.
    [[nodiscard]] static float ContentInsetX() noexcept { return kContentInsetX; }
    // Quadro do icone do item (lado kIconSize, centrado na coluna de icone da linha). Vazio se a linha nao existe.
    // O icone em si e desenhado pelo host: o modelo guarda so a chave (MenuItem::icon).
    [[nodiscard]] Rect IconRect(std::size_t depth, std::size_t index) const noexcept;
    // Quadro do atalho: alinhado a direita da linha, com a mesma folga da coluna de icone. O texto e do host.
    [[nodiscard]] Rect ShortcutRect(std::size_t depth, std::size_t index) const noexcept;
    static constexpr float kContentInsetX = 24.0f;
    static constexpr float kIconSize = 14.0f;
    static constexpr float kShortcutColumnWidth = 96.0f;

    // Linha do item index no nivel depth, ja com a rolagem do popup. Vazio se o nivel nao esta aberto.
    [[nodiscard]] Rect RowRect(std::size_t depth, std::size_t index) const noexcept;

    // Recalcula os popups abertos para a janela dada.
    void Layout(Rect window) noexcept;

  private:
    struct RowHit {
        std::size_t depth;
        std::size_t index;
    };

    [[nodiscard]] std::size_t HeaderAt(Vec2 point) const noexcept;
    [[nodiscard]] std::optional<RowHit> RowAt(Vec2 point) const noexcept;
    void Refresh(Rect window) noexcept;
    void EnsureHeaderVisible(std::size_t index) noexcept;
    void EnsureDeepestRowVisible() noexcept;

    [[nodiscard]] float ScrollOf(std::size_t depth) const noexcept;

    PressGesture gesture_;
    std::vector<Rect> popups_;
    std::vector<float> popupScroll_;
    std::size_t lastActive_ = Widgets::NoControlIndex;
    std::size_t headerCursor_ = 0;
    std::size_t hoveredHeader_ = Widgets::NoControlIndex;
    bool barActive_ = false;
    float hoverTime_ = 0.0f;
};

// Altura da faixa da barra de menus para uma escala de interface: 28 px a 100 por cento, arredondada para pixel
// fisico (1.25 -> 35 px). O host usa este valor como altura da linha da barra no layout.
[[nodiscard]] float MenuBarHeightFor(float uiScale) noexcept;

// Itens da faixa, dos cabecalhos e dos popups abertos. clipIndex recorta a faixa e os cabecalhos. Os popups
// nao sao recortados pela faixa, pois descem sobre a interface; ficam dentro da janela pelo Layout().
[[nodiscard]] std::vector<Rendering::UIRenderItem> BuildMenuBarItems(const MenuBar& bar, const MenuBarStyle& style,
                                                                     std::uint32_t clipIndex, float zOrder,
                                                                     std::uint64_t paintOrder);

} // namespace ToolEngine::SpriteUi::Elements
