// Graficos (Graph): anel de historico com tamanho fixo, escala que ignora NaN, area de plotagem e contagem de itens.
// Valores calculados a mao nos comentarios.
#include "SpriteUi/Elements/Graph.hpp"

#include <cmath>
#include <cstdio>
#include <limits>

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

const float kNan = std::numeric_limits<float>::quiet_NaN();
const float kInf = std::numeric_limits<float>::infinity();

// Capacidade 3: depois de quatro amostras, fica [2, 3, 4] da mais antiga a mais nova.
void TestRingKeepsNewestInOrder() {
    RingHistory ring(3);
    for (float v : {1.0f, 2.0f, 3.0f, 4.0f}) ring.Push(v);
    CHECK(ring.Size() == 3);
    CHECK(ring.Capacity() == 3);
    CHECK(Near(ring.At(0), 2.0f) && Near(ring.At(1), 3.0f) && Near(ring.At(2), 4.0f));
}

// Indice fora da faixa devolve NaN, mesmo com o anel cheio.
void TestRingOutOfRangeIsNan() {
    RingHistory ring(3);
    ring.Push(1.0f);
    CHECK(std::isnan(ring.At(1)));
    CHECK(std::isnan(ring.At(99)));
}

// Capacidade zero: nada e guardado e nenhum indice e valido.
void TestRingZeroCapacity() {
    RingHistory ring(0);
    ring.Push(1.0f);
    CHECK(ring.Size() == 0);
    CHECK(std::isnan(ring.At(0)));
}

// Escala automatica: NaN e infinito nao entram. Amostras finitas 1 e 5 dao faixa {1, 5}.
void TestAutoRangeIgnoresNonFinite() {
    Graph graph;
    graph.history = RingHistory(8);
    for (float v : {1.0f, kNan, 5.0f, kInf}) graph.Push(v);
    const GraphRange range = graph.ValueRange();
    CHECK(Near(range.min, 1.0f) && Near(range.max, 5.0f));
}

// Largura zero: faixa {2, 2} vira {1,5; 2,5}, uma folga de 0,5 para cada lado.
void TestZeroSpanGetsHalfSpanEachSide() {
    Graph graph;
    graph.history = RingHistory(4);
    graph.Push(2.0f);
    graph.Push(2.0f);
    const GraphRange range = graph.ValueRange();
    CHECK(Near(range.min, 1.5f) && Near(range.max, 2.5f));
}

// Escala fixa: usa fixedMin e fixedMax, ignorando as amostras.
void TestFixedRangeUsesBounds() {
    Graph graph;
    graph.autoScale = false;
    graph.fixedMin = -10.0f;
    graph.fixedMax = 10.0f;
    graph.Push(999.0f);
    const GraphRange range = graph.ValueRange();
    CHECK(Near(range.min, -10.0f) && Near(range.max, 10.0f));
}

// Caixa 200 x 100 com margens 36 (esq), 6 (dir), 4 (cima) e 16 (baixo): area {{36, 4}, {158, 80}}.
void TestPlotAreaRespectsMargins() {
    Graph graph;
    graph.border = {{0.0f, 0.0f}, {200.0f, 100.0f}};
    const Rect plot = graph.PlotArea();
    CHECK(Near(plot.position.x, 36.0f) && Near(plot.position.y, 4.0f));
    CHECK(Near(plot.size.x, 158.0f) && Near(plot.size.y, 80.0f));
}

// Rotulos: a faixa do eixo Y fica a esquerda da area; a do eixo X fica embaixo dela.
void TestLabelAreasAreInMargins() {
    Graph graph;
    graph.border = {{0.0f, 0.0f}, {200.0f, 100.0f}};
    const Rect plot = graph.PlotArea();
    const Rect yLabels = graph.YAxisLabelArea();
    const Rect xLabels = graph.XAxisLabelArea();
    CHECK(yLabels.position.x + yLabels.size.x <= plot.position.x);
    CHECK(xLabels.position.y >= plot.position.y + plot.size.y);
    CHECK(xLabels.position.y + xLabels.size.y <= graph.border.position.y + graph.border.size.y + 1.0e-4f);
}

// Plotagem de 0 a 1 em area de altura 100 (topo em y 0): valor 1 vai ao topo, 0 a base, fora da faixa preso.
void TestValueToYMapsAndClamps() {
    const Rect plot{{0.0f, 0.0f}, {10.0f, 100.0f}};
    const GraphRange range{0.0f, 1.0f};
    CHECK(Near(GraphValueToY(plot, range, 1.0f), 0.0f));
    CHECK(Near(GraphValueToY(plot, range, 0.0f), 100.0f));
    CHECK(Near(GraphValueToY(plot, range, 2.0f), 0.0f));
    CHECK(Near(GraphValueToY(plot, range, -3.0f), 100.0f));
    CHECK(std::isnan(GraphValueToY(plot, range, kNan)));
    CHECK(std::isnan(GraphValueToY(plot, range, kInf)));
}

// Linha com 3 amostras em area de 158 colunas: 3 traços + 2 ligacoes + fundo + 2 eixos = 8 itens.
void TestLineThreeSamplesItemCount() {
    Graph graph;
    graph.border = {{0.0f, 0.0f}, {200.0f, 100.0f}};
    graph.history = RingHistory(120);
    for (float v : {1.0f, 2.0f, 3.0f}) graph.Push(v);
    CHECK(BuildGraphItems(graph, GraphStyle{}, 0, 0.0f, 0).size() == 8);
}

// Histograma com 3 amostras: 3 barras + fundo + 2 eixos = 6 itens.
void TestHistogramThreeSamplesItemCount() {
    Graph graph;
    graph.kind = GraphKind::Histogram;
    graph.border = {{0.0f, 0.0f}, {200.0f, 100.0f}};
    graph.history = RingHistory(120);
    for (float v : {1.0f, 2.0f, 3.0f}) graph.Push(v);
    CHECK(BuildGraphItems(graph, GraphStyle{}, 0, 0.0f, 0).size() == 6);
}

// Custo ligado a largura, nao ao numero de amostras: 1000 e 5000 amostras dao a mesma contagem de itens.
void TestItemCountIndependentOfSampleCount() {
    Graph small;
    small.border = {{0.0f, 0.0f}, {200.0f, 100.0f}};
    small.history = RingHistory(1000);
    Graph large;
    large.border = {{0.0f, 0.0f}, {200.0f, 100.0f}};
    large.history = RingHistory(5000);
    for (int i = 0; i < 1000; ++i) small.Push(static_cast<float>(i % 17));
    for (int i = 0; i < 5000; ++i) large.Push(static_cast<float>(i % 17));
    const auto a = BuildGraphItems(small, GraphStyle{}, 0, 0.0f, 0);
    const auto b = BuildGraphItems(large, GraphStyle{}, 0, 0.0f, 0);
    CHECK(a.size() == b.size());
}

// NaN no meio: a amostra e pulada e nao cria item. [1, NaN, 2] tem 2 colunas com dado: 2 traços + 1 ligacao + 3.
void TestNanSampleAddsNoItem() {
    Graph graph;
    graph.border = {{0.0f, 0.0f}, {200.0f, 100.0f}};
    graph.history = RingHistory(120);
    for (float v : {1.0f, kNan, 2.0f}) graph.Push(v);
    CHECK(BuildGraphItems(graph, GraphStyle{}, 0, 0.0f, 0).size() == 6);
}

// Sem amostras: so o fundo e os eixos (3 itens). Area de largura zero: so o fundo (1 item).
void TestEmptyAndDegenerateBoxes() {
    Graph graph;
    graph.border = {{0.0f, 0.0f}, {200.0f, 100.0f}};
    CHECK(BuildGraphItems(graph, GraphStyle{}, 0, 0.0f, 0).size() == 3);
    graph.border = {{0.0f, 0.0f}, {30.0f, 10.0f}};  // 30 - 36 - 6 < 0: area sem largura.
    CHECK(BuildGraphItems(graph, GraphStyle{}, 0, 0.0f, 0).size() == 1);
}

} // namespace

int main() {
    TestRingKeepsNewestInOrder();
    TestRingOutOfRangeIsNan();
    TestRingZeroCapacity();
    TestAutoRangeIgnoresNonFinite();
    TestZeroSpanGetsHalfSpanEachSide();
    TestFixedRangeUsesBounds();
    TestPlotAreaRespectsMargins();
    TestLabelAreasAreInMargins();
    TestValueToYMapsAndClamps();
    TestLineThreeSamplesItemCount();
    TestHistogramThreeSamplesItemCount();
    TestItemCountIndependentOfSampleCount();
    TestNanSampleAddsNoItem();
    TestEmptyAndDegenerateBoxes();

    std::printf("graph: %d checks, %d failed\n", run, failed);
    return failed == 0 ? 0 : 1;
}
