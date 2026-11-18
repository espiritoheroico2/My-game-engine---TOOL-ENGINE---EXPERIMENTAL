#include "SpriteUi/Widgets/DockGuides.hpp"

#include "SpriteUi/Core/DrawOrder.hpp"

#include <cmath>
#include <cstdio>
#include <limits>

namespace {

using namespace ToolEngine::SpriteUi;
using namespace ToolEngine::SpriteUi::Widgets;

int checks = 0;
int failures = 0;
const char* currentTest = "";

void Check(bool ok, const char* expression) {
    ++checks;
    if (!ok) {
        ++failures;
        std::fprintf(stderr, "[FAIL] %s: %s\n", currentTest, expression);
    }
}
#define CHECK(expression) Check((expression), #expression)

void TestFiveDistinctGuidesUseDragPreviewLayer() {
    currentTest = "TestFiveDistinctGuidesUseDragPreviewLayer";
    constexpr UIHandle owner{7, 2};
    constexpr Rect leaf{{100.0f, 50.0f}, {300.0f, 200.0f}};
    DockGuideStyle style;
    style.iconSize = 28.0f;
    style.edgeInset = 12.0f;
    style.idleAlpha = 0.4f;
    style.highlightAlpha = 0.8f;
    const auto items = BuildDockGuideItems(owner, leaf, DockSlot::Right, style, 30);

    CHECK(items.size() == 15);
    for (std::size_t i = 0; i < items.size(); ++i) {
        const auto& item = items[i];
        CHECK(item.handle == owner);
        CHECK(item.layer == static_cast<std::int32_t>(UILayer::DragPreview));
        CHECK(item.paintOrder == 30 + i);
        CHECK(item.layout.resolvedSize.x > 0.0f && item.layout.resolvedSize.y > 0.0f);
        CHECK(item.layout.resolvedPosition.x >= leaf.position.x);
        CHECK(item.layout.resolvedPosition.y >= leaf.position.y);
        CHECK(item.layout.resolvedPosition.x + item.layout.resolvedSize.x <= leaf.position.x + leaf.size.x);
        CHECK(item.layout.resolvedPosition.y + item.layout.resolvedSize.y <= leaf.position.y + leaf.size.y);
    }

    CHECK(items[2].tintPremultipliedLinear.w == style.idleAlpha);
    CHECK(items[5].tintPremultipliedLinear.w == style.highlightAlpha);
    CHECK(items[14].tintPremultipliedLinear.w == style.idleAlpha);
    CHECK(items[0].layout.resolvedPosition.x != items[3].layout.resolvedPosition.x);
    CHECK(items[0].layout.resolvedPosition.y != items[6].layout.resolvedPosition.y);
}

void TestSmallLeafKeepsGuidesInsideItsBounds() {
    currentTest = "TestSmallLeafKeepsGuidesInsideItsBounds";
    constexpr Rect leaf{{5.0f, 9.0f}, {18.0f, 12.0f}};
    const auto items = BuildDockGuideItems(UIHandle{1, 1}, leaf, DockSlot::None);
    CHECK(items.size() == 15);
    for (const auto& item : items) {
        CHECK(item.layout.resolvedPosition.x >= leaf.position.x);
        CHECK(item.layout.resolvedPosition.y >= leaf.position.y);
        CHECK(item.layout.resolvedPosition.x + item.layout.resolvedSize.x <= leaf.position.x + leaf.size.x);
        CHECK(item.layout.resolvedPosition.y + item.layout.resolvedSize.y <= leaf.position.y + leaf.size.y);
    }
}

void TestInvalidInputsProduceNoGuides() {
    currentTest = "TestInvalidInputsProduceNoGuides";
    const UIHandle owner{1, 1};
    CHECK(BuildDockGuideItems(InvalidUIHandle, {{0.0f, 0.0f}, {20.0f, 20.0f}}, DockSlot::Left).empty());
    CHECK(BuildDockGuideItems(owner, {{0.0f, 0.0f}, {0.0f, 20.0f}}, DockSlot::Left).empty());
    CHECK(BuildDockGuideItems(owner, {{0.0f, 0.0f}, {20.0f, 20.0f}}, DockSlot::Left,
                              DockGuideStyle{std::numeric_limits<float>::quiet_NaN()}).empty());
    DockGuideStyle invalidColor;
    invalidColor.highlightColor.x = std::numeric_limits<float>::infinity();
    CHECK(BuildDockGuideItems(owner, {{0.0f, 0.0f}, {20.0f, 20.0f}}, DockSlot::Left, invalidColor).empty());
    invalidColor.highlightColor.x = 1.1f;
    CHECK(BuildDockGuideItems(owner, {{0.0f, 0.0f}, {20.0f, 20.0f}}, DockSlot::Left, invalidColor).empty());
    CHECK(BuildDockGuideItems(owner,
                              {{std::numeric_limits<float>::max(), 0.0f}, {std::numeric_limits<float>::max(), 20.0f}},
                              DockSlot::Left).empty());
}

} // namespace

int main() {
    TestFiveDistinctGuidesUseDragPreviewLayer();
    TestSmallLeafKeepsGuidesInsideItsBounds();
    TestInvalidInputsProduceNoGuides();
    if (failures == 0) {
        std::printf("Editor.SpriteUiDockGuides: %d checks, 0 failed\n", checks);
        return 0;
    }
    std::printf("Editor.SpriteUiDockGuides: %d checks, %d failed\n", checks, failures);
    return 1;
}
