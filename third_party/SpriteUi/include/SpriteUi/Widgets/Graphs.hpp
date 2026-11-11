#pragma once

#include "SpriteUi/Core/Math.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ToolEngine::SpriteUi::Widgets {

// Which part of the history is copied when the destination buffer is smaller than the
// number of stored samples.
enum class SampleWindow : std::uint8_t {
    Oldest, // The first N samples of the history (legacy behaviour of GetOrderedSamples).
    Latest  // The most recent N samples, still in chronological order (what a scrolling plot needs).
};

// Oscilloscope and scrolling telemetry graph (Section 29 & 30.1).
// Stores a fixed circular ring buffer of 256 samples with O(1) ingestion.
struct UIOscilloscopeGraph {
    static constexpr std::size_t HistoryCapacity = 256;

    float valueHistory[HistoryCapacity]{};
    std::size_t headIndex = 0;
    std::size_t sampleCount = 0;

    Vec4 lineColor{0.0f, 1.0f, 0.0f, 1.0f};
    float thickness = 2.0f;
    float scrollSpeed = 50.0f;

    void AddSample(float value) noexcept;
    void Clear() noexcept;
    [[nodiscard]] float GetLatestSample() const noexcept;

    // Writes samples in chronological order (oldest to newest) into outBuffer.
    // When outBuffer is smaller than the history, the OLDEST outBuffer.size() samples are written.
    void GetOrderedSamples(std::span<float> outBuffer) const noexcept;

    // Same ordering, but when outBuffer is smaller than the history the MOST RECENT samples are
    // written (chronological order preserved). Returns the number of samples written.
    std::size_t GetLatestSamples(std::span<float> outBuffer) const noexcept;

    // Normalizes samples in [0, 1] range. If minVal >= maxVal, automatically computes min/max.
    // `window` selects which samples are used when outBuffer is smaller than the history.
    void GetNormalizedSamples(
        std::span<float> outBuffer,
        float minVal = 0.0f,
        float maxVal = 0.0f,
        SampleWindow window = SampleWindow::Oldest) const noexcept;

private:
    // Copies `count` samples starting `skip` samples after the oldest one.
    void CopyChronological(std::span<float> outBuffer, std::size_t skip, std::size_t count) const noexcept;
    [[nodiscard]] std::size_t SafeCount() const noexcept;
    [[nodiscard]] std::size_t SafeHead() const noexcept;
};

// Line plot for static or dynamic 2D vector series (Section 29).
struct UILinePlot {
    std::vector<Vec2> points;
    Vec4 lineColor{0.2f, 0.8f, 1.0f, 1.0f};
    float thickness = 2.0f;

    void AddPoint(Vec2 point);
    void Clear() noexcept { points.clear(); }
    [[nodiscard]] Rect ComputeBounds() const noexcept;
};

} // namespace ToolEngine::SpriteUi::Widgets

