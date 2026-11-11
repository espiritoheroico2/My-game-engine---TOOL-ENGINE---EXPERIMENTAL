#include "SpriteUi/SpriteUi.hpp"

#include "TestHarness.hpp"
#include "TestSuites.hpp"

#include <cstddef>
#include <limits>
#include <vector>

namespace SpriteUiTests {

namespace {

using ToolEngine::SpriteUi::Widgets::SampleWindow;
using ToolEngine::SpriteUi::Widgets::UIOscilloscopeGraph;

void TestLatestSamplesReturnsNewestInOrder() {
    UIOscilloscopeGraph osc;
    for (int i = 0; i < 300; ++i) {
        osc.AddSample(static_cast<float>(i));
    }
    // Historico cheio: amostras 44..299.
    float latest[4];
    const std::size_t written = osc.GetLatestSamples(latest);
    CHECK(written == 4);
    CHECK(Near(latest[0], 296.0f));
    CHECK(Near(latest[1], 297.0f));
    CHECK(Near(latest[2], 298.0f));
    CHECK(Near(latest[3], 299.0f));

    // O comportamento legado de GetOrderedSamples (mais antigas) permanece intacto.
    float oldest[4];
    osc.GetOrderedSamples(oldest);
    CHECK(Near(oldest[0], 44.0f));
    CHECK(Near(oldest[3], 47.0f));

    // Buffer maior que o historico: copia tudo e informa o total.
    std::vector<float> all(1000, -1.0f);
    CHECK(osc.GetLatestSamples(all) == UIOscilloscopeGraph::HistoryCapacity);
    CHECK(Near(all.front(), 44.0f));
    CHECK(Near(all[255], 299.0f));
    CHECK(Near(all[256], -1.0f)); // nao escreve alem do historico
}

void TestLatestSamplesBeforeRingIsFull() {
    UIOscilloscopeGraph osc;
    for (int i = 0; i < 10; ++i) {
        osc.AddSample(static_cast<float>(i));
    }
    float out[3];
    CHECK(osc.GetLatestSamples(out) == 3);
    CHECK(Near(out[0], 7.0f));
    CHECK(Near(out[2], 9.0f));

    UIOscilloscopeGraph empty;
    float none[3]{-1.0f, -1.0f, -1.0f};
    CHECK(empty.GetLatestSamples(none) == 0);
    CHECK(Near(none[0], -1.0f));
    CHECK(empty.GetLatestSamples(std::span<float>{}) == 0);
}

void TestNormalizedSamplesWindow() {
    UIOscilloscopeGraph osc;
    for (int i = 0; i < 300; ++i) {
        osc.AddSample(static_cast<float>(i));
    }
    float oldest[2];
    osc.GetNormalizedSamples(oldest, 0.0f, 300.0f); // padrao: Oldest (44, 45)
    CHECK(Near(oldest[0], 44.0f / 300.0f));
    float latest[2];
    osc.GetNormalizedSamples(latest, 0.0f, 300.0f, SampleWindow::Latest); // 298, 299
    CHECK(Near(latest[0], 298.0f / 300.0f));
    CHECK(Near(latest[1], 299.0f / 300.0f));

    // Faixa automatica usa so a janela pedida.
    float autoRange[2];
    osc.GetNormalizedSamples(autoRange, 0.0f, 0.0f, SampleWindow::Latest);
    CHECK(Near(autoRange[0], 0.0f));
    CHECK(Near(autoRange[1], 1.0f));

    // Faixa explicita invalida (NaN) cai no automatico, sem produzir NaN.
    float badRange[2];
    osc.GetNormalizedSamples(badRange, std::numeric_limits<float>::quiet_NaN(), 5.0f, SampleWindow::Latest);
    CHECK(std::isfinite(badRange[0]) && std::isfinite(badRange[1]));
}

void TestCorruptedStateNeverIndexesOutOfBounds() {
    // Os campos sao publicos: estado corrompido nao pode causar escrita/leitura fora do array.
    UIOscilloscopeGraph osc;
    osc.headIndex = 99999;
    osc.sampleCount = 99999;
    osc.AddSample(1.0f);
    CHECK(osc.sampleCount <= UIOscilloscopeGraph::HistoryCapacity);
    CHECK(osc.headIndex < UIOscilloscopeGraph::HistoryCapacity);
    float out[8];
    osc.GetOrderedSamples(out);
    CHECK(osc.GetLatestSamples(out) == 8);
    osc.GetNormalizedSamples(out, 0.0f, 0.0f, SampleWindow::Latest);
    (void)osc.GetLatestSample();
    CHECK(true); // o objetivo e nao disparar ASan/UBSan
}

void TestNonFiniteSamplesAreSanitized() {
    UIOscilloscopeGraph osc;
    osc.AddSample(std::numeric_limits<float>::quiet_NaN());
    osc.AddSample(std::numeric_limits<float>::infinity());
    osc.AddSample(2.0f);
    float out[3];
    CHECK(osc.GetLatestSamples(out) == 3);
    CHECK(Near(out[0], 0.0f) && Near(out[1], 0.0f) && Near(out[2], 2.0f));
}

} // namespace

void RunGraphSamplingTests() {
    TestLatestSamplesReturnsNewestInOrder();
    TestLatestSamplesBeforeRingIsFull();
    TestNormalizedSamplesWindow();
    TestCorruptedStateNeverIndexesOutOfBounds();
    TestNonFiniteSamplesAreSanitized();
}

} // namespace SpriteUiTests
