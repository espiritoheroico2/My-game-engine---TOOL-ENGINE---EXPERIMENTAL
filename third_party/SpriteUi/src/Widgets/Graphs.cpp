#include "SpriteUi/Widgets/Graphs.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ToolEngine::SpriteUi::Widgets {

std::size_t UIOscilloscopeGraph::SafeCount() const noexcept {
    return std::min(sampleCount, HistoryCapacity);
}

std::size_t UIOscilloscopeGraph::SafeHead() const noexcept {
    return headIndex % HistoryCapacity;
}

void UIOscilloscopeGraph::AddSample(float value) noexcept {
    if (!IsFinite(value)) value = 0.0f;
    // The fields are public: never trust them for indexing.
    const std::size_t head = SafeHead();
    valueHistory[head] = value;
    headIndex = (head + 1) % HistoryCapacity;
    sampleCount = std::min(SafeCount() + 1, HistoryCapacity);
}

void UIOscilloscopeGraph::Clear() noexcept {
    for (std::size_t i = 0; i < HistoryCapacity; ++i) {
        valueHistory[i] = 0.0f;
    }
    headIndex = 0;
    sampleCount = 0;
}

float UIOscilloscopeGraph::GetLatestSample() const noexcept {
    if (SafeCount() == 0) return 0.0f;
    const std::size_t head = SafeHead();
    const std::size_t latestIdx = (head == 0) ? (HistoryCapacity - 1) : (head - 1);
    return valueHistory[latestIdx];
}

void UIOscilloscopeGraph::CopyChronological(
    std::span<float> outBuffer, std::size_t skip, std::size_t count) const noexcept
{
    const std::size_t stored = SafeCount();
    const std::size_t oldest = (stored < HistoryCapacity) ? 0 : SafeHead();
    for (std::size_t i = 0; i < count; ++i) {
        outBuffer[i] = valueHistory[(oldest + skip + i) % HistoryCapacity];
    }
}

void UIOscilloscopeGraph::GetOrderedSamples(std::span<float> outBuffer) const noexcept {
    const std::size_t stored = SafeCount();
    if (outBuffer.empty() || stored == 0) return;
    CopyChronological(outBuffer, 0, std::min(outBuffer.size(), stored));
}

std::size_t UIOscilloscopeGraph::GetLatestSamples(std::span<float> outBuffer) const noexcept {
    const std::size_t stored = SafeCount();
    if (outBuffer.empty() || stored == 0) return 0;
    const std::size_t count = std::min(outBuffer.size(), stored);
    CopyChronological(outBuffer, stored - count, count);
    return count;
}

void UIOscilloscopeGraph::GetNormalizedSamples(
    std::span<float> outBuffer, float minVal, float maxVal, SampleWindow window) const noexcept
{
    const std::size_t stored = SafeCount();
    if (outBuffer.empty() || stored == 0) return;

    const std::size_t count = std::min(outBuffer.size(), stored);
    if (window == SampleWindow::Latest) {
        CopyChronological(outBuffer, stored - count, count);
    } else {
        CopyChronological(outBuffer, 0, count);
    }

    float lo = minVal;
    float hi = maxVal;

    if (!IsFinite(lo) || !IsFinite(hi) || lo >= hi) {
        lo = std::numeric_limits<float>::infinity();
        hi = -std::numeric_limits<float>::infinity();
        for (std::size_t i = 0; i < count; ++i) {
            lo = std::min(lo, outBuffer[i]);
            hi = std::max(hi, outBuffer[i]);
        }
        if (lo >= hi) {
            hi = lo + 1.0f;
        }
    }

    const float range = hi - lo;
    for (std::size_t i = 0; i < count; ++i) {
        outBuffer[i] = std::clamp((outBuffer[i] - lo) / range, 0.0f, 1.0f);
    }
}

void UILinePlot::AddPoint(Vec2 point) {
    if (IsFinite(point)) {
        points.push_back(point);
    }
}

Rect UILinePlot::ComputeBounds() const noexcept {
    if (points.empty()) return Rect{};

    Vec2 minPt{std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity()};
    Vec2 maxPt{-std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()};

    for (const auto& pt : points) {
        minPt.x = std::min(minPt.x, pt.x);
        minPt.y = std::min(minPt.y, pt.y);
        maxPt.x = std::max(maxPt.x, pt.x);
        maxPt.y = std::max(maxPt.y, pt.y);
    }

    return Rect{minPt, {std::max(0.0f, maxPt.x - minPt.x), std::max(0.0f, maxPt.y - minPt.y)}};
}

} // namespace ToolEngine::SpriteUi::Widgets

