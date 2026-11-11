#include "SpriteUi/SpriteUi.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using namespace ToolEngine::SpriteUi;

constexpr std::size_t ItemCount = 1000;
constexpr std::size_t IterationCount = 250;

double ElapsedMilliseconds(Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

void BenchmarkLayout() {
    LayoutNode root;
    root.style.width = {SizeMode::Fixed, 1280.0f};
    root.style.height = {SizeMode::Fixed, 720.0f};
    root.style.direction = LayoutDirection::Row;
    root.children.resize(ItemCount);
    for (auto& child : root.children) {
        child.style.width = {SizeMode::Flex, 0.0f};
        child.style.height = {SizeMode::Fixed, 24.0f};
        child.style.grow = 1.0f;
    }

    LayoutEngine engine;
    LayoutStats stats{};
    const auto start = Clock::now();
    for (std::size_t i = 0; i < IterationCount; ++i) {
        (void)engine.Measure(root, {1280.0f, 720.0f}, &stats);
        engine.Arrange(root, {{0.0f, 0.0f}, {1280.0f, 720.0f}}, &stats);
    }
    const double elapsed = ElapsedMilliseconds(start, Clock::now());
    std::cout << "Layout Measure+Arrange: " << elapsed / IterationCount
              << " ms/frame (" << IterationCount << " frames, "
              << ItemCount << " children)\n";
}

void BenchmarkExtraction() {
    std::vector<Rendering::UIRenderItem> items(ItemCount);
    for (std::size_t i = 0; i < items.size(); ++i) {
        auto& item = items[i];
        item.handle = {static_cast<std::uint32_t>(i + 1), 1};
        item.layout.resolvedPosition = {
            static_cast<float>(i % 40) * 24.0f,
            static_cast<float>(i / 40) * 24.0f
        };
        item.layout.resolvedSize = {22.0f, 22.0f};
        item.paintOrder = i;
        item.textureSlice = static_cast<std::uint32_t>(i % 4);
    }

    Rendering::RenderExtractor extractor;
    std::uint64_t submitted = 0;
    const auto start = Clock::now();
    for (std::size_t i = 0; i < IterationCount; ++i) {
        const auto packet = extractor.Build(i, items);
        submitted += packet.instances.size();
    }
    const double elapsed = ElapsedMilliseconds(start, Clock::now());
    const double framesPerSecond = elapsed > 0.0
        ? static_cast<double>(IterationCount) * 1000.0 / elapsed
        : 0.0;
    std::cout << "Render extraction: " << elapsed / IterationCount
              << " ms/frame (" << framesPerSecond << " frames/s, "
              << submitted << " instances total)\n";
}

} // namespace

int main() {
    std::cout << "SpriteUI CPU microbenchmarks (informational; no pass/fail thresholds)\n";
    BenchmarkLayout();
    BenchmarkExtraction();
}
