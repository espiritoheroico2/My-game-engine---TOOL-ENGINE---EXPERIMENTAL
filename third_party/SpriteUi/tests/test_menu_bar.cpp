// Barra de menus (MenuBar): clique em cabecalho e item, troca por passagem, cascata com inversao a esquerda,
// Escape nivel a nivel, teclado, Alt, atraso de submenu, atalhos, rolagem e itens de desenho. Valores calculados
// a mao nos comentarios. Janela de 400 x 300; barra de 26 px; cabecalhos de 80; linhas de 24; separador de 7.
#include "SpriteUi/Elements/MenuBar.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace ToolEngine::SpriteUi;
using namespace ToolEngine::SpriteUi::Elements;
using Widgets::MenuBarEntry;
using Widgets::MenuItem;
using Widgets::MenuItemKind;
using Widgets::MenuKey;
using Widgets::NoControlIndex;

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

bool Near(float a, float b) {
    return std::fabs(a - b) <= 1.0e-4f;
}

bool SameRect(Rect a, Rect b) {
    return Near(a.position.x, b.position.x) && Near(a.position.y, b.position.y) && Near(a.size.x, b.size.x) &&
           Near(a.size.y, b.size.y);
}

const Rect kWindow{{0.0f, 0.0f}, {400.0f, 300.0f}};

PointerEvent PointerAt(PointerPhase phase, Vec2 position, bool inside) {
    PointerEvent event;
    event.phase = phase;
    event.position = position;
    event.inside = inside;
    event.pointerId = 1;
    return event;
}

MenuItem Action(std::string label, std::string shortcut = "") {
    MenuItem item;
    item.label = std::move(label);
    item.shortcut = std::move(shortcut);
    return item;
}

MenuItem Separator() {
    MenuItem item;
    item.kind = MenuItemKind::Separator;
    return item;
}

MenuItem Submenu(std::string label, std::vector<MenuItem> children) {
    MenuItem item;
    item.label = std::move(label);
    item.kind = MenuItemKind::Submenu;
    item.children = std::move(children);
    return item;
}

// Quatro menus. Arquivo tem um submenu (Exportar) e separador; Exibir tem um submenu no primeiro item.
std::vector<MenuBarEntry> StandardMenus() {
    MenuItem refazer = Action("Refazer", "Ctrl+Y");
    refazer.disabled = true;
    return {
        {"Arquivo", {Action("Novo", "Ctrl+N"), Action("Salvar", "Ctrl+S"), Separator(),
                     Submenu("Exportar", {Action("PNG"), Action("JPG", "Ctrl+J")})},
         false},
        {"Editar", {Action("Desfazer", "Ctrl+Z"), refazer}, false},
        {"Ajuda", {Action("Sobre")}, false},
        {"Exibir", {Submenu("Paineis", {Action("Janela"), Action("Console")})}, false},
    };
}

void SetUpBar(MenuBar& bar) {
    bar.border = {{0.0f, 0.0f}, {400.0f, 26.0f}};
    bar.control.SetMenus(StandardMenus());
}

MenuBarEvents ClickAt(MenuBar& bar, Vec2 point) {
    (void)bar.OnPointer(PointerAt(PointerPhase::Down, point, true), kWindow);
    return bar.OnPointer(PointerAt(PointerPhase::Up, point, true), kWindow);
}

// Clique no cabecalho 0 (Arquivo) abre o menu; de novo fecha. Popup em (0, 26) com 24 + 24 + 7 + 24 = 79 px.
void TestClickHeaderOpensAndCloses() {
    MenuBar bar;
    SetUpBar(bar);
    MenuBarEvents e = ClickAt(bar, {40.0f, 13.0f});
    CHECK(e.changed && bar.control.IsOpen() && bar.control.ActiveMenu() == 0);
    CHECK(SameRect(bar.PopupRect(0), {{0.0f, 26.0f}, {200.0f, 79.0f}}));
    e = ClickAt(bar, {40.0f, 13.0f});
    CHECK(e.closed && !bar.control.IsOpen() && bar.PopupRect(0).size.y == 0.0f);
}

// Com um menu aberto, passar sobre outro cabecalho troca o menu aberto, sem clique.
void TestHoverSwitchesOpenMenu() {
    MenuBar bar;
    SetUpBar(bar);
    (void)ClickAt(bar, {40.0f, 13.0f});
    MenuBarEvents e = bar.OnPointer(PointerAt(PointerPhase::Move, {120.0f, 13.0f}, true), kWindow);
    CHECK(e.changed && bar.control.ActiveMenu() == 1);
    CHECK(Near(bar.PopupRect(0).position.x, 80.0f));  // Popup do cabecalho 1, que comeca em x 80.
    CHECK(!bar.OnPointer(PointerAt(PointerPhase::Move, {120.0f, 13.0f}, true), kWindow).changed);
}

// Clique num item de acao executa e fecha: o caminho escolhido e (indice 1) e nada fica aberto.
void TestClickActionActivatesAndCloses() {
    MenuBar bar;
    SetUpBar(bar);
    (void)ClickAt(bar, {40.0f, 13.0f});
    MenuBarEvents e = ClickAt(bar, {50.0f, 60.0f});  // Linha 1 (Salvar): de y 50 a 74.
    CHECK(e.activated && e.closed && !bar.control.IsOpen());
    CHECK(bar.control.ActivatedMenu() == 0 && bar.control.ActivationPath() == std::vector<std::size_t>{1});
}

// Clique no item Exportar (linha 3, de y 81 a 105) abre o submenu ao lado: x = 200 e y = 81, altura 48.
// Como 200 + 200 cabe na janela de 400, nao inverte.
void TestSubmenuCascadesRight() {
    MenuBar bar;
    SetUpBar(bar);
    (void)ClickAt(bar, {40.0f, 13.0f});
    MenuBarEvents e = ClickAt(bar, {50.0f, 90.0f});
    CHECK(e.changed && !e.closed && bar.control.Dropdown().Depth() == 2);
    CHECK(SameRect(bar.PopupRect(1), {{200.0f, 81.0f}, {200.0f, 48.0f}}));
}

// Exibir (cabecalho 3, em x 240): o popup desce encostado na janela (x 200). Seu submenu nao cabe a direita
// (200 + 200 > 400), entao inverte: x 0.
void TestSubmenuFlipsLeftWhenNoRoom() {
    MenuBar bar;
    SetUpBar(bar);
    (void)ClickAt(bar, {260.0f, 13.0f});
    CHECK(Near(bar.PopupRect(0).position.x, 200.0f));
    MenuBarEvents e = ClickAt(bar, {250.0f, 36.0f});  // Linha 0 (Paineis).
    CHECK(e.changed && bar.control.Dropdown().Depth() == 2);
    CHECK(Near(bar.PopupRect(1).position.x, 0.0f) && Near(bar.PopupRect(1).size.x, 200.0f));
}

// Escape fecha um nivel de cada vez: primeiro o submenu, depois o menu. Clique fora fecha tudo.
void TestEscapeClosesOneLevelAtATime() {
    MenuBar bar;
    SetUpBar(bar);
    (void)ClickAt(bar, {40.0f, 13.0f});
    (void)ClickAt(bar, {50.0f, 90.0f});
    MenuBarEvents e = bar.KeyPressed(MenuKey::Escape, kWindow);
    CHECK(e.changed && !e.closed && bar.control.Dropdown().Depth() == 1);
    e = bar.KeyPressed(MenuKey::Escape, kWindow);
    CHECK(e.closed && !bar.control.IsOpen());

    (void)ClickAt(bar, {40.0f, 13.0f});
    e = ClickAt(bar, {300.0f, 200.0f});
    CHECK(e.closed && !bar.control.IsOpen());
}

// Setas pulam o separador e dao a volta: Arquivo comeca em Novo (0); baixo leva a Salvar (1), depois a Exportar
// (3, pulando o separador 2), depois volta a Novo (0).
void TestArrowsSkipSeparatorAndWrap() {
    MenuBar bar;
    SetUpBar(bar);
    (void)ClickAt(bar, {40.0f, 13.0f});
    CHECK(bar.control.Dropdown().Highlighted(0) == 0);
    (void)bar.KeyPressed(MenuKey::Down, kWindow);
    CHECK(bar.control.Dropdown().Highlighted(0) == 1);
    (void)bar.KeyPressed(MenuKey::Down, kWindow);
    CHECK(bar.control.Dropdown().Highlighted(0) == 3);
    (void)bar.KeyPressed(MenuKey::Down, kWindow);
    CHECK(bar.control.Dropdown().Highlighted(0) == 0);
}

// Alt ativa a barra: setas movem o cabecalho, Enter abre o menu escolhido. Escape desativa a barra.
void TestAltActivatesBar() {
    MenuBar bar;
    SetUpBar(bar);
    CHECK(bar.PressAlt().changed && !bar.control.IsOpen());
    (void)bar.KeyPressed(MenuKey::Right, kWindow);
    (void)bar.KeyPressed(MenuKey::Right, kWindow);
    (void)bar.KeyPressed(MenuKey::Left, kWindow);
    MenuBarEvents e = bar.KeyPressed(MenuKey::Enter, kWindow);
    CHECK(e.changed && bar.control.IsOpen() && bar.control.ActiveMenu() == 1);

    (void)bar.KeyPressed(MenuKey::Escape, kWindow);
    (void)bar.KeyPressed(MenuKey::Escape, kWindow);
    CHECK(!bar.control.IsOpen());
    (void)bar.PressAlt();
    e = bar.KeyPressed(MenuKey::Escape, kWindow);
    CHECK(e.closed && !bar.KeyPressed(MenuKey::Right, kWindow).changed);
}

// Submenu abre depois de 0,2 s com o item destacado. Mover o destaque para um item comum zera a espera.
void TestSubmenuOpensAfterDelay() {
    MenuBar bar;
    SetUpBar(bar);
    (void)ClickAt(bar, {40.0f, 13.0f});
    (void)bar.KeyPressed(MenuKey::Down, kWindow);
    (void)bar.KeyPressed(MenuKey::Down, kWindow);  // Destacado: Exportar (submenu).
    CHECK(!bar.Tick(0.1f, kWindow).changed && bar.control.Dropdown().Depth() == 1);
    CHECK(bar.Tick(0.11f, kWindow).changed && bar.control.Dropdown().Depth() == 2);

    (void)bar.KeyPressed(MenuKey::Escape, kWindow);
    (void)bar.KeyPressed(MenuKey::Up, kWindow);  // Volta a Salvar (1), que nao e submenu.
    (void)bar.Tick(0.1f, kWindow);
    (void)bar.Tick(0.5f, kWindow);
    CHECK(bar.control.Dropdown().Depth() == 1);
    CHECK(!bar.Tick(std::nan(""), kWindow).changed);
}

// Atalho acha a acao com o menu fechado, inclusive dentro de submenu. Desabilitada e inexistente nao acham.
void TestShortcutFindsActionWhenClosed() {
    MenuBar bar;
    SetUpBar(bar);
    CHECK(!bar.control.IsOpen());
    const auto save = bar.FindShortcut("Ctrl+S");
    CHECK(save && save->menu == 0 && save->path == std::vector<std::size_t>{1});
    const auto jpg = bar.FindShortcut("Ctrl+J");
    CHECK(jpg && jpg->menu == 0 && jpg->path == (std::vector<std::size_t>{3, 1}));
    CHECK(!bar.FindShortcut("Ctrl+Y").has_value());  // Desabilitada.
    CHECK(!bar.FindShortcut("Ctrl+Q").has_value());
    CHECK(!bar.FindShortcut("").has_value());
}

// Passar o ponteiro sobre um cabecalho com a barra fechada nao abre nada.
void TestHoverDoesNotOpenClosedBar() {
    MenuBar bar;
    SetUpBar(bar);
    CHECK(!bar.OnPointer(PointerAt(PointerPhase::Move, {120.0f, 13.0f}, true), kWindow).changed);
    CHECK(!bar.control.IsOpen());
}

// Menu com sessenta itens nao cabe na janela (26 a 300): o popup tem 274 px e rola. O ultimo item (59) fica
// inteiro no fim: sua linha termina em 300.
void TestLongMenuScrollsToHighlight() {
    std::vector<MenuItem> items;
    for (int i = 0; i < 60; ++i) items.push_back(Action("item " + std::to_string(i)));
    MenuBar bar;
    bar.border = {{0.0f, 0.0f}, {400.0f, 26.0f}};
    bar.control.SetMenus({{"Longo", items, false}});
    (void)ClickAt(bar, {40.0f, 13.0f});
    CHECK(Near(bar.PopupRect(0).size.y, 274.0f));
    for (int i = 0; i < 59; ++i) (void)bar.KeyPressed(MenuKey::Down, kWindow);
    CHECK(bar.control.Dropdown().Highlighted(0) == 59);
    const Rect last = bar.RowRect(0, 59);
    CHECK(Near(last.position.y + last.size.y, 300.0f));
    CHECK(Near(last.size.y, 24.0f));
}

// Doze cabecalhos somam 960 px numa faixa de 400: alcancar o ultimo (11) rola a faixa para 560, o maximo.
void TestHeaderStripScrollsToActive() {
    std::vector<MenuBarEntry> menus;
    for (int i = 0; i < 12; ++i) menus.push_back({"Menu " + std::to_string(i), {Action("x")}, false});
    MenuBar bar;
    bar.border = {{0.0f, 0.0f}, {400.0f, 26.0f}};
    bar.control.SetMenus(menus);
    CHECK(bar.PressAlt().changed);
    for (int i = 0; i < 11; ++i) (void)bar.KeyPressed(MenuKey::Right, kWindow);
    CHECK(Near(bar.headerScrollX, 560.0f));
    CHECK(Near(bar.HeaderRect(11).position.x, 320.0f) && Near(bar.HeaderRect(11).position.x + 80.0f, 400.0f));
}

// Itens: com a barra fechada, a faixa e os 4 cabecalhos (5). Com Arquivo aberto, + fundo do popup, destaque
// (linha 0), separador e contorno (4): 5 + 7 = 12.
void TestMenuBarItems() {
    const MenuBarStyle style;
    MenuBar bar;
    SetUpBar(bar);
    CHECK(BuildMenuBarItems(bar, style, 0, 0.0f, 0).size() == 5);
    (void)ClickAt(bar, {40.0f, 13.0f});
    CHECK(BuildMenuBarItems(bar, style, 0, 0.0f, 0).size() == 12);
}

} // namespace

int main() {
    TestClickHeaderOpensAndCloses();
    TestHoverSwitchesOpenMenu();
    TestClickActionActivatesAndCloses();
    TestSubmenuCascadesRight();
    TestSubmenuFlipsLeftWhenNoRoom();
    TestEscapeClosesOneLevelAtATime();
    TestArrowsSkipSeparatorAndWrap();
    TestAltActivatesBar();
    TestSubmenuOpensAfterDelay();
    TestShortcutFindsActionWhenClosed();
    TestHoverDoesNotOpenClosedBar();
    TestLongMenuScrollsToHighlight();
    TestHeaderStripScrollsToActive();
    TestMenuBarItems();

    std::printf("menu bar: %d checks, %d failed\n", run, failed);
    return failed == 0 ? 0 : 1;
}
