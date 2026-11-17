// Barra de rolagem (ScrollBar.hpp): polegar, pagina, arrasto, roda e eixo bloqueado. Sem janela.
#include "SpriteUi/Widgets/ScrollBar.hpp"

#include <cmath>
#include <cstdio>

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

bool Near(float a, float b, float eps = 1.0e-3f) {
    return std::fabs(a - b) <= eps;
}

// Vertical: trilho x=0..10, y=0..200. Conteudo 400, viewport 100 -> polegar 50, curso 150, maximo 300.
UIScrollViewComponent VerticalView(float offsetY = 0.0f) {
    UIScrollViewComponent view;
    view.contentSize = {10.0f, 400.0f};
    view.viewportSize = {10.0f, 100.0f};
    view.allowScrollY = true;
    view.scrollOffset = {0.0f, offsetY};
    return view;
}

ScrollBarControl VerticalBar() {
    ScrollBarControl bar;
    bar.axis = ScrollAxis::Vertical;
    bar.SetTrack({{0.0f, 0.0f}, {10.0f, 200.0f}});
    return bar;
}

void TestThumbSizeAndPosition() {
    const ScrollBarControl bar = VerticalBar();
    UIScrollViewComponent view = VerticalView(0.0f);
    CHECK(bar.HasOverflow(view));
    CHECK(Near(bar.ThumbRect(view).size.y, 50.0f));
    CHECK(Near(bar.ThumbRect(view).position.y, 0.0f));

    view.scrollOffset.y = 150.0f; // metade do maximo (300): polegar a meio curso (150 * 0.5 = 75)
    CHECK(Near(bar.ThumbRect(view).position.y, 75.0f));
    view.scrollOffset.y = 300.0f;
    CHECK(Near(bar.ThumbRect(view).position.y + bar.ThumbRect(view).size.y, 200.0f));
}

void TestThumbHasMinimumLength() {
    ScrollBarControl bar = VerticalBar();
    UIScrollViewComponent view = VerticalView();
    view.contentSize.y = 100000.0f; // 200 * 100 / 100000 = 0.2 -> sobe para o minimo
    CHECK(Near(bar.ThumbRect(view).size.y, bar.minThumbLength));
}

void TestThumbNeverShorterThanSixteenPixels() {
    ScrollBarControl bar = VerticalBar();
    UIScrollViewComponent view = VerticalView();
    view.contentSize.y = 100000.0f;
    CHECK(Near(bar.minThumbLength, 16.0f));
    bar.minThumbLength = 2.0f; // configuracao menor que o piso nao vale
    CHECK(Near(bar.ThumbRect(view).size.y, 16.0f));
}

void TestNoOverflowMeansNoScrolling() {
    ScrollBarControl bar = VerticalBar();
    UIScrollViewComponent view = VerticalView();
    view.contentSize.y = 80.0f; // cabe na viewport
    CHECK(!bar.HasOverflow(view));
    CHECK(Near(bar.ThumbRect(view).size.y, 200.0f)); // polegar ocupa o trilho
    CHECK(!bar.PointerDown({5.0f, 150.0f}, view));
    CHECK(!bar.Wheel(40.0f, view));
    CHECK(view.scrollOffset.y == 0.0f);
}

void TestTrackClickPagesTowardPointer() {
    ScrollBarControl bar = VerticalBar();
    UIScrollViewComponent view = VerticalView(0.0f);
    CHECK(bar.PointerDown({5.0f, 190.0f}, view)); // abaixo do polegar (0..50): uma pagina para baixo
    CHECK(Near(view.scrollOffset.y, 100.0f));
    CHECK(!bar.Dragging());
    CHECK(bar.PointerDown({5.0f, 150.0f}, view)); // no trilho, acima do polegar atual
    CHECK(Near(view.scrollOffset.y, 200.0f));
    CHECK(bar.PointerDown({5.0f, 10.0f}, view)); // para cima do polegar (agora perto do meio)
    CHECK(Near(view.scrollOffset.y, 100.0f));
}

void TestDragKeepsGrabPoint() {
    ScrollBarControl bar = VerticalBar();
    UIScrollViewComponent view = VerticalView(0.0f);
    CHECK(!bar.PointerDown({5.0f, 25.0f}, view)); // agarra o polegar no meio (0..50)
    CHECK(bar.Dragging());
    // Curso do polegar = 150 e curso do conteudo = 300: mover 30 px no polegar = 60 px de rolagem.
    CHECK(bar.PointerMove({5.0f, 55.0f}, view));
    CHECK(Near(view.scrollOffset.y, 60.0f));
    CHECK(bar.PointerMove({5.0f, 1000.0f}, view)); // arrasto alem do fim: para no maximo
    CHECK(Near(view.scrollOffset.y, 300.0f));
    CHECK(!bar.PointerMove({5.0f, 1000.0f}, view)); // ja no maximo
}

void TestPointerUpEndsDrag() {
    ScrollBarControl bar = VerticalBar();
    UIScrollViewComponent view = VerticalView(0.0f);
    static_cast<void>(bar.PointerDown({5.0f, 25.0f}, view));  // Resultado nao usado: o teste so mede o efeito no estado.
    bar.PointerUp();
    CHECK(!bar.Dragging());
    CHECK(!bar.PointerMove({5.0f, 150.0f}, view));
    CHECK(view.scrollOffset.y == 0.0f);
}

void TestWheelScrollsAndClamps() {
    ScrollBarControl bar = VerticalBar();
    UIScrollViewComponent view = VerticalView(0.0f);
    CHECK(bar.Wheel(40.0f, view));
    CHECK(Near(view.scrollOffset.y, 40.0f));
    CHECK(bar.Wheel(-100.0f, view));
    CHECK(view.scrollOffset.y == 0.0f); // nao passa do topo
    CHECK(!bar.Wheel(-10.0f, view));
    CHECK(!bar.Wheel(NAN, view));
}

void TestHorizontalAxis() {
    ScrollBarControl bar;
    bar.axis = ScrollAxis::Horizontal;
    bar.SetTrack({{0.0f, 0.0f}, {300.0f, 10.0f}});
    UIScrollViewComponent view;
    view.contentSize = {600.0f, 10.0f};
    view.viewportSize = {300.0f, 10.0f};
    view.allowScrollX = true;
    view.allowScrollY = false;
    CHECK(Near(bar.ThumbRect(view).size.x, 150.0f));
    CHECK(bar.Wheel(50.0f, view));
    CHECK(Near(view.scrollOffset.x, 50.0f));
    CHECK(view.scrollOffset.y == 0.0f);
    CHECK(bar.PointerDown({200.0f, 5.0f}, view)); // direita do polegar: pagina de 300
    CHECK(Near(view.scrollOffset.x, 300.0f));
}

void TestBlockedAxisHasNoBar() {
    ScrollBarControl bar = VerticalBar();
    UIScrollViewComponent view = VerticalView(0.0f);
    view.allowScrollY = false;
    CHECK(!bar.HasOverflow(view));
    CHECK(!bar.Wheel(40.0f, view));
}

void TestDisabledIgnoresInput() {
    ScrollBarControl bar = VerticalBar();
    bar.disabled = true;
    UIScrollViewComponent view = VerticalView(0.0f);
    CHECK(!bar.PointerDown({5.0f, 190.0f}, view));
    CHECK(!bar.Wheel(40.0f, view));
    CHECK(view.scrollOffset.y == 0.0f);
}

} // namespace

int main() {
    TestThumbSizeAndPosition();
    TestThumbHasMinimumLength();
    TestThumbNeverShorterThanSixteenPixels();
    TestNoOverflowMeansNoScrolling();
    TestTrackClickPagesTowardPointer();
    TestDragKeepsGrabPoint();
    TestPointerUpEndsDrag();
    TestWheelScrollsAndClamps();
    TestHorizontalAxis();
    TestBlockedAxisHasNoBar();
    TestDisabledIgnoresInput();

    std::printf("scroll bar: %d checks, %d failed\n", run, failed);
    return failed == 0 ? 0 : 1;
}
