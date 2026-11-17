// Lista (ListBox): selecao por identificador, Ctrl e Shift, teclas de navegacao, rolagem, altura que cresce e
// itens de desenho (selecao, cursor e barra de rolagem). Valores de geometria calculados a mao nos comentarios.
#include "SpriteUi/Elements/ListBox.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

using namespace ToolEngine::SpriteUi;
using namespace ToolEngine::SpriteUi::Elements;
using ToolEngine::SpriteUi::Widgets::NoControlIndex;
using ToolEngine::SpriteUi::Widgets::NoListItem;

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

bool SameColor(Vec4 a, Vec4 b) {
    return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}

PointerEvent PointerAt(PointerPhase phase, Vec2 position, bool inside) {
    PointerEvent event;
    event.phase = phase;
    event.position = position;
    event.inside = inside;
    event.pointerId = 1;
    return event;
}

// Caixa de 200 x 120: cinco linhas de 24 px visiveis (120 / 24). Itens 1..10 (conteudo de 240 px).
void FillList(ListBox& list, std::size_t count) {
    list.border = {{0, 0}, {200, 120}};
    std::vector<ListBoxItemId> ids;
    for (std::size_t i = 1; i <= count; ++i) ids.push_back(static_cast<ListBoxItemId>(i));
    list.SetItems(std::move(ids));
    list.UpdateViewport();
}

// Conta os itens de desenho com a cor dada.
std::size_t CountTint(const std::vector<Rendering::UIRenderItem>& items, Vec4 color) {
    std::size_t n = 0;
    for (const auto& item : items) {
        if (SameColor(item.tintPremultipliedLinear, color)) ++n;
    }
    return n;
}

// Item de cor dada (o primeiro), ou nulo se nao houver.
const Rendering::UIRenderItem* FindTint(const std::vector<Rendering::UIRenderItem>& items, Vec4 color) {
    for (const auto& item : items) {
        if (SameColor(item.tintPremultipliedLinear, color)) return &item;
    }
    return nullptr;
}

// Clique com Ctrl/Shift: Click mantem so o intervalo e alterna um item com Ctrl.
void TestClickSelectionRules() {
    ListBox list;
    FillList(list, 10);

    CHECK(list.Click(2, {}));
    CHECK(list.Selection().size() == 1 && list.IsSelected(2));
    CHECK(!list.Click(2, {}));                               // Mesmo item: nada muda.

    CHECK(list.Click(6, {false, true}));                     // Intervalo da ancora (2) ate 6.
    CHECK(list.Selection().size() == 5);
    CHECK(list.IsSelected(2) && list.IsSelected(4) && list.IsSelected(6) && !list.IsSelected(7));

    CHECK(list.Click(8, {true, false}));                     // Ctrl soma o item 8 e vira a ancora.
    CHECK(list.Selection().size() == 6 && list.IsSelected(8));
    CHECK(list.Click(3, {true, false}));                     // Ctrl de novo tira o item 3.
    CHECK(list.Selection().size() == 5 && !list.IsSelected(3));

    // A ancora e o ultimo clique com Ctrl (item 3): Shift+clique no 10 seleciona 3..10 e esquece o resto.
    CHECK(list.Click(10, {false, true}));
    CHECK(list.Selection().size() == 8 && list.IsSelected(3) && list.IsSelected(10) && !list.IsSelected(2));

    CHECK(!list.Click(999, {}));                             // Item inexistente: nada muda.
}

// A selecao guarda identificadores: inserir antes nao troca o item escolhido; remover tira da selecao.
void TestSelectionFollowsIdentity() {
    ListBox list;
    FillList(list, 4);
    CHECK(list.Click(3, {}));
    list.SetItems({0, 1, 2, 3, 4, 5});                       // Insercao antes do item 3.
    CHECK(list.Selection().size() == 1 && list.IsSelected(3));
    CHECK(list.ItemAt(3) == 3 && list.ItemAt(2) == 2);

    list.SetItems({0, 1, 2, 4, 5});                          // Remocao do item escolhido.
    CHECK(list.Selection().empty());
    CHECK(list.ItemAt(99) == NoListItem);
}

// Teclas: setas, Home, End, PageUp e PageDown movem o cursor; sem Shift a selecao segue o cursor; com Shift,
// o intervalo entre a ancora e o cursor.
void TestKeyboardNavigation() {
    ListBox list;
    FillList(list, 10);
    CHECK(list.Key(ListBoxKey::Down, {}) && list.IsSelected(1));  // Sem cursor: vai ao primeiro.
    CHECK(list.Key(ListBoxKey::Down, {}) && list.IsSelected(2));
    CHECK(list.Key(ListBoxKey::End, {}) && list.IsSelected(10) && list.Selection().size() == 1);
    CHECK(list.Key(ListBoxKey::Home, {}) && list.IsSelected(1));
    CHECK(!list.Key(ListBoxKey::Up, {}));                          // Na primeira linha: para.
    CHECK(list.Key(ListBoxKey::PageDown, {}) && list.IsSelected(6)); // Pagina de 5 linhas (120 / 24).
    CHECK(list.Key(ListBoxKey::PageUp, {}) && list.IsSelected(1));

    // Shift+seta com a ancora no item 1: intervalo 1..2, depois 1..10.
    CHECK(list.Key(ListBoxKey::Down, {false, true}) && list.Selection().size() == 2);
    CHECK(list.Key(ListBoxKey::End, {false, true}) && list.Selection().size() == 10);

    list.states.disabled = true;
    CHECK(!list.Key(ListBoxKey::Home, {}));
    list.states.disabled = false;

    ListBox empty;
    empty.border = {{0, 0}, {200, 120}};
    CHECK(!empty.Key(ListBoxKey::End, {}));
}

// Rolagem: ao ir ao fim, o deslocamento vai a 240 - 120 = 120. A linha 9 aparece no fundo da area.
void TestScrollAndGeometry() {
    ListBox list;
    FillList(list, 10);
    CHECK(list.Key(ListBoxKey::End, {}));
    CHECK(Near(list.view.scrollOffsetY, 120.0f));
    CHECK(list.RowAt({10, 5}) == 5);            // (5 + 120) / 24 = 5.2
    CHECK(list.RowAt({10, 115}) == 9);          // (115 + 120) / 24 = 9.8
    CHECK(list.RowAt({10, 121}) == NoControlIndex);  // Abaixo da area visivel.
    const Rect last = list.RowRect(9);          // Topo 9 * 24 - 120 = 96.
    CHECK(Near(last.position.y, 96.0f) && Near(last.size.y, 24.0f));
}

// Altura pedida: o conteudo, limitado pelo maximo; nunca negativa.
void TestPreferredHeightGrows() {
    ListBox list;
    list.border = {{0, 0}, {200, 300}};
    list.SetItems({1, 2, 3});
    CHECK(Near(list.PreferredHeight(100.0f), 72.0f));   // 3 * 24.
    CHECK(Near(list.PreferredHeight(50.0f), 50.0f));    // Limite menor que o conteudo.
    CHECK(Near(list.PreferredHeight(-5.0f), 0.0f));
    for (ListBoxItemId id = 4; id <= 50; ++id) CHECK(list.AddItem(id));
    CHECK(list.ItemCount() == 50);
    CHECK(Near(list.PreferredHeight(200.0f), 200.0f));  // Cresceu e parou no limite.
    CHECK(!list.AddItem(4));                            // Repetido.
    CHECK(!list.AddItem(NoListItem));                   // Reservado.
}

// Ponteiro: Click numa linha usa os modificadores do momento; arrastar para fora nao escolhe.
void TestPointerSelection() {
    ListBox list;
    FillList(list, 10);
    (void)list.OnPointer(PointerAt(PointerPhase::Down, {10, 60}, true), {});
    ListBoxEvents e = list.OnPointer(PointerAt(PointerPhase::Up, {10, 60}, true), {true, false});
    CHECK(e.clicked && e.selectionChanged);             // Linha 60 / 24 = 2 -> item 3, com Ctrl.
    CHECK(list.Selection().size() == 1 && list.IsSelected(3));

    (void)list.OnPointer(PointerAt(PointerPhase::Down, {10, 60}, true), {});
    (void)list.OnPointer(PointerAt(PointerPhase::Leave, {-5, 60}, false), {});
    e = list.OnPointer(PointerAt(PointerPhase::Up, {-5, 60}, false), {});
    CHECK(!e.clicked && !e.selectionChanged && list.IsSelected(3));
}

// Itens: fundo e selecao aparecem; o cursor com foco ganha quatro faixas; a barra so aparece com conteudo
// maior que a area, e o polegar tem metade da trilha (120 de 240 px visiveis).
void TestListItems() {
    const ListBoxStyle style;
    ListBox list;
    FillList(list, 10);
    CHECK(list.Click(1, {}));

    auto items = BuildListBoxItems(list, style, 0, 0.0f, 0);
    CHECK(CountTint(items, style.selected) == 1);
    CHECK(CountTint(items, style.scrollTrack) == 1 && CountTint(items, style.scrollThumb) == 1);
    CHECK(FindTint(items, style.focusRing) == nullptr);

    list.states.focused = true;
    items = BuildListBoxItems(list, style, 0, 0.0f, 0);
    CHECK(CountTint(items, style.focusRing) == 4);

    list.states.disabled = true;
    items = BuildListBoxItems(list, style, 0, 0.0f, 0);
    CHECK(CountTint(items, style.focusRing) == 0);
    list.states.disabled = false;

    // Polegar: 120 * 120 / 240 = 60 de altura; no topo ocupa a trilha de cima; no fim, a de baixo (60 px).
    const auto* thumb = FindTint(items, style.scrollThumb);
    CHECK(thumb != nullptr && Near(thumb->layout.resolvedSize.y, 60.0f) && Near(thumb->layout.resolvedPosition.y, 0.0f));
    CHECK(list.Key(ListBoxKey::End, {}));
    items = BuildListBoxItems(list, style, 0, 0.0f, 0);
    thumb = FindTint(items, style.scrollThumb);
    CHECK(thumb != nullptr && Near(thumb->layout.resolvedPosition.y, 60.0f));

    ListBox shortList;
    FillList(shortList, 3);
    items = BuildListBoxItems(shortList, style, 0, 0.0f, 0);
    CHECK(CountTint(items, style.scrollThumb) == 0);   // Conteudo cabe: sem barra.
}

} // namespace

int main() {
    TestClickSelectionRules();
    TestSelectionFollowsIdentity();
    TestKeyboardNavigation();
    TestScrollAndGeometry();
    TestPreferredHeightGrows();
    TestPointerSelection();
    TestListItems();

    std::printf("list_box: %d checks, %d failed\n", run, failed);
    return failed == 0 ? 0 : 1;
}
