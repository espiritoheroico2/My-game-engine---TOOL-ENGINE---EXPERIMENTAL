// Barra de abas (TabBar): clique, setas que trocam a aba ativa, rolagem de uma faixa que cresce, geometria das
// abas, paineis ocultos que mantem estado e caixa, e itens de desenho. Valores calculados a mao nos comentarios.
#include "SpriteUi/Elements/TabBar.hpp"

#include "SpriteUi/Elements/Checkbox.hpp"
#include "SpriteUi/Elements/ElementTree.hpp"

#include <cmath>
#include <cstdio>
#include <string>

namespace {

using namespace ToolEngine::SpriteUi;
using namespace ToolEngine::SpriteUi::Elements;
using ToolEngine::SpriteUi::Widgets::NoControlIndex;

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

PointerEvent PointerAt(PointerPhase phase, Vec2 position, bool inside) {
    PointerEvent event;
    event.phase = phase;
    event.position = position;
    event.inside = inside;
    event.pointerId = 1;
    return event;
}

// Faixa de 300 x 40 em (0, 0). Abas de 96 px com folga de 2: a aba i comeca em 4 + 98 i (4 px de padding).
void SetUpTabs(TabBar& tabs, std::size_t count) {
    tabs.border = {{0, 0}, {300, 40}};
    tabs.states.focused = true;
    for (std::size_t i = 0; i < count; ++i) tabs.control.labels.push_back("aba " + std::to_string(i));
    tabs.control.activeIndex = count > 0 ? 0 : NoControlIndex;
}

// Clique completo (Down e Up dentro da barra) no ponto dado.
TabEvents ClickAt(TabBar& tabs, Vec2 point) {
    (void)tabs.OnPointer(PointerAt(PointerPhase::Down, point, true));
    return tabs.OnPointer(PointerAt(PointerPhase::Up, point, true));
}

// Clique numa aba: a aba ativa muda uma vez; a mesma aba de novo nao muda; a folga e o padding nao fazem nada.
void TestClickSelectsOneTab() {
    TabBar tabs;
    SetUpTabs(tabs, 3);
    TabEvents e = ClickAt(tabs, {150, 20});   // Aba 1: de 102 a 198.
    CHECK(e.changed && tabs.control.activeIndex == 1);
    CHECK(!ClickAt(tabs, {150, 20}).changed);  // Ja ativa.
    CHECK(!ClickAt(tabs, {199, 20}).changed && tabs.control.activeIndex == 1);  // Folga entre 198 e 200.
    CHECK(!ClickAt(tabs, {2, 20}).changed && tabs.control.activeIndex == 1);    // Padding a esquerda.
    CHECK(!ClickAt(tabs, {250, 60}).changed && tabs.control.activeIndex == 1);  // Fora da faixa.
    CHECK(TabAt(tabs, {250, 20}) == 2);                                        // Aba 2: de 200 a 296.
}

// Setas: sem foco nada muda; com foco, andam uma aba e param nas pontas.
void TestArrowsMoveAndClamp() {
    TabBar tabs;
    SetUpTabs(tabs, 3);
    tabs.states.focused = false;
    CHECK(!tabs.KeyPressed(TabKey::Right).changed && tabs.control.activeIndex == 0);
    tabs.states.focused = true;
    CHECK(tabs.KeyPressed(TabKey::Right).changed && tabs.control.activeIndex == 1);
    CHECK(tabs.KeyPressed(TabKey::Right).changed && tabs.control.activeIndex == 2);
    CHECK(!tabs.KeyPressed(TabKey::Right).changed && tabs.control.activeIndex == 2);  // Ponta: sem volta.
    CHECK(tabs.KeyPressed(TabKey::Left).changed && tabs.control.activeIndex == 1);
    CHECK(tabs.KeyPressed(TabKey::Left).changed && tabs.control.activeIndex == 0);
    CHECK(!tabs.KeyPressed(TabKey::Left).changed && tabs.control.activeIndex == 0);
}

// Enter confirma a aba ativa sem mudar nada; desabilitada, a barra ignora tudo.
void TestEnterAndDisabled() {
    TabBar tabs;
    SetUpTabs(tabs, 3);
    TabEvents e = tabs.KeyPressed(TabKey::Enter);
    CHECK(e.activated && !e.changed && tabs.control.activeIndex == 0);

    tabs.states.disabled = true;
    CHECK(!ClickAt(tabs, {150, 20}).changed && tabs.control.activeIndex == 0);
    CHECK(!tabs.KeyPressed(TabKey::Right).changed);
    CHECK(!tabs.KeyPressed(TabKey::Enter).activated);
}

// Quarenta abas (3918 px) rolam. Indo ate a ultima, a rolagem para em 3626 (3918 - 292); a aba 39 fica inteira
// na faixa (x de 200 a 296). Voltando ao inicio, a rolagem volta a zero.
void TestGrowingStripScrollsToActive() {
    TabBar tabs;
    SetUpTabs(tabs, 40);
    CHECK(Near(tabs.scrollX, 0.0f));
    for (int i = 0; i < 39; ++i) (void)tabs.KeyPressed(TabKey::Right);
    CHECK(tabs.control.activeIndex == 39);
    CHECK(Near(tabs.scrollX, 3626.0f));
    const Rect last = TabRect(tabs, 39);
    CHECK(Near(last.position.x, 200.0f) && Near(last.size.x, 96.0f));
    CHECK(!tabs.KeyPressed(TabKey::Right).changed && Near(tabs.scrollX, 3626.0f));
    for (int i = 0; i < 39; ++i) (void)tabs.KeyPressed(TabKey::Left);
    CHECK(tabs.control.activeIndex == 0 && Near(tabs.scrollX, 0.0f));
}

// Quarenta abas: so as que tocam a faixa geram itens (fundo, abas visiveis, destaque). Com foco, mais o contorno.
void TestOnlyVisibleTabsAreDrawn() {
    const TabBarStyle style;
    TabBar tabs;
    SetUpTabs(tabs, 40);
    tabs.states.focused = false;
    // Faixa com 0 a 292 visivel: abas 0, 1 e 2 (a 2 cruza a borda). Fundo + 3 abas + destaque.
    CHECK(BuildTabBarItems(tabs, style, 0, 0.0f, 0).size() == 5);
    tabs.states.focused = true;
    CHECK(BuildTabBarItems(tabs, style, 0, 0.0f, 0).size() == 9);  // Mais o contorno de foco (4).
}

// Geometria: com 3 abas e faixa de 300 px, as abas nao rolam (conteudo 292 cabe em 292).
void TestTabGeometry() {
    TabBar tabs;
    SetUpTabs(tabs, 3);
    CHECK(Near(TabRect(tabs, 0).position.x, 4.0f));
    CHECK(Near(TabRect(tabs, 1).position.x, 102.0f));
    CHECK(Near(TabRect(tabs, 2).position.x, 200.0f));
    CHECK(Near(TabRect(tabs, 0).position.y, 4.0f) && Near(TabRect(tabs, 0).size.y, 32.0f));
    CHECK(tabs.KeyPressed(TabKey::Right).changed);
    CHECK(Near(tabs.scrollX, 0.0f));
}

// Painel: o painel ativo e arranjado na area abaixo da faixa (y 44, altura 100). Ao trocar de aba, o painel
// que ficou oculto mantem o controle marcado e a caixa antiga, mesmo quando a area muda.
void TestHiddenPanelKeepsStateAndBox() {
    ElementTree tree;
    const UIHandle root = tree.Create<Element>(InvalidUIHandle);
    const UIHandle tabHandle = tree.Create<TabBar>(root);
    const UIHandle first = tree.Create<Checkbox>(tabHandle);
    const UIHandle second = tree.Create<Checkbox>(tabHandle);
    tree.Arrange(root, {{0, 0}, {300, 200}});

    TabBar* tabs = tree.GetAs<TabBar>(tabHandle);
    tabs->transform.anchorMin = {0.0f, 0.0f};
    tabs->transform.anchorMax = {1.0f, 0.0f};
    tabs->transform.offsetMin = {0.0f, 0.0f};
    tabs->transform.offsetMax = {0.0f, 40.0f};
    tabs->control.labels = {"geral", "video"};
    tabs->control.activeIndex = 0;
    tabs->states.focused = true;
    tabs->panelHeight = 100.0f;
    for (UIHandle panel : {first, second}) {
        tree.Get(panel)->transform.anchorMax = {1.0f, 1.0f};  // Preenche a area do painel.
        tree.Get(panel)->transform.offsetMax = {0.0f, 0.0f};
    }
    tree.GetAs<Checkbox>(first)->checked = true;

    tree.Arrange(root, {{0, 0}, {300, 200}});
    tabs = tree.GetAs<TabBar>(tabHandle);
    tabs->ArrangePanel(tree);
    CHECK(Near(tree.Get(first)->border.position.y, 44.0f) && Near(tree.Get(first)->border.size.y, 100.0f));

    CHECK(tree.GetAs<TabBar>(tabHandle)->KeyPressed(TabKey::Right).changed);
    tree.GetAs<TabBar>(tabHandle)->panelHeight = 50.0f;
    tree.GetAs<TabBar>(tabHandle)->ArrangePanel(tree);
    // Aba 1 ativa: o painel de aba 1 recebe a nova altura; o da aba 0 fica com a caixa antiga.
    CHECK(Near(tree.Get(second)->border.size.y, 50.0f));
    CHECK(Near(tree.Get(first)->border.size.y, 100.0f));
    CHECK(tree.GetAs<Checkbox>(first)->checked);

    (void)tree.GetAs<TabBar>(tabHandle)->KeyPressed(TabKey::Left);
    CHECK(tree.GetAs<TabBar>(tabHandle)->control.activeIndex == 0);
    CHECK(tree.GetAs<Checkbox>(first)->checked);
}

// Desenha so o que cabe; o contorno aparece so com foco. Nenhuma aba e desenhada sem texto ou faixa.
void TestNoTabsNoItems() {
    const TabBarStyle style;
    TabBar tabs;
    SetUpTabs(tabs, 0);
    CHECK(BuildTabBarItems(tabs, style, 0, 0.0f, 0).size() == 1);  // So o fundo da faixa.
    CHECK(!tabs.KeyPressed(TabKey::Right).changed);
    CHECK(!tabs.KeyPressed(TabKey::Enter).activated);
}

} // namespace

int main() {
    TestClickSelectsOneTab();
    TestArrowsMoveAndClamp();
    TestEnterAndDisabled();
    TestGrowingStripScrollsToActive();
    TestOnlyVisibleTabsAreDrawn();
    TestTabGeometry();
    TestHiddenPanelKeepsStateAndBox();
    TestNoTabsNoItems();

    std::printf("tab bar: %d checks, %d failed\n", run, failed);
    return failed == 0 ? 0 : 1;
}
