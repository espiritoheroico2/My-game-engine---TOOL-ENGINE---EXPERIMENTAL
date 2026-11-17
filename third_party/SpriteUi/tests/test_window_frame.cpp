// Moldura de janela (WindowFrame): conteudo dentro da moldura descontando o cabecalho, botoes, aba e barra,
// raio limitado a metade do menor lado, acerto de ponteiro, rolagem e itens de pintura. Valores calculados a mao
// nos comentarios. Janela de teste: (100, 100) com 400 x 300, cabecalho de 28, borda de 1, botoes de 18 com 4 de
// espaco e 6 de recuo, aba de 140.
#include "SpriteUi/Widgets/WindowFrame.hpp"
#include "SpriteUi/Widgets/WindowFramePaint.hpp"
#include "SpriteUi/Widgets/WindowFrameTheme.hpp"

#include <cmath>
#include <cstdio>
#include <limits>

namespace {

using namespace ToolEngine::SpriteUi;
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

bool Near(float a, float b) {
    return std::fabs(a - b) <= 1.0e-4f;
}

bool SameRect(Rect a, Rect b) {
    return Near(a.position.x, b.position.x) && Near(a.position.y, b.position.y) && Near(a.size.x, b.size.x) &&
           Near(a.size.y, b.size.y);
}

const Rect kFrame{{100.0f, 100.0f}, {400.0f, 300.0f}};

WindowFrameMetrics Metrics() {
    WindowFrameMetrics m;
    m.headerHeight = 28.0f;
    m.borderWidth = 1.0f;
    m.buttonSize = 18.0f;
    m.buttonGap = 4.0f;
    m.buttonInset = 6.0f;
    m.tabWidth = 140.0f;
    return m;
}

void TestContentInsideFrame() {
    const Rect content = WindowFrameContent(kFrame, Metrics());
    // Dentro da borda (101..499 x 101..399) e abaixo do cabecalho: y comeca em 100 + 28 = 128 + 1 = 129.
    CHECK(SameRect(content, {{101.0f, 129.0f}, {398.0f, 270.0f}}));
    CHECK(content.position.y >= kFrame.position.y + Metrics().headerHeight);
    CHECK(content.position.x + content.size.x <= kFrame.position.x + kFrame.size.x);
    CHECK(content.position.y + content.size.y <= kFrame.position.y + kFrame.size.y);
}

void TestContentNeverNegative() {
    // Caixa de 20 x 20: sobram 18 x 18 dentro da borda, mas o cabecalho (28) ocupa tudo: conteudo com altura zero.
    const Rect content = WindowFrameContent({{0.0f, 0.0f}, {20.0f, 20.0f}}, Metrics());
    CHECK(content.size.x >= 0.0f);
    CHECK(content.size.y == 0.0f);
}

void TestHeaderFloatingAndDocked() {
    CHECK(SameRect(WindowFrameHeader(kFrame, Metrics(), false), {{100.0f, 100.0f}, {400.0f, 28.0f}}));
    // Encaixada: aba de 140 px no canto superior esquerdo, mesma altura.
    CHECK(SameRect(WindowFrameHeader(kFrame, Metrics(), true), {{100.0f, 100.0f}, {140.0f, 28.0f}}));
}

void TestHeaderHeightClampedToFrame() {
    // Caixa de 10 px de altura: o cabecalho nao passa da caixa.
    const Rect short_frame{{0.0f, 0.0f}, {200.0f, 10.0f}};
    CHECK(Near(WindowFrameHeader(short_frame, Metrics(), false).size.y, 10.0f));
}

void TestButtonsPlacement() {
    // Fechar: 100 + 400 - 6 - 18 = 476, topo 100 + (28 - 18) / 2 = 105. Minimizar: 476 - 4 - 18 = 454.
    CHECK(SameRect(WindowFrameButtonRect(kFrame, Metrics(), false, WindowFrameButton::Close), {{476.0f, 105.0f}, {18.0f, 18.0f}}));
    CHECK(SameRect(WindowFrameButtonRect(kFrame, Metrics(), false, WindowFrameButton::Minimize), {{454.0f, 105.0f}, {18.0f, 18.0f}}));
}

void TestDockedTabHasOnlyClose() {
    // Aba de 140 px: fechar em 100 + 140 - 6 - 18 = 216. Sem botao de minimizar.
    CHECK(SameRect(WindowFrameButtonRect(kFrame, Metrics(), true, WindowFrameButton::Close), {{216.0f, 105.0f}, {18.0f, 18.0f}}));
    CHECK(WindowFrameButtonRect(kFrame, Metrics(), true, WindowFrameButton::Minimize).size.x == 0.0f);
}

void TestButtonsDroppedWhenTooNarrow() {
    // Aba de 20 px: nao cabe o botao de 18 com 6 de recuo. Fechar some.
    WindowFrameMetrics m = Metrics();
    m.tabWidth = 20.0f;
    CHECK(WindowFrameButtonRect(kFrame, m, true, WindowFrameButton::Close).size.x == 0.0f);
}

void TestCornerRadiusClamped() {
    // Caixa 40 x 20: o raio vai ate 10 (metade do menor lado).
    const Rect small{{0.0f, 0.0f}, {40.0f, 20.0f}};
    CHECK(Near(WindowFrameCornerRadius(small, 50.0f), 10.0f));
    CHECK(Near(WindowFrameCornerRadius(small, 6.0f), 6.0f));
    CHECK(Near(WindowFrameCornerRadius(small, -3.0f), 0.0f));
    CHECK(Near(WindowFrameCornerRadius(small, std::numeric_limits<float>::quiet_NaN()), 0.0f));
}

void TestTitleRectStopsBeforeButtons() {
    // Flutuante: de 100 + 8 = 108 ate o minimizar (454) menos o espaco de 4: largura 342.
    CHECK(SameRect(WindowFrameTitleRect(kFrame, Metrics(), false), {{108.0f, 100.0f}, {342.0f, 28.0f}}));
    // Encaixada: a aba de 140 px; o fechar em 216 deixa a faixa de 108 ate 212 (104 px).
    CHECK(SameRect(WindowFrameTitleRect(kFrame, Metrics(), true), {{108.0f, 100.0f}, {104.0f, 28.0f}}));
}

void TestPartUnderPointer() {
    const WindowFrameMetrics m = Metrics();
    CHECK(WindowFramePartAt(kFrame, m, false, {480.0f, 110.0f}) == WindowFramePart::Close);
    CHECK(WindowFramePartAt(kFrame, m, false, {455.0f, 110.0f}) == WindowFramePart::Minimize);
    CHECK(WindowFramePartAt(kFrame, m, false, {300.0f, 110.0f}) == WindowFramePart::Header);
    CHECK(WindowFramePartAt(kFrame, m, false, {300.0f, 200.0f}) == WindowFramePart::Content);
    CHECK(WindowFramePartAt(kFrame, m, false, {100.0f, 200.0f}) == WindowFramePart::Border);
    CHECK(WindowFramePartAt(kFrame, m, false, {50.0f, 50.0f}) == WindowFramePart::Outside);
    CHECK(WindowFramePartAt(kFrame, m, false, {std::numeric_limits<float>::quiet_NaN(), 0.0f}) == WindowFramePart::Outside);
    // Encaixada: o ponto que estaria sobre o minimizar agora cai na faixa vazia do cabecalho.
    CHECK(WindowFramePartAt(kFrame, m, true, {455.0f, 110.0f}) == WindowFramePart::Border);
    CHECK(WindowFramePartAt(kFrame, m, true, {120.0f, 110.0f}) == WindowFramePart::Header);
}

void TestScrollClamped() {
    CHECK(Near(WindowFrameClampScroll(400.0f, 500.0f, 200.0f), 300.0f));
    CHECK(Near(WindowFrameClampScroll(-5.0f, 500.0f, 200.0f), 0.0f));
    CHECK(Near(WindowFrameClampScroll(50.0f, 100.0f, 200.0f), 0.0f));
    CHECK(Near(WindowFrameClampScroll(std::numeric_limits<float>::infinity(), 500.0f, 200.0f), 0.0f));
}

WindowFrameColors Colors() {
    return WindowFrameColors{};
}

void TestPaintItems() {
    const WindowFrameState flat{};
    const auto items = BuildWindowFrameItems(UIHandle{}, kFrame, Metrics(), flat, Colors(), 0.5f, 10);
    CHECK(!items.empty());
    // Ordem de pintura unica e crescente a partir do valor dado.
    bool ordered = true;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (items[i].paintOrder < 10 || (i > 0 && items[i].paintOrder <= items[i - 1].paintOrder)) ordered = false;
        if (items[i].zOrder != 0.5f) ordered = false;
    }
    CHECK(ordered);

    // Encaixada: sem botao de minimizar, entao tem menos itens que a flutuante.
    WindowFrameState docked{};
    docked.docked = true;
    const auto tab = BuildWindowFrameItems(UIHandle{}, kFrame, Metrics(), docked, Colors(), 0.5f, 10);
    CHECK(tab.size() < items.size());
}

void TestPaintSkipsEmptyFrame() {
    CHECK(BuildWindowFrameItems(UIHandle{}, {{0.0f, 0.0f}, {0.0f, 10.0f}}, Metrics(), {}, Colors(), 0.0f, 0).empty());
    CHECK(BuildWindowFrameItems(UIHandle{}, {{0.0f, 0.0f}, {std::numeric_limits<float>::quiet_NaN(), 10.0f}}, Metrics(), {}, Colors(), 0.0f, 0).empty());
}


// Tema: raio e borda vem do preset; cores premultiplicadas; inativa mais escura que ativa.
void TestThemeFollowsPreset() {
    CHECK(Near(MakeWindowFrameLook(StyleKind::Classic).metrics.cornerRadius, 0.0f));
    CHECK(Near(MakeWindowFrameLook(StyleKind::Modern).metrics.cornerRadius, 8.0f));
    CHECK(Near(MakeWindowFrameLook(StyleKind::Effects).metrics.cornerRadius, 12.0f));
    // Valor fora do enum cai no Modern, como em GetPreset.
    CHECK(Near(MakeWindowFrameLook(static_cast<StyleKind>(99)).metrics.cornerRadius, 8.0f));
    CHECK(Near(MakeWindowFrameLook(StyleKind::Modern).metrics.borderWidth, GetPreset(StyleKind::Modern).borderWidth));
}

void TestThemeColorsPremultiplied() {
    const WindowFrameLook look = MakeWindowFrameLook(StyleKind::Modern);
    const StylePreset preset = GetPreset(StyleKind::Modern);
    // Borda do Modern tem alfa 0,6: a cor premultiplicada vem multiplicada.
    CHECK(Near(look.colors.border.x, preset.palette.border.x * preset.palette.border.w));
    CHECK(Near(look.colors.body.w, 1.0f));
    CHECK(look.colors.headerInactive.x < look.colors.headerActive.x);
    CHECK(look.colors.headerDragging.x > look.colors.headerActive.x);
}

} // namespace

int main() {
    TestContentInsideFrame();
    TestContentNeverNegative();
    TestHeaderFloatingAndDocked();
    TestHeaderHeightClampedToFrame();
    TestButtonsPlacement();
    TestDockedTabHasOnlyClose();
    TestButtonsDroppedWhenTooNarrow();
    TestCornerRadiusClamped();
    TestTitleRectStopsBeforeButtons();
    TestPartUnderPointer();
    TestScrollClamped();
    TestPaintItems();
    TestPaintSkipsEmptyFrame();
    TestThemeFollowsPreset();
    TestThemeColorsPremultiplied();
    std::printf("WindowFrame: %d checks, %d failed\n", run, failed);
    return failed == 0 ? 0 : 1;
}
