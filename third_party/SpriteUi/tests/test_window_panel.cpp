// Janela (Window) e painel (Panel): arrasto so pela barra, botoes, redimensionamento com minimo, trazer para
// frente, rolagem limitada, filhos que rolam juntos e recorte. Valores calculados a mao nos comentarios.
#include "SpriteUi/Elements/Panel.hpp"
#include "SpriteUi/Elements/Window.hpp"

#include "SpriteUi/Elements/ElementTree.hpp"

#include <cmath>
#include <cstdio>
#include <limits>

namespace {

using namespace ToolEngine::SpriteUi;
using namespace ToolEngine::SpriteUi::Elements;

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

PointerEvent PointerAt(PointerPhase phase, Vec2 position, bool inside) {
    PointerEvent event;
    event.phase = phase;
    event.position = position;
    event.inside = inside;
    event.pointerId = 1;
    return event;
}

// Janela de 200 x 150 em (100, 100): barra de 30 px no topo, botoes de 20 px. Fechar em x 280..300 e y 105..125;
// minimizar em x 256..276. Bordas com 6 px.
void SetUpWindow(Window& win) {
    win.transform.offsetMin = {100.0f, 100.0f};
    win.transform.offsetMax = {300.0f, 250.0f};
    win.border = {{100.0f, 100.0f}, {200.0f, 150.0f}};
}

// Clique completo sobre o ponto (Down e Up dentro).
WindowEvents ClickWindow(Window& win, Vec2 point) {
    (void)win.OnPointer(PointerAt(PointerPhase::Down, point, true));
    return win.OnPointer(PointerAt(PointerPhase::Up, point, true));
}

// Arrastar pela barra: so move depois de 4 px; depois, a janela segue o ponteiro.
void TestDragByTitleBarOnly() {
    Window win;
    SetUpWindow(win);
    (void)win.OnPointer(PointerAt(PointerPhase::Down, {150, 110}, true));  // Barra: de y 100 a 130.
    WindowEvents e = win.OnPointer(PointerAt(PointerPhase::Move, {153, 110}, true));
    CHECK(!e.moved && Near(win.border.position.x, 100.0f));                // 3 px: abaixo do limite de 4.
    e = win.OnPointer(PointerAt(PointerPhase::Move, {180, 130}, true));    // Delta (30, 20).
    CHECK(e.moved && Near(win.border.position.x, 130.0f) && Near(win.border.position.y, 120.0f));
    CHECK(Near(win.transform.offsetMin.x, 130.0f) && Near(win.transform.offsetMax.y, 270.0f));
    (void)win.OnPointer(PointerAt(PointerPhase::Up, {180, 130}, true));
    CHECK(Near(win.border.size.x, 200.0f) && Near(win.border.size.y, 150.0f));
}

// Arrastar o corpo (fora da barra) nao move a janela.
void TestBodyDoesNotDrag() {
    Window win;
    SetUpWindow(win);
    (void)win.OnPointer(PointerAt(PointerPhase::Down, {150, 200}, true));
    WindowEvents e = win.OnPointer(PointerAt(PointerPhase::Move, {250, 260}, true));
    CHECK(!e.moved && SameRect(win.border, {{100, 100}, {200, 150}}));
}

// Arrastar a janela nao mexe em irmaos fora dela: a janela so altera a propria caixa.
void TestSiblingStaysWhenWindowMoves() {
    ElementTree tree;
    const UIHandle root = tree.Create<Element>(InvalidUIHandle);
    tree.Get(root)->transform.anchorMax = {1.0f, 1.0f};
    tree.Get(root)->transform.offsetMax = {0.0f, 0.0f};
    const UIHandle winHandle = tree.Create<Window>(root);
    const UIHandle siblingHandle = tree.Create<Element>(root);
    tree.Get(siblingHandle)->transform.offsetMin = {0.0f, 0.0f};
    tree.Get(siblingHandle)->transform.offsetMax = {50.0f, 50.0f};
    tree.Arrange(root, {{0, 0}, {400, 300}});
    Window* win = tree.GetAs<Window>(winHandle);
    win->transform.offsetMin = {100.0f, 100.0f};
    win->transform.offsetMax = {300.0f, 250.0f};
    tree.Arrange(root, {{0, 0}, {400, 300}});
    const Rect siblingBefore = tree.Get(siblingHandle)->border;

    win = tree.GetAs<Window>(winHandle);
    (void)win->OnPointer(PointerAt(PointerPhase::Down, {150, 110}, true));
    (void)win->OnPointer(PointerAt(PointerPhase::Move, {180, 130}, true));
    CHECK(SameRect(tree.Get(siblingHandle)->border, siblingBefore));
    CHECK(Near(tree.GetAs<Window>(winHandle)->border.position.x, 130.0f));
}

// Botao fechar pede Close e minimizar pede Minimize; soltar fora do botao nao pede nada e nao arrasta.
void TestButtonsRequestCloseAndMinimize() {
    Window win;
    SetUpWindow(win);
    WindowEvents e = ClickWindow(win, {290, 115});
    CHECK(e.closeRequested && !e.minimizeRequested && !e.moved);
    e = ClickWindow(win, {266, 115});
    CHECK(e.minimizeRequested && !e.closeRequested);

    (void)win.OnPointer(PointerAt(PointerPhase::Down, {290, 115}, true));
    e = win.OnPointer(PointerAt(PointerPhase::Up, {290, 115}, false));
    CHECK(!e.closeRequested && !e.minimizeRequested);
    CHECK(WindowButtonAt(win, {290, 115}) == WindowButton::Close && WindowButtonAt(win, {100, 100}) == WindowButton::None);
}

// Pressionar em qualquer ponto da janela pede trazer para frente; fora dela, nao.
void TestPressBringsToFront() {
    Window win;
    SetUpWindow(win);
    CHECK(win.OnPointer(PointerAt(PointerPhase::Down, {150, 200}, true)).broughtToFront);
    CHECK(!win.OnPointer(PointerAt(PointerPhase::Down, {50, 50}, false)).broughtToFront);
}

// Canto de baixo a direita redimensiona: +50 e +30 aumenta para 250 x 180. Puxado alem do minimo, para em 150 x 100
// com a borda de origem parada em (100, 100).
void TestResizeCornerRespectsMinimum() {
    Window win;
    SetUpWindow(win);
    CHECK((WindowEdgesAt(win, {299, 249}) & (EdgeRight | EdgeBottom)) == (EdgeRight | EdgeBottom));
    (void)win.OnPointer(PointerAt(PointerPhase::Down, {299, 249}, true));
    WindowEvents e = win.OnPointer(PointerAt(PointerPhase::Move, {349, 279}, true));
    CHECK(e.resized && Near(win.border.size.x, 250.0f) && Near(win.border.size.y, 180.0f));
    (void)win.OnPointer(PointerAt(PointerPhase::Move, {-201, -251}, true));  // Delta (-500, -500) desde o inicio.
    CHECK(SameRect(win.border, {{100, 100}, {150, 100}}));
    (void)win.OnPointer(PointerAt(PointerPhase::Up, {-201, -251}, true));
}

// Borda esquerda puxada alem da direita: a esquerda para em (300 - 150) = 150; a direita fica em 300.
void TestLeftEdgeKeepsRightEdge() {
    Window win;
    SetUpWindow(win);
    (void)win.OnPointer(PointerAt(PointerPhase::Down, {101, 200}, true));
    (void)win.OnPointer(PointerAt(PointerPhase::Move, {400, 200}, true));
    CHECK(SameRect(win.border, {{150, 100}, {150, 150}}));
}

// Cancel (captura perdida) desfaz o arraste.
void TestCancelRestoresBox() {
    Window win;
    SetUpWindow(win);
    (void)win.OnPointer(PointerAt(PointerPhase::Down, {150, 110}, true));
    (void)win.OnPointer(PointerAt(PointerPhase::Move, {180, 130}, true));
    (void)win.OnPointer(PointerAt(PointerPhase::Cancel, {180, 130}, false));
    CHECK(SameRect(win.border, {{100, 100}, {200, 150}}));
    CHECK(Near(win.transform.offsetMin.x, 100.0f));
}

// Desabilitada, a janela nao reage.
void TestDisabledWindowIgnoresPointer() {
    Window win;
    SetUpWindow(win);
    win.states.disabled = true;
    CHECK(!win.OnPointer(PointerAt(PointerPhase::Down, {150, 110}, true)).broughtToFront);
    CHECK(!win.OnPointer(PointerAt(PointerPhase::Move, {200, 200}, true)).moved);
    CHECK(SameRect(win.border, {{100, 100}, {200, 150}}));
}

// Janela: fundo, barra, dois botoes (fundo e traco) e contorno. 1 + 1 + (1 + 1) + (1 + 2) + 4 = 11.
void TestWindowItems() {
    const WindowStyle style;
    Window win;
    SetUpWindow(win);
    CHECK(BuildWindowItems(win, style, 0.0f, 0).size() == 11);
}

// Painel: o conteudo maior rola ate (0, 200) quando a area e 100 de altura e o conteudo 300. Fora dos limites,
// a rolagem para no limite; valores nao finitos sao ignorados.
void TestScrollClampsToContent() {
    Panel panel;
    panel.border = {{0, 0}, {200, 100}};
    panel.contentSize = {200.0f, 300.0f};
    CHECK(Near(panel.MaxScroll().x, 0.0f) && Near(panel.MaxScroll().y, 200.0f));
    panel.ScrollBy({0.0f, 1000.0f});
    CHECK(Near(panel.scroll.y, 200.0f));
    panel.ScrollBy({0.0f, -1000.0f});
    CHECK(Near(panel.scroll.y, 0.0f));
    panel.ScrollBy({50.0f, 0.0f});
    CHECK(Near(panel.scroll.x, 0.0f));
    const float nan = std::numeric_limits<float>::quiet_NaN();
    panel.ScrollBy({0.0f, 30.0f});
    panel.ScrollBy({nan, nan});
    CHECK(Near(panel.scroll.y, 30.0f));
}

// Conteudo que cabe nao rola; a roda move 24 px por passo.
void TestNoScrollWhenContentFitsAndWheel() {
    Panel panel;
    panel.border = {{0, 0}, {200, 100}};
    panel.contentSize = {100.0f, 50.0f};
    panel.ScrollBy({0.0f, 40.0f});
    CHECK(Near(panel.scroll.y, 0.0f));

    panel.contentSize = {200.0f, 300.0f};
    panel.ScrollByWheel(2.0f);
    CHECK(Near(panel.scroll.y, 48.0f));
    panel.ScrollByWheel(std::numeric_limits<float>::infinity());
    CHECK(Near(panel.scroll.y, 48.0f));
}

// Dois filhos recebem o mesmo deslocamento: com rolagem de 50, o de cima sai (y -40) e o de baixo entra (y 30).
// Com rolagem zero, voltam a y 10 e 80. Ver so o de baixo: o de cima fica fora do recorte.
void TestChildrenScrollTogether() {
    ElementTree tree;
    const UIHandle root = tree.Create<Element>(InvalidUIHandle);
    tree.Get(root)->transform.anchorMax = {1.0f, 1.0f};
    tree.Get(root)->transform.offsetMax = {0.0f, 0.0f};
    const UIHandle panelHandle = tree.Create<Panel>(root);
    tree.Get(panelHandle)->transform.anchorMax = {1.0f, 1.0f};
    tree.Get(panelHandle)->transform.offsetMax = {0.0f, 0.0f};
    const UIHandle top = tree.Create<Element>(panelHandle);
    tree.Get(top)->transform.offsetMin = {0.0f, 10.0f};
    tree.Get(top)->transform.offsetMax = {50.0f, 40.0f};
    const UIHandle bottom = tree.Create<Element>(panelHandle);
    tree.Get(bottom)->transform.offsetMin = {0.0f, 80.0f};
    tree.Get(bottom)->transform.offsetMax = {50.0f, 110.0f};
    tree.Arrange(root, {{0, 0}, {200, 100}});

    Panel* panel = tree.GetAs<Panel>(panelHandle);
    panel->contentSize = {200.0f, 300.0f};
    panel->ScrollBy({0.0f, 50.0f});
    panel->ArrangeChildren(tree);
    CHECK(Near(tree.Get(top)->border.position.y, -40.0f));
    CHECK(Near(tree.Get(bottom)->border.position.y, 30.0f));
    panel = tree.GetAs<Panel>(panelHandle);
    CHECK(!panel->ChildVisible(*tree.Get(top)) && panel->ChildVisible(*tree.Get(bottom)));

    panel->ScrollBy({0.0f, -50.0f});
    panel->ArrangeChildren(tree);
    CHECK(Near(tree.Get(top)->border.position.y, 10.0f) && Near(tree.Get(bottom)->border.position.y, 80.0f));
    CHECK(panel->ChildVisible(*tree.Get(top)));
}

// Painel: fundo e contorno; a barra de rolagem so aparece quando o conteudo rola.
void TestPanelItems() {
    const PanelStyle style;
    Panel panel;
    panel.border = {{0, 0}, {200, 100}};
    panel.contentSize = {100.0f, 50.0f};
    CHECK(BuildPanelItems(panel, style, 0.0f, 0).size() == 5);  // Fundo e contorno (4).
    panel.contentSize = {200.0f, 300.0f};
    CHECK(BuildPanelItems(panel, style, 0.0f, 0).size() == 6);  // Mais a barra de rolagem.
}

} // namespace

int main() {
    TestDragByTitleBarOnly();
    TestBodyDoesNotDrag();
    TestSiblingStaysWhenWindowMoves();
    TestButtonsRequestCloseAndMinimize();
    TestPressBringsToFront();
    TestResizeCornerRespectsMinimum();
    TestLeftEdgeKeepsRightEdge();
    TestCancelRestoresBox();
    TestDisabledWindowIgnoresPointer();
    TestWindowItems();
    TestScrollClampsToContent();
    TestNoScrollWhenContentFitsAndWheel();
    TestChildrenScrollTogether();
    TestPanelItems();

    std::printf("window and panel: %d checks, %d failed\n", run, failed);
    return failed == 0 ? 0 : 1;
}
