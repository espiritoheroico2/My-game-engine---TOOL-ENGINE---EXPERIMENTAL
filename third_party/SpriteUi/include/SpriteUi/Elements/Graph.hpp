#pragma once

// Graficos (Graph): linha ou histograma sobre um historico de amostras em anel de tamanho fixo.
//
// Partes: o fundo, a area de plotagem (PlotArea: a caixa menos margens para eixos e rotulos), os eixos esquerdo
// e inferior, e os dados. Os rotulos de valor ficam nas margens (YAxisLabelArea e XAxisLabelArea), por fora da
// area de dados, para nada encostar na borda. O chamador desenha esses rotulos.
//
// Historico: RingHistory guarda as ultimas N amostras, com N fixo. Quando esta cheio, a amostra nova toma o lugar
// da mais antiga. At(i) le da mais antiga (0) a mais nova; indice fora da faixa devolve NaN, nunca le fora do vetor.
//
// Escala: automatica (sobre as amostras finitas) ou fixa (fixedMin a fixedMax). NaN e infinito nao entram na
// escala e nao viram pico: sao pulados no desenho. Faixa de largura zero recebe uma folga de 0,5 para cada lado.
//
// Desenho: o custo cresce com a largura da area, nao com o numero de amostras. Cada coluna de pixel recebe o
// minimo, o maximo e os valores das pontas das amostras que caem nela (uma passada pelo historico).
//   - Linha: um traco vertical por coluna, de minimo a maximo, ligado ao da coluna anterior.
//   - Histograma: uma barra por coluna, ate o maximo da coluna.
//
// Exemplo:
//   auto id = tree.Create<Graph>(parent);
//   auto* graph = tree.GetAs<Graph>(id);
//   graph->history = RingHistory(240);
//   graph->Push(cpuLoad);

#include "SpriteUi/Core/Math.hpp"
#include "SpriteUi/Elements/Element.hpp"
#include "SpriteUi/Rendering/DrawList.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ToolEngine::SpriteUi::Elements {

class RingHistory {
  public:
    RingHistory() = default;
    explicit RingHistory(std::size_t capacity);

    // Guarda a amostra. Com o historico cheio, substitui a mais antiga. Capacidade zero: nada e guardado.
    void Push(float value) noexcept;

    [[nodiscard]] std::size_t Size() const noexcept { return size_; }
    [[nodiscard]] std::size_t Capacity() const noexcept { return data_.size(); }

    // Amostra i, da mais antiga (0) a mais nova. Fora da faixa devolve NaN.
    [[nodiscard]] float At(std::size_t index) const noexcept;

  private:
    std::vector<float> data_;
    std::size_t head_ = 0;  // Proxima posicao de escrita.
    std::size_t size_ = 0;
};

enum class GraphKind : std::uint8_t { Line, Histogram };

struct GraphRange {
    float min = 0.0f;
    float max = 1.0f;
};

struct GraphStyle {
    Vec4 background{0.1f, 0.11f, 0.14f, 1.0f};
    Vec4 axis{0.5f, 0.52f, 0.58f, 1.0f};
    Vec4 line{0.35f, 0.75f, 1.0f, 1.0f};
    Vec4 histogram{0.4f, 0.85f, 0.5f, 1.0f};
    float axisThickness = 1.0f;
    float lineThickness = 1.5f;
};

class Graph final : public Element {
  public:
    GraphKind kind = GraphKind::Line;
    RingHistory history{120};
    bool autoScale = true;   // true: escala sobre as amostras; false: fixedMin a fixedMax.
    float fixedMin = 0.0f;
    float fixedMax = 1.0f;
    float leftMargin = 36.0f;    // Espaco dos rotulos de valor, a esquerda.
    float bottomMargin = 16.0f;  // Espaco dos rotulos do eixo, embaixo.
    float topMargin = 4.0f;
    float rightMargin = 6.0f;

    void Push(float value) noexcept { history.Push(value); }

    // Faixa de valores usada no desenho: automatica ou fixa, com folga quando a largura e zero.
    [[nodiscard]] GraphRange ValueRange() const noexcept;

    // Area onde os dados sao desenhados (dentro das margens).
    [[nodiscard]] Rect PlotArea() const noexcept;

    // Faixa dos rotulos de valor, a esquerda da area de dados. Mesma altura da area.
    [[nodiscard]] Rect YAxisLabelArea() const noexcept;

    // Faixa dos rotulos do eixo, embaixo da area de dados. Mesma largura da area.
    [[nodiscard]] Rect XAxisLabelArea() const noexcept;
};

// Y no canvas para um valor na faixa: range.max fica no topo da area, range.min na base. NaN se o valor nao for
// finito. Valores fora da faixa sao presos a area.
[[nodiscard]] float GraphValueToY(Rect plot, GraphRange range, float value) noexcept;

// Itens do fundo, dos eixos e dos dados. Os dados sao recortados pela area de plotagem (clipIndex).
[[nodiscard]] std::vector<Rendering::UIRenderItem> BuildGraphItems(const Graph& graph, const GraphStyle& style,
                                                                    std::uint32_t clipIndex, float zOrder,
                                                                    std::uint64_t paintOrder);

} // namespace ToolEngine::SpriteUi::Elements
