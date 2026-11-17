#include "SpriteUi/Elements/Graph.hpp"

#include "SpriteUi/Rendering/Primitives.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace ToolEngine::SpriteUi::Elements {

namespace {

// Folga do rotulo em relacao ao eixo, e a metade da folga de uma faixa de largura zero.
constexpr float kLabelGap = 4.0f;
constexpr float kFlatRangeHalfSpan = 0.5f;

// Amostras que caem numa coluna de pixel: minimo, maximo e o primeiro e o ultimo valor finitos.
struct ColumnSpan {
    float min = 0.0f;
    float max = 0.0f;
    float first = 0.0f;
    float last = 0.0f;
    bool has = false;
};

// Uma passada pelo historico. A amostra i cai na coluna (i * colunas) / n; NaN e infinito sao pulados.
std::vector<ColumnSpan> BinSamples(const RingHistory& history, std::size_t columns) {
    std::vector<ColumnSpan> spans(columns);
    const std::size_t count = history.Size();
    for (std::size_t i = 0; i < count; ++i) {
        const float value = history.At(i);
        if (!std::isfinite(value)) continue;
        ColumnSpan& span = spans[(i * columns) / count];
        if (!span.has) {
            span = {value, value, value, value, true};
            continue;
        }
        span.min = std::min(span.min, value);
        span.max = std::max(span.max, value);
        span.last = value;
    }
    return spans;
}

} // namespace

RingHistory::RingHistory(std::size_t capacity) : data_(capacity, 0.0f) {}

void RingHistory::Push(float value) noexcept {
    if (data_.empty()) return;
    data_[head_] = value;
    head_ = (head_ + 1) % data_.size();
    if (size_ < data_.size()) ++size_;
}

float RingHistory::At(std::size_t index) const noexcept {
    if (index >= size_) return std::numeric_limits<float>::quiet_NaN();
    const std::size_t oldest = (head_ + data_.size() - size_) % data_.size();
    return data_[(oldest + index) % data_.size()];
}

GraphRange Graph::ValueRange() const noexcept {
    GraphRange range{0.0f, 1.0f};
    if (autoScale) {
        bool any = false;
        for (std::size_t i = 0; i < history.Size(); ++i) {
            const float value = history.At(i);
            if (!std::isfinite(value)) continue;
            if (!any) {
                range = {value, value};
                any = true;
            } else {
                range.min = std::min(range.min, value);
                range.max = std::max(range.max, value);
            }
        }
    } else if (std::isfinite(fixedMin) && std::isfinite(fixedMax)) {
        range = {fixedMin, fixedMax};
    }
    if (!(range.max > range.min)) {  // Largura zero (ou invertida): folga para cada lado.
        range.min -= kFlatRangeHalfSpan;
        range.max += kFlatRangeHalfSpan;
    }
    return range;
}

Rect Graph::PlotArea() const noexcept {
    const float width = std::max(0.0f, border.size.x - leftMargin - rightMargin);
    const float height = std::max(0.0f, border.size.y - topMargin - bottomMargin);
    return {{border.position.x + leftMargin, border.position.y + topMargin}, {width, height}};
}

Rect Graph::YAxisLabelArea() const noexcept {
    const Rect plot = PlotArea();
    return {{border.position.x, plot.position.y}, {std::max(0.0f, plot.position.x - border.position.x - kLabelGap), plot.size.y}};
}

Rect Graph::XAxisLabelArea() const noexcept {
    const Rect plot = PlotArea();
    const float top = plot.position.y + plot.size.y + kLabelGap;
    return {{plot.position.x, top}, {plot.size.x, std::max(0.0f, border.position.y + border.size.y - top)}};
}

float GraphValueToY(Rect plot, GraphRange range, float value) noexcept {
    if (!std::isfinite(value)) return std::numeric_limits<float>::quiet_NaN();
    const float span = range.max - range.min;
    const float t = span > 0.0f ? (value - range.min) / span : 0.5f;
    return plot.position.y + (1.0f - std::clamp(t, 0.0f, 1.0f)) * plot.size.y;
}

std::vector<Rendering::UIRenderItem> BuildGraphItems(const Graph& graph, const GraphStyle& style,
                                                     std::uint32_t clipIndex, float zOrder,
                                                     std::uint64_t paintOrder) {
    std::vector<Rendering::UIRenderItem> out;
    const Rect box = graph.border;
    if (!IsFinite(box.position) || !IsFinite(box.size)) return out;

    std::uint64_t paint = paintOrder;
    out.push_back(Rendering::MakeSolidRect(graph.handle, box, style.background, Rendering::NoClipIndex, zOrder,
                                           paint++));
    const Rect plot = graph.PlotArea();
    if (plot.size.x <= 0.0f || plot.size.y <= 0.0f) return out;
    const auto columns = static_cast<std::size_t>(std::max(1.0f, std::floor(plot.size.x)));
    out.reserve(3 + 2 * columns);

    const float t = style.axisThickness;
    const float bottom = plot.position.y + plot.size.y;
    out.push_back(Rendering::MakeSolidRect(graph.handle, {{plot.position.x - t, plot.position.y}, {t, plot.size.y}},
                                           style.axis, Rendering::NoClipIndex, zOrder, paint++));
    out.push_back(Rendering::MakeSolidRect(graph.handle, {{plot.position.x - t, bottom}, {plot.size.x + t, t}},
                                           style.axis, Rendering::NoClipIndex, zOrder, paint++));
    if (graph.history.Size() == 0) return out;

    const GraphRange range = graph.ValueRange();
    const std::vector<ColumnSpan> spans = BinSamples(graph.history, columns);

    // Uma coluna de pixel por item de dados: custo cresce com a largura, nao com o numero de amostras.
    std::optional<Vec2> previous;
    for (std::size_t c = 0; c < spans.size(); ++c) {
        const ColumnSpan& span = spans[c];
        if (!span.has) continue;
        const float x = plot.position.x + static_cast<float>(c);
        if (graph.kind == GraphKind::Histogram) {
            const float top = GraphValueToY(plot, range, span.max);
            out.push_back(Rendering::MakeSolidRect(graph.handle, {{x, top}, {1.0f, bottom - top}}, style.histogram,
                                                   clipIndex, zOrder, paint++));
            continue;
        }
        const float yMax = GraphValueToY(plot, range, span.max);
        const float yMin = GraphValueToY(plot, range, span.min);
        out.push_back(Rendering::MakeSolidRect(graph.handle, {{x, yMax}, {1.0f, std::max(1.0f, yMin - yMax)}},
                                               style.line, clipIndex, zOrder, paint++));
        const float yFirst = GraphValueToY(plot, range, span.first);
        if (previous) {
            if (auto segment = Rendering::MakeLine(graph.handle, *previous, {x, yFirst}, style.lineThickness,
                                                   style.line, clipIndex, zOrder, paint++)) {
                out.push_back(*segment);
            }
        }
        previous = Vec2{x, GraphValueToY(plot, range, span.last)};
    }
    return out;
}

} // namespace ToolEngine::SpriteUi::Elements
