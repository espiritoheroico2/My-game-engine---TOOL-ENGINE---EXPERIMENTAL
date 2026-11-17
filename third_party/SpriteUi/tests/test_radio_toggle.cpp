// Grupo de radio (exclusao mutua por identificador, linhas que crescem, teclado) e chave Toggle (clique, foco,
// geometria da trilha e da bolinha). Os valores esperados foram calculados a mao e estao nos comentarios.
#include "SpriteUi/Elements/RadioButton.hpp"
#include "SpriteUi/Elements/Toggle.hpp"

#include <cmath>
#include <cstdio>

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

bool SameRect(Rect actual, Rect expected) {
    return Near(actual.position.x, expected.position.x) && Near(actual.position.y, expected.position.y) &&
           Near(actual.size.x, expected.size.x) && Near(actual.size.y, expected.size.y);
}

PointerEvent PointerAt(PointerPhase phase, Vec2 position, bool inside) {
    PointerEvent event;
    event.phase = phase;
    event.position = position;
    event.inside = inside;
    event.pointerId = 1;
    return event;
}

// Grupo com tres opcoes (ids 10, 20, 30) em uma caixa de 200 x 100. Linhas de 28 px: 0..28, 28..56, 56..84.
void FillGroup(RadioGroup& group) {
    group.border = {{0, 0}, {200, 100}};
    CHECK(group.AddOption(10, "Pequeno"));
    CHECK(group.AddOption(20, "Medio"));
    CHECK(group.AddOption(30, "Grande"));
}

// Uma so escolha por grupo: guarda o identificador. Escolher outra desmarca a anterior.
void TestRadioExclusiveSelection() {
    RadioGroup group;
    FillGroup(group);
    CHECK(group.SelectedIndex() == NoRadioIndex);
    CHECK(group.Select(20));
    CHECK(group.selectedId == 20 && group.SelectedIndex() == 1);
    CHECK(group.Select(10));
    CHECK(group.selectedId == 10 && group.SelectedIndex() == 0);
    CHECK(!group.Select(10));                 // Mesma opcao: nada muda.
    CHECK(!group.Select(999));                // Desconhecida: nada muda.
    CHECK(group.selectedId == 10);
}

// Identificadores: repetido e reservado sao recusados; a lista aceita quantas opcoes forem adicionadas.
void TestRadioOptionsGrow() {
    RadioGroup group;
    CHECK(group.AddOption(1, "a"));
    CHECK(!group.AddOption(1, "repetido"));
    CHECK(!group.AddOption(NoRadioOption, "reservado"));
    for (RadioOptionId id = 2; id <= 60; ++id) CHECK(group.AddOption(id, "opcao"));
    CHECK(group.options.size() == 60);
    CHECK(group.Select(60) && group.SelectedIndex() == 59);
}

// Click numa linha escolhe; arrastar para fora e soltar nao escolhe; desabilitado nao escolhe pelo usuario.
void TestRadioClickRules() {
    RadioGroup group;
    FillGroup(group);
    (void)group.OnPointer(PointerAt(PointerPhase::Down, {10, 30}, true));
    RadioEvents e = group.OnPointer(PointerAt(PointerPhase::Up, {10, 30}, true));  // Linha 1 (28..56).
    CHECK(e.clicked && e.valueChanged && group.selectedId == 20);

    (void)group.OnPointer(PointerAt(PointerPhase::Down, {10, 5}, true));
    (void)group.OnPointer(PointerAt(PointerPhase::Leave, {-5, 5}, false));
    e = group.OnPointer(PointerAt(PointerPhase::Up, {-5, 5}, false));
    CHECK(!e.clicked && !e.valueChanged && group.selectedId == 20);

    group.states.disabled = true;
    (void)group.OnPointer(PointerAt(PointerPhase::Down, {10, 5}, true));
    CHECK(!group.OnPointer(PointerAt(PointerPhase::Up, {10, 5}, true)).valueChanged);
    CHECK(group.selectedId == 20);
    CHECK(group.Select(30) && group.selectedId == 30);   // Programa continua valendo.
}

// Linha sob o ponto: limites de cada faixa e nada fora da lista.
void TestRadioRowAt() {
    RadioGroup group;
    FillGroup(group);
    CHECK(group.RowAt({10, 0}) == 0);
    CHECK(group.RowAt({10, 27.9f}) == 0);
    CHECK(group.RowAt({10, 28}) == 1);
    CHECK(group.RowAt({10, 83.9f}) == 2);
    CHECK(group.RowAt({10, 84}) == NoRadioIndex);   // Abaixo da ultima linha.
    CHECK(group.RowAt({250, 10}) == NoRadioIndex);  // Fora da largura.
    CHECK(group.RowAt({10, -1}) == NoRadioIndex);
}

// Setas movem a escolha e o foco, sem dar a volta; Espaco escolhe o focado; Tab entra na opcao escolhida.
void TestRadioKeyboard() {
    RadioGroup group;
    FillGroup(group);
    group.FocusSelected();
    CHECK(group.focusedIndex == 0);          // Sem escolha: primeira opcao.
    CHECK(group.MoveSelection(+1) && group.selectedId == 20 && group.focusedIndex == 1);
    CHECK(group.MoveSelection(+1) && group.selectedId == 30);
    CHECK(!group.MoveSelection(+1));         // Na ultima: para.
    CHECK(group.selectedId == 30);
    CHECK(group.MoveSelection(-1) && group.selectedId == 20);

    group.focusedIndex = 0;
    CHECK(group.SelectFocused() && group.selectedId == 10);
    CHECK(!group.SelectFocused());

    group.states.disabled = true;
    CHECK(!group.MoveSelection(+1) && !group.SelectFocused());
    CHECK(group.selectedId == 10);
}

// Geometria: linha i comeca em i * 28; circulo centrado na linha; rotulo recuado pelo circulo e a folga.
void TestRadioGeometry() {
    RadioGroup group;
    FillGroup(group);
    const RadioStyle style;
    CHECK(SameRect(RadioRowRect(group, 1), {{0, 28}, {200, 28}}));
    CHECK(SameRect(RadioRowRect(group, 3), {{0, 0}, {0, 0}}));
    // Circulo de 18 px centrado na linha de 28: deslocamento (28 - 18) / 2 = 5.
    CHECK(SameRect(RadioCircleRect(group, style, 1), {{0, 33}, {18, 18}}));
    // Rotulo: 18 + 8 = 26 a partir da esquerda.
    CHECK(SameRect(RadioLabelRect(group, style, 1), {{26, 28}, {174, 28}}));
}

// Itens: um circulo por opcao; a escolhida ganha o ponto; a focada ganha um anel (um item). Desabilitado: sem anel.
void TestRadioItems() {
    RadioGroup group;
    FillGroup(group);
    const RadioStyle style;
    CHECK(BuildRadioGroupItems(group, style, 0.0f, 0).size() == 3);
    group.Select(20);
    CHECK(BuildRadioGroupItems(group, style, 0.0f, 0).size() == 4);
    group.focusedIndex = 2;
    CHECK(BuildRadioGroupItems(group, style, 0.0f, 0).size() == 5);
    group.states.disabled = true;
    CHECK(BuildRadioGroupItems(group, style, 0.0f, 0).size() == 4);
}

// Chave: Click inverte uma vez; arrastar para fora nao muda; desabilitada nao muda; Espaco so com foco.
void TestToggleRules() {
    Toggle toggle;
    toggle.border = {{0, 0}, {200, 30}};
    (void)toggle.OnPointer(PointerAt(PointerPhase::Down, {5, 5}, true));
    ToggleEvents e = toggle.OnPointer(PointerAt(PointerPhase::Up, {5, 5}, true));
    CHECK(e.clicked && e.valueChanged && toggle.on);

    (void)toggle.OnPointer(PointerAt(PointerPhase::Down, {5, 5}, true));
    (void)toggle.OnPointer(PointerAt(PointerPhase::Leave, {-5, 5}, false));
    e = toggle.OnPointer(PointerAt(PointerPhase::Up, {-5, 5}, false));
    CHECK(!e.clicked && !e.valueChanged && toggle.on);

    CHECK(!toggle.ToggleByKey().valueChanged);   // Sem foco, nada.
    toggle.states.focused = true;
    CHECK(toggle.ToggleByKey().valueChanged && !toggle.on);

    toggle.states.disabled = true;
    CHECK(!toggle.OnPointer(PointerAt(PointerPhase::Down, {5, 5}, true)).valueChanged);
    CHECK(!toggle.ToggleByKey().valueChanged && !toggle.on);
}

// Trilha de 40 x 28 centrada numa area de 200 x 30: x 0, y (30 - 28) / 2 = 1.
// Bolinha de 28 - 2 * 3 = 22 px: a esquerda em x = 3 quando desligada, em x = 40 - 3 - 22 = 15 quando ligada.
void TestToggleGeometry() {
    Toggle toggle;
    toggle.border = {{0, 0}, {200, 30}};
    const ToggleStyle style;
    CHECK(SameRect(ToggleTrackRect(toggle, style), {{0, 1}, {40, 28}}));
    CHECK(SameRect(ToggleKnobRect(toggle, style), {{3, 4}, {22, 22}}));
    toggle.on = true;
    CHECK(SameRect(ToggleKnobRect(toggle, style), {{15, 4}, {22, 22}}));
}

// Area de clique da chave: a pilula da trilha (40 x 28), nao a caixa do elemento (200 x 30). Canto da caixa e
// area ao lado da trilha nao acionam; o meio da trilha aciona; o hover segue a mesma area.
void TestToggleClickAreaFollowsTrack() {
    Toggle toggle;
    toggle.border = {{0, 0}, {200, 30}};
    const ToggleStyle style;
    (void)toggle.OnPointer(PointerAt(PointerPhase::Down, {100, 15}, true), style);
    CHECK(!toggle.OnPointer(PointerAt(PointerPhase::Up, {100, 15}, true), style).clicked && !toggle.on);
    (void)toggle.OnPointer(PointerAt(PointerPhase::Down, {1, 2}, true), style);   // Canto, fora do arco.
    CHECK(!toggle.OnPointer(PointerAt(PointerPhase::Up, {1, 2}, true), style).clicked && !toggle.on);
    (void)toggle.OnPointer(PointerAt(PointerPhase::Down, {20, 15}, true), style); // Meio da trilha.
    CHECK(toggle.OnPointer(PointerAt(PointerPhase::Up, {20, 15}, true), style).clicked && toggle.on);

    (void)toggle.OnPointer(PointerAt(PointerPhase::Enter, {100, 15}, true), style);
    CHECK(!toggle.states.hovered);
    (void)toggle.OnPointer(PointerAt(PointerPhase::Enter, {20, 15}, true), style);
    CHECK(toggle.states.hovered);
}

// Area menor que a trilha: a trilha encolhe para caber, sem passar da borda.
void TestToggleSmallArea() {
    Toggle toggle;
    toggle.border = {{0, 0}, {20, 10}};
    const ToggleStyle style;
    CHECK(SameRect(ToggleTrackRect(toggle, style), {{0, 0}, {20, 10}}));
    CHECK(ToggleKnobRect(toggle, style).size.x > 0.0f);
}

// Itens: trilha e bolinha sempre; anel de foco (um item) so com foco e sem desabilitado.
void TestToggleItems() {
    Toggle toggle;
    toggle.border = {{0, 0}, {200, 30}};
    const ToggleStyle style;
    CHECK(BuildToggleItems(toggle, style, 0.0f, 0).size() == 2);
    toggle.states.focused = true;
    CHECK(BuildToggleItems(toggle, style, 0.0f, 0).size() == 3);
    toggle.states.disabled = true;
    CHECK(BuildToggleItems(toggle, style, 0.0f, 0).size() == 2);
}

} // namespace

int main() {
    TestRadioExclusiveSelection();
    TestRadioOptionsGrow();
    TestRadioClickRules();
    TestRadioRowAt();
    TestRadioKeyboard();
    TestRadioGeometry();
    TestRadioItems();
    TestToggleRules();
    TestToggleGeometry();
    TestToggleClickAreaFollowsTrack();
    TestToggleSmallArea();
    TestToggleItems();

    std::printf("radio_toggle: %d checks, %d failed\n", run, failed);
    return failed == 0 ? 0 : 1;
}
