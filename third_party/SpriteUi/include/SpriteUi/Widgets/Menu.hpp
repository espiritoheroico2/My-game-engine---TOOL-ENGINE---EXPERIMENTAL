#pragma once

// Menus como modelos puros: menu de contexto com submenus em cascata e barra de menus. Nao desenha e nao
// conhece teclado ou mouse de plataforma: o chamador traduz eventos em MenuKey e cliques (nivel, indice).
//
// Regras:
//  - Itens separadores e itens desabilitados nunca recebem destaque nem disparam acao.
//  - Um submenu sem filhos tambem nao e selecionavel.
//  - Setas Up/Down andam pelos itens selecionaveis e dao a volta nas pontas.
//  - Right abre o submenu destacado; Left e Escape fecham so o nivel mais fundo.
//  - Escape no nivel raiz fecha o menu (MenuEvent::Dismissed). Enter ou clique em acao fecha e informa o
//    caminho escolhido (ActivationPath).
//  - Clique em um nivel mais raso fecha os niveis mais fundos que ficaram abaixo dele.
//  - Shortcut e so texto exibido ao lado do rotulo; o modelo nao o interpreta.
//  - Menu fechado ignora teclas e movimentos (retornam None / false).

#include "SpriteUi/Widgets/Controls.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ToolEngine::SpriteUi::Widgets {

enum class MenuItemKind : std::uint8_t { Action, Submenu, Separator };

struct MenuItem {
    std::string label;
    std::string shortcut;           // Ex.: "Ctrl+S". Exibido pelo chamador, nao interpretado aqui.
    MenuItemKind kind = MenuItemKind::Action;
    bool disabled = false;
    std::vector<MenuItem> children; // Usado apenas por MenuItemKind::Submenu.
};

enum class MenuKey : std::uint8_t { Up, Down, Left, Right, Home, End, Enter, Escape };

enum class MenuEvent : std::uint8_t {
    None,      // Nada mudou.
    Moved,     // Destaque ou nivel aberto mudou; o menu continua aberto.
    Activated, // Acao escolhida; o menu fechou. Ver ActivationPath().
    Dismissed  // Escape no nivel raiz; o menu fechou sem escolha.
};

// Menu em cascata. Os niveis abertos formam um caminho: o nivel 0 e a lista raiz, cada nivel seguinte e o
// submenu aberto no nivel anterior.
class MenuControl {
public:
    // Abre o menu com os itens raiz e destaca o primeiro item selecionavel.
    void Open(const std::vector<MenuItem>& items);
    void Close() noexcept;

    [[nodiscard]] bool IsOpen() const noexcept { return open_; }
    // Quantidade de niveis abertos (0 se fechado).
    [[nodiscard]] std::size_t Depth() const noexcept { return open_ ? path_.size() + 1 : 0; }
    // Itens do nivel (0 = raiz). Valido para depth < Depth().
    [[nodiscard]] const std::vector<MenuItem>& Items(std::size_t depth) const noexcept;
    // Indice destacado no nivel, ou NoControlIndex.
    [[nodiscard]] std::size_t Highlighted(std::size_t depth) const noexcept;
    // Indice do submenu aberto a partir do nivel, ou NoControlIndex.
    [[nodiscard]] std::size_t OpenSubmenu(std::size_t depth) const noexcept;

    // Teclado. Ver a regra no topo do arquivo.
    [[nodiscard]] MenuEvent Key(MenuKey key) noexcept;

    // Clique em um item de um nivel. Retorna Moved, Activated ou None.
    [[nodiscard]] MenuEvent Click(std::size_t depth, std::size_t index) noexcept;

    // Ponteiro sobre um item do nivel mais fundo: so destaca. Retorna true se o destaque mudou.
    [[nodiscard]] bool Hover(std::size_t index) noexcept;

    // Caminho de indices da ultima acao escolhida (um indice por nivel).
    [[nodiscard]] const std::vector<std::size_t>& ActivationPath() const noexcept { return activation_; }

private:
    [[nodiscard]] const std::vector<MenuItem>& Current() const noexcept { return Items(path_.size()); }
    [[nodiscard]] MenuEvent MoveHighlight(int step) noexcept;
    [[nodiscard]] MenuEvent SetHighlight(std::size_t index) noexcept;
    [[nodiscard]] MenuEvent OpenHighlightedSubmenu() noexcept;
    [[nodiscard]] MenuEvent ActivateHighlighted() noexcept;
    void PushSubmenu(std::size_t index) noexcept;
    [[nodiscard]] MenuEvent PopSubmenu() noexcept;
    [[nodiscard]] MenuEvent Activate(std::size_t depth, std::size_t index) noexcept;

    std::vector<MenuItem> root_;
    std::vector<std::size_t> path_;       // Submenu aberto em cada nivel (indice no nivel anterior).
    std::vector<std::size_t> highlights_; // Um destaque por nivel aberto.
    std::vector<std::size_t> activation_;
    bool open_ = false;
};

struct MenuBarEntry {
    std::string label;
    std::vector<MenuItem> items;
    bool disabled = false;
};

// Barra de menus: uma fileira de cabecalhos, cada um com um menu. Com um menu aberto, passar o ponteiro sobre
// outro cabecalho troca o menu aberto. Left e Right (sem submenu aberto) trocam de menu e dao a volta.
class MenuBarControl {
public:
    void SetMenus(std::vector<MenuBarEntry> menus) noexcept; // Fecha o menu aberto.

    [[nodiscard]] const std::vector<MenuBarEntry>& Menus() const noexcept { return menus_; }
    [[nodiscard]] bool IsOpen() const noexcept { return dropdown_.IsOpen(); }
    [[nodiscard]] std::size_t ActiveMenu() const noexcept { return active_; } // NoControlIndex se fechado.
    [[nodiscard]] const MenuControl& Dropdown() const noexcept { return dropdown_; }
    [[nodiscard]] std::size_t ActivatedMenu() const noexcept { return activatedMenu_; }
    [[nodiscard]] const std::vector<std::size_t>& ActivationPath() const noexcept {
        return dropdown_.ActivationPath();
    }

    // Abre o menu i (nao abre se desabilitado ou inexistente). Retorna true se o menu aberto mudou.
    [[nodiscard]] bool OpenMenu(std::size_t index) noexcept;
    // Clique no cabecalho: abre, ou fecha se ele ja for o menu aberto.
    [[nodiscard]] bool ClickHeader(std::size_t index) noexcept;
    // Ponteiro sobre um cabecalho: troca o menu aberto. Com a barra fechada, nao faz nada.
    [[nodiscard]] bool HoverHeader(std::size_t index) noexcept;
    void Close() noexcept;

    [[nodiscard]] MenuEvent Key(MenuKey key) noexcept;
    [[nodiscard]] MenuEvent Click(std::size_t depth, std::size_t index) noexcept;
    [[nodiscard]] bool HoverItem(std::size_t index) noexcept { return dropdown_.Hover(index); }

private:
    [[nodiscard]] MenuEvent Finish(MenuEvent event) noexcept;
    [[nodiscard]] MenuEvent SwitchMenu(int step) noexcept;

    std::vector<MenuBarEntry> menus_;
    MenuControl dropdown_;
    std::size_t active_ = NoControlIndex;
    std::size_t activatedMenu_ = NoControlIndex;
};

} // namespace ToolEngine::SpriteUi::Widgets
