// Dica de ferramenta (Tooltip.hpp): atraso, troca de alvo, transicoes e posicao dentro do canvas. Sem janela.
#include "SpriteUi/Widgets/Tooltip.hpp"

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

bool Near(float a, float b, float eps = 1.0e-4f) {
    return std::fabs(a - b) <= eps;
}

constexpr Rect kCanvas{{0.0f, 0.0f}, {400.0f, 300.0f}};
constexpr Vec2 kOffset{12.0f, 16.0f};

void TestAppearsOnlyAfterDelay() {
    TooltipControl tip(TooltipConfig{0.5f, kOffset});
    tip.Enter(7, {10.0f, 10.0f}, 1.0f);
    CHECK(!tip.Visible(1.2f));
    CHECK(tip.Visible(1.5f)); // exatamente no atraso ja aparece
    CHECK(tip.ShowAt().has_value() && Near(*tip.ShowAt(), 1.5f));
}

void TestSameTargetKeepsTimerNewTargetRestarts() {
    TooltipControl tip(TooltipConfig{0.5f, kOffset});
    tip.Enter(7, {10.0f, 10.0f}, 1.0f);
    tip.Enter(7, {50.0f, 50.0f}, 1.4f); // mesmo alvo: o relogio continua
    CHECK(tip.Visible(1.5f));
    tip.Enter(8, {50.0f, 50.0f}, 1.6f); // alvo novo: reinicia
    CHECK(!tip.Visible(1.9f));
    CHECK(tip.Visible(2.2f));
    CHECK(tip.Target() == 8);
}

void TestLeaveHidesAtOnce() {
    TooltipControl tip(TooltipConfig{0.5f, kOffset});
    tip.Enter(7, {10.0f, 10.0f}, 0.0f);
    CHECK(tip.Visible(1.0f));
    tip.Leave();
    CHECK(!tip.HasTarget());
    CHECK(!tip.Visible(1.0f));
    CHECK(!tip.ShowAt().has_value());
}

void TestAnchorIsFixedWhileHovering() {
    TooltipControl tip(TooltipConfig{0.5f, kOffset});
    tip.Enter(7, {10.0f, 10.0f}, 0.0f);
    tip.Enter(7, {90.0f, 90.0f}, 0.1f); // ponteiro andou no mesmo alvo
    CHECK(Near(tip.Anchor().x, 10.0f) && Near(tip.Anchor().y, 10.0f));
}

void TestTickReportsOnlyTransitions() {
    TooltipControl tip(TooltipConfig{0.5f, kOffset});
    CHECK(!tip.Tick(0.0f)); // sem alvo: nada mudou
    tip.Enter(7, {0.0f, 0.0f}, 0.0f);
    CHECK(!tip.Tick(0.1f));
    CHECK(tip.Tick(0.6f)); // apareceu
    CHECK(!tip.Tick(0.7f)); // continua visivel
    tip.Leave();
    CHECK(tip.Tick(0.8f)); // sumiu ao sair do alvo
    CHECK(!tip.Tick(0.9f));
}

void TestTickSeesTargetChangeWhileShown() {
    TooltipControl tip(TooltipConfig{0.5f, kOffset});
    tip.Enter(7, {0.0f, 0.0f}, 0.0f);
    CHECK(tip.Tick(1.0f));
    tip.Enter(8, {0.0f, 0.0f}, 1.0f); // troca de alvo esconde e reinicia
    CHECK(tip.Tick(1.1f));
    CHECK(!tip.Visible(1.1f));
}

void TestPlaceBelowAndRightOfPointer() {
    const Rect placed = PlaceTooltip({100.0f, 50.0f}, {80.0f, 20.0f}, kCanvas, kOffset);
    CHECK(Near(placed.position.x, 112.0f));
    CHECK(Near(placed.position.y, 66.0f));
    CHECK(Near(placed.size.x, 80.0f) && Near(placed.size.y, 20.0f));
}

void TestPlaceFlipsAboveNearBottom() {
    const Rect placed = PlaceTooltip({100.0f, 290.0f}, {80.0f, 20.0f}, kCanvas, kOffset);
    CHECK(placed.Max().y <= 290.0f - kOffset.y + 1.0e-4f); // inteira acima do ponteiro
    CHECK(placed.position.y >= 0.0f);
}

void TestPlaceStaysInsideCanvasOnRight() {
    const Rect placed = PlaceTooltip({390.0f, 20.0f}, {80.0f, 20.0f}, kCanvas, kOffset);
    CHECK(Near(placed.Max().x, 400.0f)); // encostada na borda direita
    CHECK(placed.position.x >= 0.0f);
}

void TestPlaceOversizedPinnedToCorner() {
    const Rect placed = PlaceTooltip({200.0f, 150.0f}, {500.0f, 400.0f}, kCanvas, kOffset);
    CHECK(Near(placed.position.x, 0.0f));
    CHECK(Near(placed.position.y, 0.0f));
}

void TestPlaceRejectsNonFiniteInput() {
    const Rect placed = PlaceTooltip({NAN, 0.0f}, {80.0f, 20.0f}, kCanvas, kOffset);
    CHECK(Near(placed.position.x, 0.0f) && Near(placed.position.y, 0.0f));
    CHECK(Near(placed.size.x, 0.0f) && Near(placed.size.y, 0.0f));
}

} // namespace

int main() {
    TestAppearsOnlyAfterDelay();
    TestSameTargetKeepsTimerNewTargetRestarts();
    TestLeaveHidesAtOnce();
    TestAnchorIsFixedWhileHovering();
    TestTickReportsOnlyTransitions();
    TestTickSeesTargetChangeWhileShown();
    TestPlaceBelowAndRightOfPointer();
    TestPlaceFlipsAboveNearBottom();
    TestPlaceStaysInsideCanvasOnRight();
    TestPlaceOversizedPinnedToCorner();
    TestPlaceRejectsNonFiniteInput();

    std::printf("tooltip: %d checks, %d failed\n", run, failed);
    return failed == 0 ? 0 : 1;
}
