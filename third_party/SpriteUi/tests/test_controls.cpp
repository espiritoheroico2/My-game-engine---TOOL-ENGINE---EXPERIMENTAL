// Modelos puros de controles (Controls.hpp): checkbox, slider, dropdown, abas e lista. Sem janela, sem SDL.
#include "SpriteUi/Widgets/Controls.hpp"

#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
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

bool Near(float a, float b, float eps = 1.0e-4f) {
    return std::fabs(a - b) <= eps;
}

std::vector<std::string> Options(std::size_t count) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < count; ++i)
        out.push_back("opt" + std::to_string(i));
    return out;
}

void TestButtonStatePriority() {
    UIButtonState b{};
    CHECK(ResolveControlState(b) == ControlState::Normal);
    b.hovered = true;
    CHECK(ResolveControlState(b) == ControlState::Hover);
    b.pressed = true;
    CHECK(ResolveControlState(b) == ControlState::Pressed);
    b.disabled = true;
    CHECK(ResolveControlState(b) == ControlState::Disabled);
    // Desabilitado vence tudo, mesmo com o ponteiro em cima e pressionado.
    UIButtonState off{};
    off.disabled = true;
    CHECK(ResolveControlState(off) == ControlState::Disabled);
}

void TestCheckboxToggle() {
    CheckboxControl cb;
    CHECK(!cb.checked);
    CHECK(cb.Toggle());
    CHECK(cb.checked);
    CHECK(cb.Toggle());
    CHECK(!cb.checked);

    cb.button.hovered = true;
    CHECK(cb.State() == ControlState::Hover);
    cb.button.pressed = true;
    CHECK(cb.State() == ControlState::Pressed);
    cb.button.hovered = false;
    cb.button.pressed = false;
    cb.button.disabled = true;
    CHECK(cb.State() == ControlState::Disabled);
}

void TestCheckboxDisabledDoesNotToggle() {
    CheckboxControl cb;
    cb.checked = true;
    cb.button.disabled = true;
    CHECK(!cb.Toggle());
    CHECK(cb.checked);
    CHECK(!cb.Toggle());
    CHECK(cb.checked);
}

void TestSliderPointerClampAndStep() {
    SliderControl s;
    s.slider.minValue = 0.0f;
    s.slider.maxValue = 1.0f;
    s.slider.value = 0.5f;
    CHECK(s.SetFromPointer(0.0f, 200.0f));
    CHECK(Near(s.slider.value, 0.0f));
    CHECK(!s.SetFromPointer(0.0f, 200.0f)); // Sem mudanca: retorna false.
    CHECK(s.SetFromPointer(200.0f, 200.0f));
    CHECK(Near(s.slider.value, 1.0f));
    CHECK(!s.SetFromPointer(900.0f, 200.0f)); // Fora da trilha: clamp no limite, que ja era o valor.
    CHECK(Near(s.slider.value, 1.0f));

    // Passo 0.25: 0.3 da trilha cai no passo mais proximo, 0.25.
    s.slider.step = 0.25f;
    CHECK(s.SetFromPointer(60.0f, 200.0f));
    CHECK(Near(s.slider.value, 0.25f));

    // Entrada invalida nao muda nada.
    const float before = s.slider.value;
    CHECK(!s.SetFromPointer(std::numeric_limits<float>::quiet_NaN(), 200.0f));
    CHECK(!s.SetFromPointer(10.0f, 0.0f));
    CHECK(!s.SetFromPointer(10.0f, -5.0f));
    CHECK(Near(s.slider.value, before));
}

void TestSliderKeys() {
    // Continuo (passo 0): setas andam 1 por cento da faixa.
    SliderControl c;
    c.slider.minValue = 0.0f;
    c.slider.maxValue = 100.0f;
    c.slider.value = 50.0f;
    CHECK(c.Key(SliderKey::Right));
    CHECK(Near(c.slider.value, 51.0f));
    CHECK(c.Key(SliderKey::Left));
    CHECK(Near(c.slider.value, 50.0f));
    CHECK(c.Key(SliderKey::Up));
    CHECK(Near(c.slider.value, 51.0f));
    CHECK(c.Key(SliderKey::Down));
    CHECK(Near(c.slider.value, 50.0f));

    CHECK(c.Key(SliderKey::End));
    CHECK(Near(c.slider.value, 100.0f));
    CHECK(!c.Key(SliderKey::End)); // Ja no limite.
    CHECK(!c.Key(SliderKey::Right));
    CHECK(c.Key(SliderKey::Home));
    CHECK(Near(c.slider.value, 0.0f));
    CHECK(!c.Key(SliderKey::Home));
    CHECK(!c.Key(SliderKey::Left));

    // Com passo: setas andam um passo; Home/End vao aos extremos.
    SliderControl st;
    st.slider.minValue = 0.0f;
    st.slider.maxValue = 1.0f;
    st.slider.step = 0.25f;
    st.slider.value = 0.5f;
    CHECK(st.Key(SliderKey::Right));
    CHECK(Near(st.slider.value, 0.75f));
    CHECK(st.Key(SliderKey::Right));
    CHECK(Near(st.slider.value, 1.0f));
    CHECK(!st.Key(SliderKey::Right));
    CHECK(st.Key(SliderKey::Left));
    CHECK(Near(st.slider.value, 0.75f));

    // Faixa vazia ou invalida: teclas nao mudam nada.
    SliderControl bad;
    bad.slider.minValue = 5.0f;
    bad.slider.maxValue = 5.0f;
    bad.slider.value = 5.0f;
    CHECK(!bad.Key(SliderKey::Right));
    CHECK(!bad.Key(SliderKey::End));
}

void TestSliderDisabledIgnoresUserInput() {
    SliderControl s;
    s.slider.value = 0.5f;
    s.disabled = true;
    CHECK(s.State() == ControlState::Disabled);
    CHECK(!s.SetFromPointer(10.0f, 100.0f));
    CHECK(!s.Key(SliderKey::Right));
    CHECK(!s.Key(SliderKey::Home));
    CHECK(Near(s.slider.value, 0.5f));
    // Valor de programa ainda vale.
    CHECK(s.SetValue(0.75f));
    CHECK(Near(s.slider.value, 0.75f));
}

void TestDropdownOpenSelectClose() {
    DropdownControl d;
    d.options = Options(4);
    CHECK(d.selectedIndex == NoControlIndex);
    CHECK(d.Open());
    CHECK(d.open);
    CHECK(d.highlightedIndex == 0); // Sem selecao, destaque comeca no item 0.
    CHECK(!d.Open());               // Ja aberta.
    CHECK(d.MoveDown());
    CHECK(d.highlightedIndex == 1);
    CHECK(d.Confirm());
    CHECK(d.selectedIndex == 1);
    CHECK(!d.open); // Escolher fecha.

    // Abrir de novo destaca a selecao atual.
    CHECK(d.Open());
    CHECK(d.highlightedIndex == 1);
    CHECK(!d.Confirm()); // Confirmar a mesma opcao: fecha, mas a selecao nao muda.
    CHECK(d.selectedIndex == 1);
    CHECK(!d.open);

    CHECK(!d.Select(1)); // Mesmo indice: fecha mas nao muda.
    CHECK(d.Select(3));
    CHECK(d.selectedIndex == 3);
}

void TestDropdownBoundsDoNotWrap() {
    DropdownControl d;
    d.options = Options(3);
    CHECK(d.Open());
    CHECK(!d.MoveUp()); // Destaque no item 0: nao volta ao fim.
    CHECK(d.highlightedIndex == 0);
    CHECK(d.MoveDown());
    CHECK(d.MoveDown());
    CHECK(d.highlightedIndex == 2);
    CHECK(!d.MoveDown()); // Na ponta: nao da a volta.
    CHECK(d.highlightedIndex == 2);
    CHECK(d.Confirm());
    CHECK(d.selectedIndex == 2);

    // Fechada, as setas mudam a selecao e param nas pontas.
    CHECK(d.MoveDown() == false);
    CHECK(d.selectedIndex == 2);
    CHECK(d.MoveUp());
    CHECK(d.selectedIndex == 1);
    CHECK(d.MoveUp());
    CHECK(d.MoveUp() == false);
    CHECK(d.selectedIndex == 0);
}

void TestDropdownClosedKeysWithoutSelection() {
    DropdownControl d;
    d.options = Options(3);
    CHECK(d.MoveUp()); // Sem selecao, qualquer seta leva ao item 0.
    CHECK(d.selectedIndex == 0);
}

void TestDropdownInvalidIndexAndDisabled() {
    DropdownControl d;
    d.options = Options(2);
    CHECK(d.Open());
    CHECK(!d.Select(7)); // Indice invalido: nada muda, a lista continua aberta.
    CHECK(d.selectedIndex == NoControlIndex);
    CHECK(d.open);
    CHECK(d.Close());
    CHECK(!d.Close());

    DropdownControl empty;
    CHECK(!empty.Open());
    CHECK(!empty.MoveDown());
    CHECK(!empty.Select(0));

    DropdownControl off;
    off.options = Options(2);
    off.disabled = true;
    CHECK(!off.Open());
    CHECK(!off.Select(0));
    CHECK(!off.MoveDown());
    CHECK(off.State() == ControlState::Disabled);
    CHECK(off.selectedIndex == NoControlIndex);
}

void TestDropdownEscapeKeepsSelection() {
    DropdownControl d;
    d.options = Options(3);
    CHECK(d.Select(0));
    CHECK(d.Open());
    CHECK(d.MoveDown());
    CHECK(d.highlightedIndex == 1);
    CHECK(d.Close()); // Escape.
    CHECK(d.selectedIndex == 0);
    CHECK(!d.open);
    CHECK(!d.Confirm()); // Fechada: Enter nao escolhe nada.
    CHECK(d.selectedIndex == 0);
}

void TestTabBarSelect() {
    TabBarControl t(Options(3));
    CHECK(t.activeIndex == 0);
    CHECK(t.labels.size() == 3);
    CHECK(t.labels[0] == "opt0" && t.labels[1] == "opt1" && t.labels[2] == "opt2"); // Ordem estavel.
    CHECK(t.Select(2));
    CHECK(t.activeIndex == 2);
    CHECK(t.labels[2] == "opt2"); // Selecionar nao reordena.
    CHECK(!t.Select(2));          // Ja ativa.
    CHECK(!t.Select(3));          // Indice invalido (== tamanho).
    CHECK(!t.Select(NoControlIndex));
    CHECK(t.activeIndex == 2);
    CHECK(t.Select(0));
    CHECK(t.activeIndex == 0);

    TabBarControl none;
    CHECK(none.activeIndex == NoControlIndex);
    CHECK(!none.Select(0));
    CHECK(none.activeIndex == NoControlIndex);

    TabBarControl off(Options(2));
    off.disabled = true;
    CHECK(!off.Select(1));
    CHECK(off.activeIndex == 0);
    CHECK(off.State() == ControlState::Disabled);
}

void TestListViewScrollMinimal() {
    // 100 itens de 20 px; area visivel de 100 px (5 itens).
    ListViewControl l;
    l.SetItemHeight(20.0f);
    l.SetItemCount(100);
    l.SetViewportHeight(100.0f);
    CHECK(Near(l.MaxScrollY(), 1900.0f));

    CHECK(l.Select(0));
    CHECK(Near(l.scrollOffsetY, 0.0f));
    CHECK(l.Select(4)); // Item 4 termina em 100, ainda cabe: nao rola.
    CHECK(Near(l.scrollOffsetY, 0.0f));
    CHECK(l.Select(5)); // Item 5 termina em 120: rola o minimo (20).
    CHECK(Near(l.scrollOffsetY, 20.0f));
    CHECK(l.Select(2)); // Ja visivel: rolagem nao muda.
    CHECK(Near(l.scrollOffsetY, 20.0f));
    CHECK(l.Select(1)); // Item 1 começa em 20, o topo da area: visivel, nao rola.
    CHECK(Near(l.scrollOffsetY, 20.0f));
    CHECK(l.Select(0)); // Acima da area: rola para o topo do item.
    CHECK(Near(l.scrollOffsetY, 0.0f));
    CHECK(l.Select(99)); // Ultimo item: rolagem no maximo.
    CHECK(Near(l.scrollOffsetY, 1900.0f));
    CHECK(!l.Select(99));
    CHECK(!l.Select(100)); // Indice invalido.
    CHECK(l.SelectedIndex() == 99);
}

void TestListViewKeysAndBounds() {
    ListViewControl l;
    l.SetItemHeight(20.0f);
    l.SetItemCount(3);
    l.SetViewportHeight(100.0f);

    CHECK(l.Key(ListKey::Down)); // Sem selecao: vai ao item 0.
    CHECK(l.SelectedIndex() == 0);
    CHECK(!l.Key(ListKey::Up)); // Ja no item 0: para.
    CHECK(l.SelectedIndex() == 0);
    CHECK(l.Key(ListKey::Down));
    CHECK(l.Key(ListKey::End));
    CHECK(l.SelectedIndex() == 2);
    CHECK(!l.Key(ListKey::Down)); // Ultimo: para.
    CHECK(l.Key(ListKey::Home));
    CHECK(l.SelectedIndex() == 0);

    ListViewControl empty;
    CHECK(!empty.Key(ListKey::Down));
    CHECK(!empty.Select(0));

    ListViewControl off;
    off.SetItemCount(3);
    off.disabled = true;
    CHECK(!off.Key(ListKey::Down));
    CHECK(!off.Select(0));
    CHECK(off.State() == ControlState::Disabled);
}

void TestListSelectionFollowsItemIdentity() {
    ListViewControl l;
    l.SetItemHeight(20.0f);
    l.SetViewportHeight(100.0f);
    l.SetItemIds({10, 20, 30});
    CHECK(l.Select(1)); // item de id 20
    CHECK(l.selectedId == 20);
    l.SetItemIds({5, 10, 20, 30}); // item novo antes da selecao
    CHECK(l.selectedId == 20);
    CHECK(l.SelectedIndex() == 2);
    l.SetItemIds({10, 30}); // o item selecionado some
    CHECK(l.selectedId == NoListItem);
    CHECK(l.SelectedIndex() == NoControlIndex);
}

void TestListSelectById() {
    ListViewControl l;
    l.SetItemIds({7, 8, 9});
    CHECK(l.SelectId(9));
    CHECK(l.SelectedIndex() == 2);
    CHECK(!l.SelectId(9));
    CHECK(!l.SelectId(42)); // id que nao esta na lista
    CHECK(l.selectedId == 9);
}

void TestListViewNoScrollWithoutViewport() {
    ListViewControl l;
    l.SetItemHeight(20.0f);
    l.SetItemCount(100);
    // Sem area visivel (altura zero ou invalida): nao rola.
    CHECK(l.Select(50));
    CHECK(Near(l.scrollOffsetY, 0.0f));
    l.SetViewportHeight(std::numeric_limits<float>::quiet_NaN());
    CHECK(l.viewportHeight == 0.0f);
    CHECK(l.Select(60));
    CHECK(Near(l.scrollOffsetY, 0.0f));
}

void TestListViewCountShrinkClampsSelectionAndScroll() {
    ListViewControl l;
    l.SetItemHeight(20.0f);
    l.SetItemCount(100);
    l.SetViewportHeight(100.0f);
    CHECK(l.Select(80));
    CHECK(Near(l.scrollOffsetY, 1520.0f)); // Item 80 termina em 1620: rolagem minima = 1620 - 100.
    l.SetItemCount(10); // Lista encolhe: selecao 80 some; rolagem recua para o novo maximo (200 - 100).
    CHECK(l.SelectedIndex() == NoControlIndex);
    CHECK(Near(l.scrollOffsetY, 100.0f));
    CHECK(Near(l.MaxScrollY(), 100.0f));
}

void TestListViewVisibleRange() {
    ListViewControl l;
    l.SetItemHeight(20.0f);
    l.SetItemCount(100);
    l.SetViewportHeight(100.0f);
    CHECK(l.Select(50)); // Rola para o item 50 aparecer por baixo.
    const VirtualListRange r = l.VisibleRange();
    CHECK(r.firstIndex <= 46 && r.lastIndex >= 50); // Visivel pelo menos a selecao.
    CHECK(r.lastIndex < 100);
}

} // namespace

int main() {
    TestButtonStatePriority();
    TestCheckboxToggle();
    TestCheckboxDisabledDoesNotToggle();
    TestSliderPointerClampAndStep();
    TestSliderKeys();
    TestSliderDisabledIgnoresUserInput();
    TestDropdownOpenSelectClose();
    TestDropdownBoundsDoNotWrap();
    TestDropdownClosedKeysWithoutSelection();
    TestDropdownInvalidIndexAndDisabled();
    TestDropdownEscapeKeepsSelection();
    TestTabBarSelect();
    TestListViewScrollMinimal();
    TestListViewKeysAndBounds();
    TestListViewNoScrollWithoutViewport();
    TestListViewCountShrinkClampsSelectionAndScroll();
    TestListSelectionFollowsItemIdentity();
    TestListSelectById();
    TestListViewVisibleRange();
    std::printf("controls: %d/%d checks passed\n", run - failed, run);
    return failed == 0 ? 0 : 1;
}
