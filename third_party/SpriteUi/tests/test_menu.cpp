// Menus (Menu.hpp): destaque, submenus em cascata, atalhos exibidos, separadores e barra de menus. Sem janela.
#include "SpriteUi/Widgets/Menu.hpp"

#include <cstdio>
#include <utility>
#include <vector>

namespace {

using namespace ToolEngine::SpriteUi::Widgets;

int run = 0;
int failed = 0;

#define CHECK(expr)                                                                                          \
    do {                                                                                                     \
        ++run;                                                                                               \
        if (!(expr)) {                                                                                       \
            ++failed;                                                                                        \
            std::fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #expr);                           \
        }                                                                                                    \
    } while (0)

MenuItem Action(const char* label, const char* shortcut = "") {
    MenuItem item;
    item.label = label;
    item.shortcut = shortcut;
    return item;
}

MenuItem Separator() {
    MenuItem item;
    item.kind = MenuItemKind::Separator;
    return item;
}

MenuItem Submenu(const char* label, std::vector<MenuItem> children) {
    MenuItem item;
    item.label = label;
    item.kind = MenuItemKind::Submenu;
    item.children = std::move(children);
    return item;
}

MenuItem Disabled(MenuItem item) {
    item.disabled = true;
    return item;
}

// Raiz: [0] Desabilitado (pulado), [1] Novo, [2] separador, [3] Salvar, [4] Exportar (submenu).
std::vector<MenuItem> RootItems() {
    return {
        Disabled(Action("Desfazer", "Ctrl+Z")),
        Action("Novo", "Ctrl+N"),
        Separator(),
        Action("Salvar", "Ctrl+S"),
        Submenu("Exportar", {Action("PNG"), Disabled(Action("SVG")), Action("PDF")}),
    };
}

void TestOpenHighlightsFirstSelectable() {
    MenuControl menu;
    menu.Open(RootItems());
    CHECK(menu.IsOpen());
    CHECK(menu.Depth() == 1);
    CHECK(menu.Highlighted(0) == 1); // pulou o desabilitado [0]
}

void TestArrowsSkipSeparatorsAndDisabledAndWrap() {
    MenuControl menu;
    menu.Open(RootItems());
    CHECK(menu.Key(MenuKey::Down) == MenuEvent::Moved);
    CHECK(menu.Highlighted(0) == 3); // pulou o separador [2]
    CHECK(menu.Key(MenuKey::Down) == MenuEvent::Moved);
    CHECK(menu.Highlighted(0) == 4);
    CHECK(menu.Key(MenuKey::Down) == MenuEvent::Moved);
    CHECK(menu.Highlighted(0) == 1); // volta ao topo, pulando [0]
    CHECK(menu.Key(MenuKey::Up) == MenuEvent::Moved);
    CHECK(menu.Highlighted(0) == 4); // volta pela ponta de baixo
}

void TestHomeEnd() {
    MenuControl menu;
    menu.Open(RootItems());
    CHECK(menu.Key(MenuKey::End) == MenuEvent::Moved);
    CHECK(menu.Highlighted(0) == 4);
    CHECK(menu.Key(MenuKey::End) == MenuEvent::None);
    CHECK(menu.Key(MenuKey::Home) == MenuEvent::Moved);
    CHECK(menu.Highlighted(0) == 1);
}

void TestEnterActivatesAndCloses() {
    MenuControl menu;
    menu.Open(RootItems());
    CHECK(menu.Key(MenuKey::Down) == MenuEvent::Moved);
    // Salvar: indice 3, logo apos o separador.
    CHECK(menu.Key(MenuKey::Enter) == MenuEvent::Activated);
    CHECK(!menu.IsOpen());
    CHECK(menu.ActivationPath() == std::vector<std::size_t>{3});
}

void TestClickDisabledKeepsMenuOpen() {
    MenuControl menu;
    menu.Open(RootItems());
    CHECK(menu.Click(0, 0) == MenuEvent::None); // desabilitado
    CHECK(menu.IsOpen());
    CHECK(menu.Click(0, 2) == MenuEvent::None); // separador
    CHECK(menu.IsOpen());
    CHECK(menu.Click(0, 3) == MenuEvent::Activated);
    CHECK(!menu.IsOpen());
}

void TestSubmenuOpensWithRightAndLeftCloses() {
    MenuControl menu;
    menu.Open(RootItems());
    CHECK(menu.Click(0, 4) == MenuEvent::Moved); // Exportar abre o submenu por clique
    CHECK(menu.Depth() == 2);
    CHECK(menu.OpenSubmenu(0) == 4);
    CHECK(menu.Highlighted(1) == 0); // primeiro item do submenu
    CHECK(menu.Items(1).size() == 3);
    CHECK(menu.Key(MenuKey::Down) == MenuEvent::Moved);
    CHECK(menu.Highlighted(1) == 2); // pulou o desabilitado SVG
    CHECK(menu.Key(MenuKey::Left) == MenuEvent::Moved);
    CHECK(menu.Depth() == 1);
    CHECK(menu.Highlighted(0) == 4); // destaque continua no item que abriu o submenu
    CHECK(menu.Key(MenuKey::Left) == MenuEvent::None); // ja na raiz
}

void TestRightOpensHighlightedSubmenuOnly() {
    MenuControl menu;
    menu.Open(RootItems());
    CHECK(menu.Key(MenuKey::Right) == MenuEvent::None); // Novo nao e submenu
    CHECK(menu.Key(MenuKey::End) == MenuEvent::Moved);
    CHECK(menu.Key(MenuKey::Right) == MenuEvent::Moved);
    CHECK(menu.Depth() == 2);
}

void TestEnterOnSubmenuOpensIt() {
    MenuControl menu;
    menu.Open(RootItems());
    CHECK(menu.Key(MenuKey::End) == MenuEvent::Moved);
    CHECK(menu.Key(MenuKey::Enter) == MenuEvent::Moved);
    CHECK(menu.Depth() == 2);
    CHECK(menu.Key(MenuKey::Enter) == MenuEvent::Activated);
    CHECK(menu.ActivationPath() == (std::vector<std::size_t>{4, 0}));
}

void TestEscapeClosesOneLevelAtATime() {
    MenuControl menu;
    menu.Open(RootItems());
    CHECK(menu.Click(0, 4) == MenuEvent::Moved);
    CHECK(menu.Key(MenuKey::Escape) == MenuEvent::Moved);
    CHECK(menu.IsOpen());
    CHECK(menu.Depth() == 1);
    CHECK(menu.Key(MenuKey::Escape) == MenuEvent::Dismissed);
    CHECK(!menu.IsOpen());
}

void TestClickShallowerLevelClosesDeeperOnes() {
    MenuControl menu;
    menu.Open(RootItems());
    CHECK(menu.Click(0, 4) == MenuEvent::Moved);
    CHECK(menu.Key(MenuKey::Down) == MenuEvent::Moved); // destaque no submenu
    CHECK(menu.Click(0, 1) == MenuEvent::Activated); // Novo, no nivel raiz: fecha o submenu e escolhe
    CHECK(menu.ActivationPath() == std::vector<std::size_t>{1});
}

void TestClickOpenSubmenuAgainIsNoOp() {
    MenuControl menu;
    menu.Open(RootItems());
    CHECK(menu.Click(0, 4) == MenuEvent::Moved);
    CHECK(menu.Click(0, 4) == MenuEvent::None);
    CHECK(menu.Depth() == 2);
}

void TestHoverOnlyHighlights() {
    MenuControl menu;
    menu.Open(RootItems());
    CHECK(menu.Hover(3));
    CHECK(menu.Highlighted(0) == 3);
    CHECK(!menu.Hover(2)); // separador
    CHECK(!menu.Hover(3)); // ja destacado
    CHECK(menu.Depth() == 1);
}

void TestShortcutKeptAndClosedMenuIgnoresInput() {
    MenuControl menu;
    CHECK(menu.Key(MenuKey::Down) == MenuEvent::None);
    CHECK(!menu.Hover(1));
    menu.Open(RootItems());
    CHECK(menu.Items(0)[3].shortcut == "Ctrl+S");
    CHECK(menu.Items(0)[2].kind == MenuItemKind::Separator);
    menu.Close();
    CHECK(menu.Click(0, 1) == MenuEvent::None);
}

// Barra: Arquivo (abre), Editar (desabilitado), Exibir (dois itens).
std::vector<MenuBarEntry> BarMenus() {
    MenuBarEntry file{"Arquivo", {Action("Novo", "Ctrl+N"), Action("Sair")}, false};
    MenuBarEntry edit{"Editar", {Action("Copiar")}, true};
    MenuBarEntry view{"Exibir", {Action("Zoom")}, false};
    return {file, edit, view};
}

void TestBarClickHeaderTogglesMenu() {
    MenuBarControl bar;
    bar.SetMenus(BarMenus());
    CHECK(bar.ClickHeader(0));
    CHECK(bar.IsOpen());
    CHECK(bar.ActiveMenu() == 0);
    CHECK(bar.ClickHeader(0)); // mesmo cabecalho fecha
    CHECK(!bar.IsOpen());
    CHECK(!bar.ClickHeader(1)); // desabilitado nao abre
    CHECK(!bar.IsOpen());
}

void TestBarHoverSwitchesOnlyWhenOpen() {
    MenuBarControl bar;
    bar.SetMenus(BarMenus());
    CHECK(!bar.HoverHeader(2)); // fechada: nada
    CHECK(bar.ClickHeader(0));
    CHECK(bar.HoverHeader(2));
    CHECK(bar.ActiveMenu() == 2);
    CHECK(!bar.HoverHeader(1)); // desabilitado
    CHECK(bar.ActiveMenu() == 2);
}

void TestBarLeftRightWrapsSkippingDisabled() {
    MenuBarControl bar;
    bar.SetMenus(BarMenus());
    CHECK(bar.ClickHeader(0));
    CHECK(bar.Key(MenuKey::Right) == MenuEvent::Moved);
    CHECK(bar.ActiveMenu() == 2); // pulou o desabilitado [1]
    CHECK(bar.Key(MenuKey::Right) == MenuEvent::Moved);
    CHECK(bar.ActiveMenu() == 0); // volta ao inicio
    CHECK(bar.Key(MenuKey::Left) == MenuEvent::Moved);
    CHECK(bar.ActiveMenu() == 2);
}

void TestBarLeftInsideSubmenuOnlyPops() {
    MenuBarControl bar;
    bar.SetMenus({MenuBarEntry{"Arquivo", {Submenu("Abrir", {Action("Recente")})}, false},
                  MenuBarEntry{"Exibir", {Action("Zoom")}, false}});
    CHECK(bar.ClickHeader(0));
    CHECK(bar.Click(0, 0) == MenuEvent::Moved); // abre o submenu Abrir
    CHECK(bar.Dropdown().Depth() == 2);
    CHECK(bar.Key(MenuKey::Left) == MenuEvent::Moved);
    CHECK(bar.ActiveMenu() == 0); // continua no mesmo menu
    CHECK(bar.Dropdown().Depth() == 1);
}

void TestBarEscapeAndActivationCloseBar() {
    MenuBarControl bar;
    bar.SetMenus(BarMenus());
    CHECK(bar.ClickHeader(0));
    CHECK(bar.Key(MenuKey::Escape) == MenuEvent::Dismissed);
    CHECK(!bar.IsOpen());
    CHECK(bar.ActiveMenu() == NoControlIndex);

    CHECK(bar.ClickHeader(2));
    CHECK(bar.Click(0, 0) == MenuEvent::Activated);
    CHECK(!bar.IsOpen());
    CHECK(bar.ActivatedMenu() == 2);
    CHECK(bar.ActivationPath() == std::vector<std::size_t>{0});
}

void TestBarClosedIgnoresKeys() {
    MenuBarControl bar;
    bar.SetMenus(BarMenus());
    CHECK(bar.Key(MenuKey::Right) == MenuEvent::None);
    CHECK(!bar.IsOpen());
}

} // namespace

int main() {
    TestOpenHighlightsFirstSelectable();
    TestArrowsSkipSeparatorsAndDisabledAndWrap();
    TestHomeEnd();
    TestEnterActivatesAndCloses();
    TestClickDisabledKeepsMenuOpen();
    TestSubmenuOpensWithRightAndLeftCloses();
    TestRightOpensHighlightedSubmenuOnly();
    TestEnterOnSubmenuOpensIt();
    TestEscapeClosesOneLevelAtATime();
    TestClickShallowerLevelClosesDeeperOnes();
    TestClickOpenSubmenuAgainIsNoOp();
    TestHoverOnlyHighlights();
    TestShortcutKeptAndClosedMenuIgnoresInput();
    TestBarClickHeaderTogglesMenu();
    TestBarHoverSwitchesOnlyWhenOpen();
    TestBarLeftRightWrapsSkippingDisabled();
    TestBarLeftInsideSubmenuOnlyPops();
    TestBarEscapeAndActivationCloseBar();
    TestBarClosedIgnoresKeys();

    std::printf("menu: %d checks, %d failed\n", run, failed);
    return failed == 0 ? 0 : 1;
}
