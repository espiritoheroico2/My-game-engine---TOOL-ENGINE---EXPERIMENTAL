// Divisor (Splitter): area de pegada, arraste nos dois sentidos, limites por painel, cancelamento e cursor.
// Executavel proprio, sem o harness principal.

#include "SpriteUi/Elements/Splitter.hpp"

#include <cstdio>

namespace {

using namespace ToolEngine::SpriteUi;
using namespace ToolEngine::SpriteUi::Elements;

int run = 0;
int failed = 0;
const char* current = "";

void Check(bool ok, const char* expr) {
    ++run;
    if (!ok) {
        ++failed;
        std::fprintf(stderr, "[FAIL] %s: %s\n", current, expr);
    }
}
#define CHECK(expr) Check((expr), #expr)

PointerEvent Ev(PointerPhase phase, Vec2 p, bool inside = true) {
    PointerEvent e;
    e.phase = phase;
    e.position = p;
    e.inside = inside;
    e.pointerId = 1;
    return e;
}

// Divisor vertical na coluna x = 200, com a caixa de 8 px (196..204) e altura 300. Element nao e copiavel, entao
// o divisor e preparado no lugar.
void SetupVertical(Splitter& s, float leading = 200.0f) {
    s.axis = SplitAxis::Vertical;
    s.border = {{196.0f, 0.0f}, {8.0f, 300.0f}};
    s.leadingSize = leading;
    s.minLeading = 100.0f;
    s.maxLeading = 260.0f;
}

void TestHitAreaIsAtLeastEightPixelsCenteredOnTheLine() {
    current = "TestHitAreaIsAtLeastEightPixelsCenteredOnTheLine";
    Splitter s;
    SetupVertical(s);
    const Rect line = s.LineRect();
    CHECK(line.size.x == 2.0f);
    CHECK(line.position.x == 199.0f);            // linha de 2 px no centro (196 + 3)
    const Rect hit = s.HitRect();
    CHECK(hit.size.x >= 8.0f);
    CHECK(hit.position.x <= 196.0f && hit.position.x + hit.size.x >= 204.0f);
    // Caixa de layout fina (2 px): a pegada continua com 8 px, centrada na linha.
    Splitter thin;
    SetupVertical(thin);
    thin.border = {{199.0f, 0.0f}, {2.0f, 300.0f}};
    const Rect thinHit = thin.HitRect();
    CHECK(thinHit.size.x == 8.0f);
    CHECK(thinHit.position.x == 196.0f);
    // Um lado da linha tem pelo menos 4 px de pegada a partir do centro.
    const float centerX = line.position.x + 0.5f * line.size.x;
    CHECK(centerX - hit.position.x >= 4.0f && hit.position.x + hit.size.x - centerX >= 4.0f);
}

void TestDragRightGrowsLeadingPane() {
    current = "TestDragRightGrowsLeadingPane";
    Splitter s;
    SetupVertical(s, 200.0f);
    CHECK(s.OnPointer(Ev(PointerPhase::Down, {200.0f, 150.0f})).dragStarted);
    const SplitterEvents e = s.OnPointer(Ev(PointerPhase::Move, {230.0f, 150.0f}));
    CHECK(e.resized);
    CHECK(s.leadingSize == 230.0f);
    CHECK(s.OnPointer(Ev(PointerPhase::Up, {230.0f, 150.0f})).dragEnded);
}

void TestDragLeftShrinksLeadingPane() {
    current = "TestDragLeftShrinksLeadingPane";
    Splitter s;
    SetupVertical(s, 200.0f);
    (void)s.OnPointer(Ev(PointerPhase::Down, {200.0f, 150.0f}));
    const SplitterEvents e = s.OnPointer(Ev(PointerPhase::Move, {170.0f, 150.0f}));
    CHECK(e.resized);
    CHECK(s.leadingSize == 170.0f);
    CHECK(s.OnPointer(Ev(PointerPhase::Up, {170.0f, 150.0f})).dragEnded);
}

void TestHorizontalSplitterWorksBothWays() {
    current = "TestHorizontalSplitterWorksBothWays";
    Splitter s;
    s.axis = SplitAxis::Horizontal;
    s.border = {{0.0f, 196.0f}, {300.0f, 8.0f}};
    s.leadingSize = 150.0f;
    s.minLeading = 50.0f;
    s.maxLeading = 400.0f;
    (void)s.OnPointer(Ev(PointerPhase::Down, {50.0f, 200.0f}));
    (void)s.OnPointer(Ev(PointerPhase::Move, {50.0f, 240.0f}));
    CHECK(s.leadingSize == 190.0f);              // para baixo aumenta o painel de cima
    (void)s.OnPointer(Ev(PointerPhase::Move, {50.0f, 150.0f}));
    CHECK(s.leadingSize == 100.0f);              // para cima diminui
    (void)s.OnPointer(Ev(PointerPhase::Up, {50.0f, 150.0f}));
}

void TestLeadingLimitsClampBothDirections() {
    current = "TestLeadingLimitsClampBothDirections";
    Splitter s;
    SetupVertical(s, 200.0f);
    (void)s.OnPointer(Ev(PointerPhase::Down, {200.0f, 150.0f}));
    (void)s.OnPointer(Ev(PointerPhase::Move, {900.0f, 150.0f}));
    CHECK(s.leadingSize == 260.0f);              // maximo do painel da frente
    (void)s.OnPointer(Ev(PointerPhase::Move, {-900.0f, 150.0f}));
    CHECK(s.leadingSize == 100.0f);              // minimo do painel da frente
    (void)s.OnPointer(Ev(PointerPhase::Up, {-900.0f, 150.0f}));
}

void TestTrailingLimitsUseTotalSize() {
    current = "TestTrailingLimitsUseTotalSize";
    Splitter s;
    SetupVertical(s, 200.0f);
    s.totalSize = 400.0f;
    s.minTrailing = 120.0f;                      // painel de tras nao pode ser menor que 120
    s.maxTrailing = 300.0f;                      // nem maior que 300
    // Limite de tras -> painel da frente entre 100 (total - max de tras) e 280 (total - min de tras).
    CHECK(s.ClampedLeading(10.0f) == 100.0f);
    CHECK(s.ClampedLeading(999.0f) == 260.0f);   // max do painel da frente ainda vale (260 < 280)
    s.maxLeading = 400.0f;
    CHECK(s.ClampedLeading(999.0f) == 280.0f);
    // Limites incompativeis: o divisor fica onde esta.
    s.minLeading = 350.0f;
    s.maxLeading = 360.0f;
    CHECK(s.ClampedLeading(300.0f) == s.leadingSize);
}

void TestClickWithoutMoveDoesNotResize() {
    current = "TestClickWithoutMoveDoesNotResize";
    Splitter s;
    SetupVertical(s, 200.0f);
    (void)s.OnPointer(Ev(PointerPhase::Down, {200.0f, 150.0f}));
    const SplitterEvents e = s.OnPointer(Ev(PointerPhase::Up, {200.0f, 150.0f}));
    CHECK(!e.resized);
    CHECK(s.leadingSize == 200.0f);
}

void TestCancelRestoresSizeBeforeDrag() {
    current = "TestCancelRestoresSizeBeforeDrag";
    Splitter s;
    SetupVertical(s, 200.0f);
    (void)s.OnPointer(Ev(PointerPhase::Down, {200.0f, 150.0f}));
    (void)s.OnPointer(Ev(PointerPhase::Move, {240.0f, 150.0f}));
    CHECK(s.leadingSize == 240.0f);
    const SplitterEvents e = s.OnPointer(Ev(PointerPhase::Cancel, {240.0f, 150.0f}));
    CHECK(e.resized && e.dragEnded);
    CHECK(s.leadingSize == 200.0f);
    CHECK(!s.Dragging());
}

void TestCursorShowsResizeForTheAxis() {
    current = "TestCursorShowsResizeForTheAxis";
    Splitter v;
    SetupVertical(v);
    CHECK(v.Cursor() == SplitterCursor::Arrow);
    (void)v.OnPointer(Ev(PointerPhase::Enter, {200.0f, 10.0f}));
    CHECK(v.Cursor() == SplitterCursor::ResizeHorizontal);
    (void)v.OnPointer(Ev(PointerPhase::Leave, {200.0f, 10.0f}));
    CHECK(v.Cursor() == SplitterCursor::Arrow);
    Splitter h;
    h.axis = SplitAxis::Horizontal;
    (void)h.OnPointer(Ev(PointerPhase::Enter, {0.0f, 0.0f}));
    CHECK(h.Cursor() == SplitterCursor::ResizeVertical);
}

void TestPressOutsideDoesNotStartDrag() {
    current = "TestPressOutsideDoesNotStartDrag";
    Splitter s;
    SetupVertical(s, 200.0f);
    CHECK(!s.OnPointer(Ev(PointerPhase::Down, {10.0f, 10.0f}, false)).dragStarted);
    (void)s.OnPointer(Ev(PointerPhase::Move, {300.0f, 10.0f}));
    CHECK(s.leadingSize == 200.0f);
}

void TestBuildItemsUsesHotColorWhileHovered() {
    current = "TestBuildItemsUsesHotColorWhileHovered";
    Splitter s;
    SetupVertical(s);
    SplitterStyle style;
    const auto idle = BuildSplitterItems(s, style, 0.0f, 0);
    (void)s.OnPointer(Ev(PointerPhase::Enter, {200.0f, 10.0f}));
    const auto hot = BuildSplitterItems(s, style, 0.0f, 0);
    CHECK(idle.size() == 1 && hot.size() == 1);
    if (idle.size() == 1 && hot.size() == 1) {
        CHECK(hot[0].tintPremultipliedLinear.x != idle[0].tintPremultipliedLinear.x);
        CHECK(hot[0].layout.resolvedSize.x == 2.0f);
    }
}

} // namespace

int main() {
    TestHitAreaIsAtLeastEightPixelsCenteredOnTheLine();
    TestDragRightGrowsLeadingPane();
    TestDragLeftShrinksLeadingPane();
    TestHorizontalSplitterWorksBothWays();
    TestLeadingLimitsClampBothDirections();
    TestTrailingLimitsUseTotalSize();
    TestClickWithoutMoveDoesNotResize();
    TestCancelRestoresSizeBeforeDrag();
    TestCursorShowsResizeForTheAxis();
    TestPressOutsideDoesNotStartDrag();
    TestBuildItemsUsesHotColorWhileHovered();
    std::printf("splitter: %d/%d checks passed\n", run - failed, run);
    return failed == 0 ? 0 : 1;
}
