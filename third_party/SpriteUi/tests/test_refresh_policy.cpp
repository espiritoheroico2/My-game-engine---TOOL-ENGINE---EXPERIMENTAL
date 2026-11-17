// Politica de atualizacao por janela (UIRefreshPolicy). Relogio injetado: o tempo vem do teste, nunca do sistema.
// Executavel proprio, sem o harness principal.

#include "SpriteUi/Core/RefreshPolicy.hpp"

#include <cmath>
#include <cstdio>
#include <limits>

namespace {

using namespace ToolEngine::SpriteUi;

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

UIDirtyState Clean() {
    UIDirtyState s;
    (void)s.Consume();   // estado inicial de uma janela ja construida: nada sujo
    return s;
}

// Roda `frames` quadros a `fps` (relogio injetado) e devolve quantas reconstrucoes aconteceram.
// `dirtyAt` marca a janela suja no quadro indicado (-1 = nunca).
int RunFrames(UIRefreshPolicy& policy, UIDirtyState& dirty, int frames, double fps, int dirtyAt = -1) {
    int rebuilds = 0;
    for (int k = 0; k < frames; ++k) {
        if (k == dirtyAt) dirty.Mark(DirtyDrawData);
        const double now = static_cast<double>(k) / fps;
        if (policy.TryRebuild(now, dirty)) ++rebuilds;
    }
    return rebuilds;
}

void TestDefaultsAndRateSanitizing() {
    current = "TestDefaultsAndRateSanitizing";
    UIRefreshPolicy p;
    CHECK(p.RateHz() == 60.0f);
    p.SetRateHz(4.0f);
    CHECK(p.RateHz() == 4.0f);
    p.SetRateHz(std::numeric_limits<float>::quiet_NaN());
    CHECK(p.RateHz() == 0.0f);
    p.SetRateHz(-3.0f);
    CHECK(p.RateHz() == 0.0f);
    p.SetRateHz(std::numeric_limits<float>::infinity());
    CHECK(p.RateHz() == 0.0f);
}

void TestFreshWindowRebuildsOnFirstFrame() {
    current = "TestFreshWindowRebuildsOnFirstFrame";
    UIRefreshPolicy p{0.0f};
    UIDirtyState fresh;                                  // estado padrao: tudo sujo
    CHECK(p.ShouldRebuild(0.0, fresh));
    CHECK(p.TryRebuild(0.0, fresh));
    CHECK(fresh.bits == 0);                              // a reconstrucao limpa o estado sujo
    CHECK(!p.TryRebuild(0.0, fresh));                    // mesmo instante: nao reconstroi de novo
}

void TestSixHundredFramesAt4HzRebuildsForty() {
    current = "TestSixHundredFramesAt4HzRebuildsForty";
    UIRefreshPolicy p{4.0f};
    UIDirtyState d = Clean();
    // Primeiro quadro: a janela nunca foi reconstruida, entao conta. Depois, a cada 15 quadros (0,25 s).
    CHECK(RunFrames(p, d, 600, 60.0) == 40);
}

void TestSixHundredFramesAt60HzRebuildsEveryFrame() {
    current = "TestSixHundredFramesAt60HzRebuildsEveryFrame";
    UIRefreshPolicy p;                                   // padrao: 60 Hz
    UIDirtyState d = Clean();
    CHECK(RunFrames(p, d, 600, 60.0) == 600);
}

void TestRateZeroRebuildsOnlyOnceWhenClean() {
    current = "TestRateZeroRebuildsOnlyOnceWhenClean";
    UIRefreshPolicy p{0.0f};
    UIDirtyState d;
    const int total = RunFrames(p, d, 600, 60.0);
    CHECK(total == 1);                                   // so o primeiro quadro (estado inicial sujo)
    UIDirtyState d2 = Clean();
    CHECK(RunFrames(p, d2, 600, 60.0) == 0);             // janela sem mudanca nenhuma: zero reconstrucoes
}

void TestRateZeroRebuildsWhenDirtied() {
    current = "TestRateZeroRebuildsWhenDirtied";
    UIRefreshPolicy p{0.0f};
    UIDirtyState d = Clean();
    CHECK(RunFrames(p, d, 600, 60.0, 100) == 1);         // suja no quadro 100: uma reconstrucao, sem esperar taxa
    CHECK(d.bits == 0);
}

void TestDirtyWindowRebuildsOffCycle() {
    current = "TestDirtyWindowRebuildsOffCycle";
    // Suja fora do ciclo de 4 Hz: reconstroi no mesmo quadro (regra "suja OU intervalo passou").
    UIRefreshPolicy p{4.0f};
    UIDirtyState d = Clean();
    CHECK(p.TryRebuild(0.0, d));                         // primeira reconstrucao
    d.Mark(DirtyDrawData);
    CHECK(p.TryRebuild(1.0 / 60.0, d));                  // quadro 1: suja, reconstroi
    CHECK(!p.ShouldRebuild(2.0 / 60.0, Clean()));        // limpa logo depois: espera o intervalo
}

void TestNeverTwiceAtTheSameInstant() {
    current = "TestNeverTwiceAtTheSameInstant";
    UIRefreshPolicy p{60.0f};
    UIDirtyState d = Clean();
    d.Mark(DirtyMeasure);
    CHECK(p.TryRebuild(1.5, d));
    d.Mark(DirtyMeasure);                                // suja de novo, mas no mesmo instante
    CHECK(!p.ShouldRebuild(1.5, d));
    CHECK(!p.TryRebuild(1.5, d));
    CHECK(d.bits != 0);                                  // nada foi consumido
}

void TestIntervalBoundaryAt4Hz() {
    current = "TestIntervalBoundaryAt4Hz";
    UIRefreshPolicy p{4.0f};
    p.MarkRebuilt(0.0);
    const UIDirtyState c = Clean();
    CHECK(!p.ShouldRebuild(0.2499, c));
    CHECK(p.ShouldRebuild(0.25, c));
    CHECK(p.ShouldRebuild(0.3, c));
}

void TestClockGoingBackwardsResyncs() {
    current = "TestClockGoingBackwardsResyncs";
    UIRefreshPolicy p{4.0f};
    p.MarkRebuilt(5.0);
    const UIDirtyState c = Clean();
    CHECK(p.ShouldRebuild(4.0, c));                      // relogio andou para tras: reconstroi
    p.MarkRebuilt(4.0);
    CHECK(!p.ShouldRebuild(4.1, c));
    CHECK(p.ShouldRebuild(4.25, c));
}

void TestNonFiniteTimeIsIgnored() {
    current = "TestNonFiniteTimeIsIgnored";
    UIRefreshPolicy p{60.0f};
    UIDirtyState d;
    CHECK(!p.ShouldRebuild(std::nan(""), d));
    CHECK(!p.TryRebuild(std::numeric_limits<double>::infinity(), d));
    CHECK(d.bits != 0);
}

} // namespace

int main() {
    TestDefaultsAndRateSanitizing();
    TestFreshWindowRebuildsOnFirstFrame();
    TestSixHundredFramesAt4HzRebuildsForty();
    TestSixHundredFramesAt60HzRebuildsEveryFrame();
    TestRateZeroRebuildsOnlyOnceWhenClean();
    TestRateZeroRebuildsWhenDirtied();
    TestDirtyWindowRebuildsOffCycle();
    TestNeverTwiceAtTheSameInstant();
    TestIntervalBoundaryAt4Hz();
    TestClockGoingBackwardsResyncs();
    TestNonFiniteTimeIsIgnored();
    std::printf("refresh policy: %d/%d checks passed\n", run - failed, run);
    return failed == 0 ? 0 : 1;
}
