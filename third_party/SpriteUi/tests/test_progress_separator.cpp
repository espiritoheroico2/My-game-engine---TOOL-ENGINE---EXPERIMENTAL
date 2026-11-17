// Barra de progresso (ProgressBar.hpp) e separador (Separator.hpp): faixa, valor, preenchimento, modo
// indeterminado e linha centrada. Sem janela.
#include "SpriteUi/Widgets/ProgressBar.hpp"
#include "SpriteUi/Widgets/Separator.hpp"

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

bool Near(float a, float b, float eps = 1.0e-3f) {
    return std::fabs(a - b) <= eps;
}

void TestValueIsClampedAndReportsChange() {
    ProgressBarControl bar;
    CHECK(bar.Fraction() == 0.0f);
    CHECK(bar.SetValue(0.5f));
    CHECK(Near(bar.Fraction(), 0.5f));
    CHECK(!bar.SetValue(0.5f)); // mesmo valor: nada mudou
    CHECK(bar.SetValue(2.0f));  // acima da faixa padrao [0, 1] prende em 1
    CHECK(Near(bar.Value(), 1.0f));
    CHECK(bar.SetValue(-3.0f));
    CHECK(Near(bar.Value(), 0.0f));
}

void TestNonFiniteValueIsSanitized() {
    ProgressBarControl bar;
    CHECK(bar.SetValue(0.25f));
    CHECK(bar.SetValue(std::numeric_limits<float>::quiet_NaN())); // NaN vira zero
    CHECK(Near(bar.Value(), 0.0f));
    CHECK(Near(bar.Fraction(), 0.0f));
    CHECK(bar.SetValue(std::numeric_limits<float>::infinity())); // infinito prende no maximo
    CHECK(Near(bar.Value(), 1.0f));
}

void TestRangeIsValidatedAndReclampsValue() {
    ProgressBarControl bar;
    CHECK(bar.SetValue(1.0f));
    CHECK(bar.SetRange(10.0f, 20.0f));
    CHECK(Near(bar.Value(), 10.0f)); // 1 estava abaixo da nova faixa: sobe para o minimo
    CHECK(bar.SetValue(15.0f));
    CHECK(Near(bar.Fraction(), 0.5f));

    CHECK(!bar.SetRange(5.0f, 5.0f));   // faixa vazia
    CHECK(!bar.SetRange(20.0f, 10.0f)); // invertida
    CHECK(!bar.SetRange(std::numeric_limits<float>::quiet_NaN(), 1.0f));
    CHECK(Near(bar.Minimum(), 10.0f));
    CHECK(Near(bar.Maximum(), 20.0f));
}

void TestDeterminateFillFollowsFraction() {
    ProgressBarControl bar;
    CHECK(bar.SetValue(0.25f));
    const Rect track{{30.0f, 40.0f}, {200.0f, 8.0f}};
    const Rect fill = bar.FillRect(track);
    CHECK(Near(fill.position.x, 30.0f));
    CHECK(Near(fill.position.y, 40.0f));
    CHECK(Near(fill.size.x, 50.0f));
    CHECK(Near(fill.size.y, 8.0f));
}

void TestFillNeverPassesTrackEnd() {
    ProgressBarControl bar;
    CHECK(bar.SetValue(1.0f));
    const Rect track{{0.0f, 0.0f}, {120.0f, 10.0f}};
    CHECK(Near(bar.FillRect(track).Max().x, 120.0f));
}

void TestIndeterminateSegmentCyclesAcrossTrack() {
    ProgressBarControl bar;
    bar.indeterminate = true;
    const Rect track{{100.0f, 0.0f}, {200.0f, 10.0f}};
    const float segment = 200.0f * 0.25f; // padrao: um quarto do trilho

    // Fase 0: o trecho esta inteiro a esquerda do trilho (termina exatamente na borda).
    const Rect start = bar.IndeterminateRect(track, 0.0f);
    CHECK(Near(start.size.x, segment));
    CHECK(Near(start.Max().x, 100.0f));

    // Fase 1 volta a fase 0: uma volta completa e o mesmo quadro.
    const Rect wrapped = bar.IndeterminateRect(track, 1.0f);
    CHECK(Near(wrapped.position.x, start.position.x));

    // Meia volta: o trecho anda metade do caminho (trilho mais trecho).
    const Rect middle = bar.IndeterminateRect(track, 0.5f);
    CHECK(Near(middle.position.x, 100.0f - segment + 0.5f * (200.0f + segment)));
}

void TestIndeterminateRejectsNonFinitePhase() {
    const ProgressBarControl bar;
    const Rect track{{0.0f, 0.0f}, {100.0f, 10.0f}};
    const Rect none = bar.IndeterminateRect(track, std::numeric_limits<float>::quiet_NaN());
    CHECK(none.size.x == 0.0f && none.size.y == 0.0f);
}

void TestHorizontalSeparatorIsCentred() {
    SeparatorControl sep;
    sep.axis = SeparatorAxis::Horizontal;
    sep.thickness = 2.0f;
    const Rect line = sep.LineRect({{10.0f, 20.0f}, {300.0f, 10.0f}});
    CHECK(Near(line.position.x, 10.0f));
    CHECK(Near(line.size.x, 300.0f));
    CHECK(Near(line.position.y, 24.0f)); // centro da area (25) menos metade da espessura
    CHECK(Near(line.size.y, 2.0f));
}

void TestVerticalSeparatorIsCentred() {
    SeparatorControl sep;
    sep.axis = SeparatorAxis::Vertical;
    sep.thickness = 4.0f;
    const Rect line = sep.LineRect({{0.0f, 0.0f}, {20.0f, 100.0f}});
    CHECK(Near(line.position.x, 8.0f));
    CHECK(Near(line.size.x, 4.0f));
    CHECK(Near(line.position.y, 0.0f));
    CHECK(Near(line.size.y, 100.0f));
}

void TestSeparatorRejectsBadThickness() {
    SeparatorControl sep;
    sep.thickness = 0.0f;
    CHECK(sep.LineRect({{0.0f, 0.0f}, {50.0f, 50.0f}}).size.x == 0.0f);
    sep.thickness = std::numeric_limits<float>::infinity();
    CHECK(sep.LineRect({{0.0f, 0.0f}, {50.0f, 50.0f}}).size.x == 0.0f);
}

} // namespace

int main() {
    TestValueIsClampedAndReportsChange();
    TestNonFiniteValueIsSanitized();
    TestRangeIsValidatedAndReclampsValue();
    TestDeterminateFillFollowsFraction();
    TestFillNeverPassesTrackEnd();
    TestIndeterminateSegmentCyclesAcrossTrack();
    TestIndeterminateRejectsNonFinitePhase();
    TestHorizontalSeparatorIsCentred();
    TestVerticalSeparatorIsCentred();
    TestSeparatorRejectsBadThickness();

    std::printf("ProgressBar/Separator: %d checks, %d failed\n", run, failed);
    return failed == 0 ? 0 : 1;
}
