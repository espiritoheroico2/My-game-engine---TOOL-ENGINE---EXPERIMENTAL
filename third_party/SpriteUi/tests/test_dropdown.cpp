// Caixa de combinacao (Dropdown): abrir e fechar, escolher, Escape e clique fora, posicao abaixo ou acima,
// encaixe na janela, lista que cresce com rolagem e itens de desenho. Valores calculados a mao nos comentarios.
#include "SpriteUi/Elements/Dropdown.hpp"

#include <cmath>
#include <cstdio>

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

// Janela 300 x 300. Botao de 200 x 30 em (0, 20): a lista cabe abaixo (espaco de 250 px).
void SetUpDropdown(Dropdown& dd, std::size_t count) {
    dd.border = {{0, 20}, {200, 30}};
    dd.states.focused = true;
    for (std::size_t i = 0; i < count; ++i) dd.control.options.push_back("opcao " + std::to_string(i));
}

const Rect kWindow{{0, 0}, {300, 300}};

// Clique completo (Down e Up dentro) no botao: abre ou fecha.
DropdownEvents ClickButton(Dropdown& dd) {
    (void)dd.OnPointer(PointerAt(PointerPhase::Down, {10, 25}, true), kWindow);
    return dd.OnPointer(PointerAt(PointerPhase::Up, {10, 25}, true), kWindow);
}

std::size_t CountTint(const std::vector<Rendering::UIRenderItem>& items, Vec4 color) {
    std::size_t n = 0;
    for (const auto& item : items) {
        const Vec4 c = item.tintPremultipliedLinear;
        if (c.x == color.x && c.y == color.y && c.z == color.z && c.w == color.w) ++n;
    }
    return n;
}

// Clique no botao abre a lista; o segundo clique fecha sem escolher.
void TestPointerOpenAndClose() {
    Dropdown dd;
    SetUpDropdown(dd, 3);
    DropdownEvents e = ClickButton(dd);
    CHECK(e.opened && dd.control.open);
    // Abaixo: topo do botao + altura (50), altura = 3 * 24 = 72.
    CHECK(Near(dd.Popup().position.y, 50.0f) && Near(dd.Popup().size.y, 72.0f));

    e = ClickButton(dd);
    CHECK(e.closed && !dd.control.open && dd.Popup().size.y == 0.0f);
    CHECK(dd.control.selectedIndex == NoControlIndex);
}

// Teclado: Seta para baixo abre, desce o destaque, Enter escolhe uma vez e fecha; Escape fecha sem mudar.
void TestKeyboardChooseAndEscape() {
    Dropdown dd;
    SetUpDropdown(dd, 3);
    dd.control.selectedIndex = 0;
    CHECK(dd.KeyPressed(DropdownKey::Down, kWindow).opened);  // Fechada: abre no destaque da escolhida.
    CHECK(dd.control.highlightedIndex == 0);
    CHECK(!dd.KeyPressed(DropdownKey::Down, kWindow).valueChanged);
    CHECK(dd.control.highlightedIndex == 1);
    DropdownEvents e = dd.KeyPressed(DropdownKey::Enter, kWindow);
    CHECK(e.valueChanged && e.closed && dd.control.selectedIndex == 1 && !dd.control.open);

    CHECK(dd.KeyPressed(DropdownKey::Down, kWindow).opened);
    e = dd.KeyPressed(DropdownKey::Escape, kWindow);
    CHECK(e.closed && !e.valueChanged && dd.control.selectedIndex == 1);
}

// Clique numa linha da lista escolhe aquela opcao; fora da lista, nada acontece.
void TestChooseAtRow() {
    Dropdown dd;
    SetUpDropdown(dd, 3);
    (void)ClickButton(dd);                                    // Lista em y 50..122.
    DropdownEvents e = dd.ChooseAt({10, 10}, kWindow);        // Fora da lista: nada.
    CHECK(!e.closed && !e.valueChanged && dd.control.open);
    e = dd.ChooseAt({10, 80}, kWindow);                       // (80 - 50) / 24 = linha 1.
    CHECK(e.valueChanged && e.closed && dd.control.selectedIndex == 1 && !dd.control.open);
}

// Clique fora fecha sem escolher.
void TestCloseOutside() {
    Dropdown dd;
    SetUpDropdown(dd, 3);
    dd.control.selectedIndex = 2;
    (void)ClickButton(dd);
    DropdownEvents e = dd.CloseOutside();
    CHECK(e.closed && !dd.control.open && dd.control.selectedIndex == 2);
}

// Abaixo quando cabe; acima quando nao cabe abaixo; a lista nunca sai da janela.
void TestPlacementBelowAndAbove() {
    Dropdown below;
    SetUpDropdown(below, 3);
    (void)ClickButton(below);
    CHECK(Near(below.Popup().position.y, 50.0f));

    Dropdown above;
    SetUpDropdown(above, 3);
    above.border = {{0, 280}, {200, 20}};                    // Abaixo sobra 0; acima sobra 280.
    CHECK(above.KeyPressed(DropdownKey::Down, kWindow).opened);
    // Acima: topo do botao (280) menos a altura (72) = 208.
    CHECK(Near(above.Popup().position.y, 208.0f) && Near(above.Popup().size.y, 72.0f));
}

// Horizontal: a lista fica dentro da janela (botao em x 150 de 200 de largura, janela de 300: x maximo 100).
void TestPopupStaysInsideWindowHorizontally() {
    Dropdown dd;
    SetUpDropdown(dd, 3);
    dd.border = {{150, 20}, {200, 30}};
    CHECK(dd.KeyPressed(DropdownKey::Down, kWindow).opened);
    CHECK(Near(dd.Popup().position.x, 100.0f) && Near(dd.Popup().size.x, 200.0f));
}

// Sessenta opcoes (1440 px) nao cabem: a lista usa o espaco abaixo (250 px) e rola. Sem limite fixo.
void TestListGrowsAndScrolls() {
    Dropdown dd;
    SetUpDropdown(dd, 60);
    dd.control.selectedIndex = 0;
    CHECK(dd.KeyPressed(DropdownKey::Down, kWindow).opened);
    CHECK(Near(dd.Popup().size.y, 250.0f));
    CHECK(Near(dd.popupScrollY, 0.0f));
    for (int i = 0; i < 40; ++i) (void)dd.KeyPressed(DropdownKey::Down, kWindow);
    CHECK(dd.control.highlightedIndex == 40);
    // Linha 40 termina em 41 * 24 = 984: a rolagem para em 984 - 250 = 734.
    CHECK(Near(dd.popupScrollY, 734.0f));
    for (int i = 0; i < 40; ++i) (void)dd.KeyPressed(DropdownKey::Up, kWindow);
    CHECK(dd.control.highlightedIndex == 0 && Near(dd.popupScrollY, 0.0f));
}

// Desabilitado: nao abre e nao reage a teclas.
void TestDisabledIgnoresUser() {
    Dropdown dd;
    SetUpDropdown(dd, 3);
    dd.states.disabled = true;
    CHECK(!ClickButton(dd).opened && !dd.control.open);
    CHECK(!dd.KeyPressed(DropdownKey::Down, kWindow).opened);
    CHECK(dd.Popup().size.y == 0.0f);
}

// Itens: fechada = fundo e seta; com foco ganha o contorno; aberta = fundo, destaque, escolhida e contorno.
void TestDropdownItems() {
    const DropdownStyle style;
    Dropdown dd;
    SetUpDropdown(dd, 3);
    dd.states.focused = false;
    CHECK(BuildDropdownButtonItems(dd, style, 0.0f, 0).size() == 3);
    CHECK(BuildDropdownPopupItems(dd, style, 0, 0.0f, 0).empty());

    dd.states.focused = true;
    CHECK(BuildDropdownButtonItems(dd, style, 0.0f, 0).size() == 7);

    dd.control.selectedIndex = 0;
    CHECK(dd.KeyPressed(DropdownKey::Down, kWindow).opened);   // Destaque = escolhida = 0.
    auto popup = BuildDropdownPopupItems(dd, style, 0, 0.0f, 0);
    CHECK(popup.size() == 6);                                  // Fundo, destaque, contorno (4).
    CHECK(CountTint(popup, style.highlight) == 1);

    (void)dd.KeyPressed(DropdownKey::Down, kWindow);           // Destaque na 1; a escolhida (0) continua marcada.
    popup = BuildDropdownPopupItems(dd, style, 0, 0.0f, 0);
    CHECK(popup.size() == 7);
    CHECK(CountTint(popup, style.selectedRow) == 1 && CountTint(popup, style.highlight) == 1);
}

} // namespace

int main() {
    TestPointerOpenAndClose();
    TestKeyboardChooseAndEscape();
    TestChooseAtRow();
    TestCloseOutside();
    TestPlacementBelowAndAbove();
    TestPopupStaysInsideWindowHorizontally();
    TestListGrowsAndScrolls();
    TestDisabledIgnoresUser();
    TestDropdownItems();

    std::printf("dropdown: %d checks, %d failed\n", run, failed);
    return failed == 0 ? 0 : 1;
}
