#include "SpriteUi/SpriteUi.hpp"

#include "TestHarness.hpp"
#include "TestSuites.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <vector>

using namespace ToolEngine::SpriteUi;

namespace {

using SpriteUiTests::failures;
using SpriteUiTests::Near;
using SpriteUiTests::testsRun;

void TestAnchorsAndTransforms() {
    UIRectTransform transform;
    transform.anchorMin = {0.0f, 0.0f};
    transform.anchorMax = {1.0f, 1.0f};
    transform.offsetMin = {10.0f, 20.0f};
    transform.offsetMax = {-10.0f, -20.0f};

    const auto rect = ResolveAnchoredRect({{0, 0}, {800, 600}}, transform);
    CHECK(rect.has_value());
    CHECK(Near(rect->position.x, 10.0f));
    CHECK(Near(rect->size.x, 780.0f));
    CHECK(Near(rect->size.y, 560.0f));

    UIRectTransform invalidAnchors;
    invalidAnchors.anchorMin = {0.8f, 0.0f};
    invalidAnchors.anchorMax = {0.2f, 1.0f};
    CHECK(!ResolveAnchoredRect({{0, 0}, {800, 600}}, invalidAnchors).has_value());

    UILayoutRect layout{rect->position, rect->size, true, false};
    transform.rotationRadians = 0.4f;
    transform.pivot = {0.5f, 0.5f};
    const auto affine = BuildUnitQuadTransform(layout, transform);
    CHECK(affine.has_value());

    const Vec2 pivotScreen = affine->TransformPoint({0.5f, 0.5f});
    CHECK(Near(pivotScreen.x, rect->position.x + rect->size.x * 0.5f));
    CHECK(Near(pivotScreen.y, rect->position.y + rect->size.y * 0.5f));

    const auto inverse = affine->Inverse();
    CHECK(inverse.has_value());
    const Vec2 roundTrip = inverse->TransformPoint(affine->TransformPoint({0.25f, 0.75f}));
    CHECK(Near(roundTrip.x, 0.25f));
    CHECK(Near(roundTrip.y, 0.75f));
}

void TestInputRouterClickAndCapture() {
    using namespace ToolEngine::SpriteUi::Input;

    UIEventRouter router;
    HitTarget target;
    target.handle = {10, 1};
    target.localToScreen = {1, 0, 100, 0, 1, 100};
    target.localSize = {100, 50};
    target.paintOrder = 1;
    target.focusable = true;

    UIInputSnapshot down;
    down.frameId = 1;
    down.pointerPositionLogical = {120, 120};
    down.primaryDown = true;
    down.primaryPressed = true;
    auto downEvents = router.Process(down, std::span<const HitTarget>(&target, 1));
    CHECK(router.GetState().capturedEntity == target.handle);
    CHECK(router.GetState().focusedEntity == target.handle);
    CHECK(!downEvents.empty());

    UIInputSnapshot up;
    up.frameId = 2;
    up.pointerPositionLogical = {130, 120};
    up.primaryReleased = true;
    auto upEvents = router.Process(up, std::span<const HitTarget>(&target, 1));
    bool clicked = false;
    for (const auto& event : upEvents) clicked |= event.type == UIEventType::Click;
    CHECK(clicked);
    CHECK(!router.GetState().capturedEntity.IsValid());
}

void TestMultiPointerRouter() {
    using namespace ToolEngine::SpriteUi::Input;

    std::vector<HitTarget> targets(2);
    targets[0].handle = {80, 1};
    targets[0].localSize = {100, 100};
    targets[0].localToScreen.tx = 0.0f;
    targets[0].localToScreen.ty = 0.0f;
    targets[0].paintOrder = 1;
    targets[1].handle = {81, 1};
    targets[1].localSize = {100, 100};
    targets[1].localToScreen.tx = 100.0f;
    targets[1].localToScreen.ty = 0.0f;
    targets[1].paintOrder = 2;

    UIMultiPointerEventRouter router;
    std::array<UIInputSnapshot, 2> down{};
    down[0].pointerId = 101;
    down[0].pointerPositionLogical = {20, 20};
    down[0].primaryDown = true;
    down[0].primaryPressed = true;
    down[0].frameId = 1;
    down[1].pointerId = 202;
    down[1].pointerPositionLogical = {120, 20};
    down[1].primaryDown = true;
    down[1].primaryPressed = true;
    down[1].frameId = 1;
    auto result = router.ProcessFrame(1, down, targets);
    CHECK(result.valid);
    CHECK(router.PointerCount() == 2);
    CHECK(router.GetState(101) != nullptr);
    CHECK(router.GetState(101)->capturedEntity == targets[0].handle);
    CHECK(router.GetState(202)->capturedEntity == targets[1].handle);

    std::array<UIInputSnapshot, 2> up = down;
    for (UIInputSnapshot& input : up) {
        input.frameId = 2;
        input.primaryDown = false;
        input.primaryPressed = false;
        input.primaryReleased = true;
    }
    result = router.ProcessFrame(2, up, targets);
    CHECK(result.valid);
    CHECK(std::count_if(result.events.begin(), result.events.end(),
        [](const UIEvent& event) { return event.type == UIEventType::Click; }) == 2);
    CHECK(std::any_of(result.events.begin(), result.events.end(), [&](const UIEvent& event) {
        return event.type == UIEventType::Click && event.pointerId == 101 &&
               event.target == targets[0].handle;
    }));
    CHECK(std::any_of(result.events.begin(), result.events.end(), [&](const UIEvent& event) {
        return event.type == UIEventType::Click && event.pointerId == 202 &&
               event.target == targets[1].handle;
    }));

    up[0].frameId = 3;
    result = router.ProcessFrame(3, std::span<const UIInputSnapshot>(up.data(), 1), targets);
    CHECK(result.valid);
    CHECK(router.PointerCount() == 1);
    CHECK(router.GetState(202) == nullptr);
    CHECK(std::any_of(result.events.begin(), result.events.end(), [](const UIEvent& event) {
        return event.type == UIEventType::PointerLeave && event.pointerId == 202;
    }));

    result = router.ProcessFrame(4, std::span<const UIInputSnapshot>{}, targets);
    CHECK(result.valid);
    CHECK(router.PointerCount() == 0);

    UIMultiPointerEventRouter cancellationRouter;
    UIInputSnapshot touchDown;
    touchDown.pointerId = 303;
    touchDown.pointerPositionLogical = {20, 20};
    touchDown.primaryDown = true;
    touchDown.primaryPressed = true;
    touchDown.frameId = 1;
    result = cancellationRouter.ProcessFrame(
        1, std::span<const UIInputSnapshot>(&touchDown, 1), targets);
    CHECK(result.valid);
    touchDown.frameId = 2;
    touchDown.primaryDown = false;
    touchDown.primaryPressed = false;
    touchDown.pointerCancelled = true;
    result = cancellationRouter.ProcessFrame(
        2, std::span<const UIInputSnapshot>(&touchDown, 1), targets);
    CHECK(result.valid);
    CHECK(std::any_of(result.events.begin(), result.events.end(), [](const UIEvent& event) {
        return event.type == UIEventType::Cancel && event.pointerId == 303;
    }));
    CHECK(std::none_of(result.events.begin(), result.events.end(), [](const UIEvent& event) {
        return event.type == UIEventType::Click && event.pointerId == 303;
    }));

    std::array<UIInputSnapshot, 2> duplicate{};
    duplicate[0].pointerId = duplicate[1].pointerId = 9;
    duplicate[0].frameId = 5;
    duplicate[1].frameId = 6;
    result = router.ProcessFrame(5, duplicate, targets);
    CHECK(!result.valid);
    CHECK(router.PointerCount() == 0);
    duplicate[1].frameId = 5;
    result = router.ProcessFrame(5, duplicate, targets);
    CHECK(!result.valid);
    CHECK(router.PointerCount() == 0);
    router.Reset();
    CHECK(router.PointerCount() == 0);
}

void TestInputRouterCancelAndOcclusion() {
    using namespace ToolEngine::SpriteUi::Input;

    UIEventRouter router;
    HitTarget lower;
    lower.handle = {1, 1};
    lower.localToScreen.tx = 0;
    lower.localToScreen.ty = 0;
    lower.localSize = {100, 100};
    lower.paintOrder = 1;

    HitTarget upper = lower;
    upper.handle = {2, 1};
    upper.paintOrder = 2;
    upper.blocksInput = true;

    const HitTarget targets[] = {lower, upper};
    UIInputSnapshot down;
    down.frameId = 10;
    down.pointerPositionLogical = {20, 20};
    down.primaryPressed = true;
    down.primaryDown = true;
    const auto downEvents = router.Process(down, targets);
    CHECK(!downEvents.empty());
    CHECK(router.GetState().capturedEntity == upper.handle);
    CHECK(router.GetState().consumedThisFrame);

    UIInputSnapshot lost;
    lost.frameId = 11;
    lost.windowFocused = false;
    const auto events = router.Process(lost, targets);
    bool cancelled = false;
    for (const auto& event : events) cancelled |= event.type == UIEventType::Cancel;
    CHECK(cancelled);
    CHECK(!router.GetState().capturedEntity.IsValid());
}

void TestHoverMoveEvent() {
    using namespace ToolEngine::SpriteUi::Input;
    UIEventRouter router;
    HitTarget target;
    target.handle = {55, 1};
    target.localSize = {80, 40};
    target.paintOrder = 1;

    UIInputSnapshot input;
    input.frameId = 3;
    input.pointerPositionLogical = {10, 10};
    input.pointerDeltaLogical = {2, 1};
    const auto events = router.Process(input, std::span<const HitTarget>(&target, 1));
    bool moved = false;
    for (const auto& event : events) moved |= event.type == UIEventType::PointerMove;
    CHECK(moved);
}

void TestSpatialNavigationAndDirtyFlags() {
    using namespace ToolEngine::SpriteUi::Input;
    const UIFocusCandidate candidates[] = {
        {{1, 1}, {{0, 0}, {40, 40}}, true, true, true},
        {{2, 1}, {{60, 0}, {40, 40}}, true, true, true},
        {{3, 1}, {{0, 60}, {40, 40}}, true, true, true},
        {{4, 1}, {{-70, 0}, {40, 40}}, true, true, true}
    };
    CHECK(FindSpatialNeighbor({1, 1}, NavDirection::Right, candidates) == (UIHandle{2, 1}));
    CHECK(FindSpatialNeighbor({1, 1}, NavDirection::Down, candidates) == (UIHandle{3, 1}));
    CHECK(FindSpatialNeighbor({1, 1}, NavDirection::Left, candidates) == (UIHandle{4, 1}));

    UIDirtyState dirty;
    CHECK(dirty.Has(UIDirtyBits::Measure));
    dirty.Clear(UIDirtyBits::Measure);
    CHECK(!dirty.Has(UIDirtyBits::Measure));
    dirty.Mark(UIDirtyBits::TextShape | UIDirtyBits::DrawData);
    CHECK(dirty.Has(UIDirtyBits::TextShape));
    CHECK((dirty.Consume() & static_cast<std::uint32_t>(UIDirtyBits::DrawData)) != 0);
    CHECK(dirty.bits == 0);
}


void TestInvalidInputCancelsCapture() {
    using namespace ToolEngine::SpriteUi::Input;
    UIEventRouter router;
    HitTarget target;
    target.handle = {777, 1};
    target.localSize = {100.0f, 50.0f};

    UIInputSnapshot down;
    down.frameId = 1;
    down.pointerPositionLogical = {10.0f, 10.0f};
    down.primaryPressed = true;
    down.primaryDown = true;
    (void)router.Process(down, std::span<const HitTarget>(&target, 1));
    CHECK(router.GetState().capturedEntity == target.handle);

    UIInputSnapshot invalid;
    invalid.frameId = 2;
    invalid.pointerPositionLogical = {10.0f, 10.0f};
    invalid.pointerDeltaLogical.x = std::numeric_limits<float>::quiet_NaN();
    const auto events = router.Process(invalid, std::span<const HitTarget>(&target, 1));
    bool cancelled = false;
    for (const auto& event : events) cancelled |= event.type == UIEventType::Cancel;
    CHECK(cancelled);
    CHECK(!router.GetState().capturedEntity.IsValid());
}

void TestHitTestTransformAndMaxFrameId() {
    using namespace ToolEngine::SpriteUi::Input;

    UILayoutRect layout{{100.0f, 100.0f}, {200.0f, 100.0f}, true, false};
    UIRectTransform transform;
    transform.pivot = {0.5f, 0.5f};
    transform.scale = {1.5f, 0.75f};
    transform.rotationRadians = 1.57079632679f;
    const auto hitTransform = BuildHitTestTransform(layout, transform);
    CHECK(hitTransform.has_value());
    if (!hitTransform) return;

    const Vec2 center = hitTransform->TransformPoint({100.0f, 50.0f});
    CHECK(Near(center.x, 200.0f));
    CHECK(Near(center.y, 150.0f));

    HitTarget target;
    target.handle = {901, 1};
    target.localToScreen = *hitTransform;
    target.localSize = layout.resolvedSize;
    target.focusable = true;

    UIEventRouter router;
    UIInputSnapshot input;
    input.frameId = UINT64_MAX; // Must be processed even on the first call.
    input.pointerPositionLogical = center;
    input.primaryPressed = true;
    input.primaryDown = true;
    const auto events = router.Process(input, std::span<const HitTarget>(&target, 1));
    CHECK(router.GetState().capturedEntity == target.handle);
    bool down = false;
    for (const auto& event : events) {
        down |= event.type == UIEventType::PointerDown &&
                Near(event.localPosition.x, 100.0f) && Near(event.localPosition.y, 50.0f);
    }
    CHECK(down);

    // Malformed dimensions must never create a hit target.
    HitTarget malformed = target;
    malformed.handle = {902, 1};
    malformed.localSize.x = std::numeric_limits<float>::quiet_NaN();
    UIEventRouter invalidRouter;
    UIInputSnapshot invalidInput;
    invalidInput.frameId = 1;
    invalidInput.pointerPositionLogical = center;
    invalidInput.primaryPressed = true;
    const auto invalidEvents = invalidRouter.Process(
        invalidInput, std::span<const HitTarget>(&malformed, 1));
    CHECK(!invalidRouter.GetState().capturedEntity.IsValid());
    CHECK(invalidEvents.empty());
}


void TestEventBubblingAndHoverConsumption() {
    using namespace ToolEngine::SpriteUi::Input;
    HitTarget parent;
    parent.handle = {100, 1};
    parent.localSize = {200, 100};
    parent.blocksInput = true;
    parent.paintOrder = 1;

    HitTarget child;
    child.handle = {101, 1};
    child.parent = parent.handle;
    child.localSize = {50, 30};
    child.localToScreen.tx = 20.0f;
    child.localToScreen.ty = 20.0f;
    child.blocksInput = false;
    child.paintOrder = 2;

    const HitTarget targets[] = {parent, child};
    UIEventRouter router;
    UIInputSnapshot hover;
    hover.frameId = 20;
    hover.pointerPositionLogical = {25.0f, 25.0f};
    const auto hoverEvents = router.Process(hover, targets);
    CHECK(!hoverEvents.empty());
    CHECK(router.GetState().consumedThisFrame); // Parent blocks world input.

    UIInputSnapshot down;
    down.frameId = 21;
    down.pointerPositionLogical = {25.0f, 25.0f};
    down.primaryPressed = true;
    down.primaryDown = true;
    const auto events = router.Process(down, targets);
    bool childDown = false;
    bool parentDown = false;
    for (const auto& event : events) {
        if (event.type != UIEventType::PointerDown) continue;
        childDown |= event.target == child.handle;
        parentDown |= event.target == parent.handle;
    }
    CHECK(childDown);
    CHECK(parentDown);
    CHECK(router.GetState().consumedThisFrame);
}

void TestSemanticNavigationRouter() {
    using namespace ToolEngine::SpriteUi::Input;
    const UIFocusCandidate candidates[] = {
        {{11, 1}, {{0, 0}, {40, 40}}, true, true, true, 0},
        {{12, 1}, {{60, 0}, {40, 40}}, true, true, true, 1},
        {{13, 1}, {{0, 60}, {40, 40}}, true, true, true, 2}
    };

    UINavigationRouter router;
    const auto moved = router.Process(UINavigationCommand::Right, candidates);
    CHECK(router.Focused() == (UIHandle{12, 1}));
    CHECK(moved.size() == 2); // Initial focus, then directional focus change.
    CHECK(moved.back().type == UISemanticEventType::FocusChanged);

    const auto activated = router.Process(UINavigationCommand::Activate, candidates);
    CHECK(activated.size() == 1);
    CHECK(activated.front().type == UISemanticEventType::Activate);
    CHECK(activated.front().target == (UIHandle{12, 1}));

    const auto cancelled = router.Process(UINavigationCommand::Cancel, candidates);
    CHECK(cancelled.size() == 1);
    CHECK(cancelled.front().type == UISemanticEventType::Cancel);

    const UIFocusCandidate disabled[] = {
        {{12, 1}, {{60, 0}, {40, 40}}, true, false, true, 1}
    };
    const auto lost = router.Process(UINavigationCommand::None, disabled);
    CHECK(!router.Focused().IsValid());
    CHECK(lost.size() == 1);
    CHECK(lost.front().type == UISemanticEventType::FocusChanged);
}


void TestReviewRegressions() {
    using namespace ToolEngine::SpriteUi::Widgets;
    using namespace ToolEngine::SpriteUi::Rendering;
    using namespace ToolEngine::SpriteUi::Text;
    using namespace ToolEngine::SpriteUi::Binding;
    using namespace ToolEngine::SpriteUi::Config;

    // Percentuais resolvem contra o pai, nao contra o viewport raiz.
    {
        LayoutNode root; root.style.width = {SizeMode::Fixed, 400}; root.style.height = {SizeMode::Fixed, 100};
        LayoutNode mid; mid.style.width = {SizeMode::Fixed, 200}; mid.style.height = {SizeMode::Fixed, 100};
        LayoutNode leaf; leaf.style.width = {SizeMode::Percent, 0.5f}; leaf.style.height = {SizeMode::Fixed, 10};
        mid.children = {leaf}; root.children = {mid};
        LayoutEngine engine;
        (void)engine.Measure(root, {400, 100});
        engine.Arrange(root, {{0, 0}, {400, 100}});
        CHECK(Near(root.children[0].children[0].rect.size.x, 100.0f));
    }
    // Stretch nao sobrescreve tamanho transversal Fixed; max/min sao respeitados no grow.
    {
        LayoutNode root; root.style.width = {SizeMode::Fixed, 300}; root.style.height = {SizeMode::Fixed, 100};
        LayoutNode a; a.style.width = {SizeMode::Flex, 0}; a.style.height = {SizeMode::Fixed, 30}; a.style.grow = 1; a.style.maxSize.x = 50;
        LayoutNode b; b.style.width = {SizeMode::Flex, 0}; b.style.height = {SizeMode::Auto, 0}; b.style.grow = 1;
        root.children = {a, b};
        LayoutEngine engine;
        (void)engine.Measure(root, {300, 100});
        engine.Arrange(root, {{0, 0}, {300, 100}});
        CHECK(Near(root.children[0].rect.size.y, 30.0f));
        CHECK(Near(root.children[0].rect.size.x, 50.0f));
        CHECK(Near(root.children[1].rect.size.x, 250.0f));
        CHECK(Near(root.children[1].rect.size.y, 100.0f));
    }
    // Texto gerado precisa sobreviver ao RenderExtractor quando ha handle dono.
    {
        TextLayoutResult layout;
        GlyphQuad quad; quad.bounds = {{0, 0}, {8, 8}};
        layout.quads.push_back(quad);
        UILayoutRect rect{{0, 0}, {100, 20}, true, false};
        const auto items = GenerateTextRenderItems(layout, rect, {1, 1, 1, 1}, 1, NoClipIndex, 0.0f, 0, UIHandle{7, 1});
        CHECK(RenderExtractor{}.Build(1, items).instances.size() == 1);
        CHECK(items[0].flags == Rendering::FlagMsdfText);
        const auto bitmapItems = GenerateTextRenderItems(
            layout, rect, {1, 1, 1, 1}, 1, NoClipIndex, 0.0f, 0,
            UIHandle{7, 1}, Rendering::FlagBitmapText);
        CHECK(bitmapItems[0].flags == Rendering::FlagBitmapText);
    }
    // Bindings disparam na primeira leitura, mesmo com valores "sentinela" antigos.
    {
        UIFloatBinding f; CHECK(f.Update(-1.0e9f));
        UIIntBinding i; CHECK(i.Update(-999999)); CHECK(!i.Update(-999999));
    }
    // Aba ativa preservada ao remover aba anterior; Split em no ja dividido e recusado.
    {
        UITabGroupComponent tabs;
        (void)tabs.AddTab("A", {1, 1}); (void)tabs.AddTab("B", {2, 1}); (void)tabs.AddTab("C", {3, 1});
        CHECK(tabs.SelectTab(2)); CHECK(tabs.RemoveTab(0));
        CHECK(tabs.GetActiveContent() == (UIHandle{3, 1}));
        UIDockNode node({10, 1});
        CHECK(node.Split(DockSlot::Left, 0.3f, {11, 1}));
        CHECK(!node.Split(DockSlot::Right, 0.3f, {12, 1}));
        UIDockNode leaf({20, 1});
        CHECK(leaf.Split(DockSlot::Center, 0.5f, {21, 1}));
        CHECK(leaf.content == (UIHandle{20, 1}) && leaf.tabs.TabCount() == 2);
        CHECK(leaf.VisibleContent() == (UIHandle{21, 1}));
    }
    // VirtualList: scroll gigantesco nao causa overflow de conversao.
    {
        UIVirtualList list(1000, 20.0f);
        const auto range = list.ComputeVisibleRange(1.0e30f, 200.0f);
        CHECK(range.lastIndex == 999 && range.firstIndex <= range.lastIndex);
    }
    // Navegacao: primeiro Activate so adquire foco.
    {
        using namespace ToolEngine::SpriteUi::Input;
        const UIFocusCandidate c[] = {{{1, 1}, {{0, 0}, {10, 10}}, true, true, true, 0}};
        UINavigationRouter router;
        const auto events = router.Process(UINavigationCommand::Activate, c);
        CHECK(events.size() == 1 && events[0].type == UISemanticEventType::FocusChanged);
    }
    // Config: chaves desconhecidas de qualquer tipo, escapes e lixo final.
    {
        UISettingsConfig out;
        const std::string ok = R"({"extra":{"a":[1,2,{"b":null}]},"schemaVersion":1,"windows":[{"name":"a\"b","zzz":3.5}]})";
        CHECK(UIConfigManager::ValidateAndStage(ok, out));
        CHECK(out.windows.size() == 1 && out.windows[0].name == "a\"b");
        CHECK(UIConfigManager::ValidateAndStage(UIConfigManager::Serialize(out), out));
        CHECK(!UIConfigManager::ValidateAndStage(R"({"schemaVersion":1} xx)", out));
        CHECK(!UIConfigManager::ValidateAndStage(R"({"schemaVersion":1)", out));
        CHECK(!UIConfigManager::ValidateAndStage(R"({"schemaVersion":1.5})", out));
    }
    // Slider normalizado e ClampScroll livre (V4.2 sec. 3.1).
    {
        UISliderComponent slider; slider.minValue = 10; slider.maxValue = 20; slider.value = 15;
        CHECK(Near(slider.Normalized(), 0.5f));
        UIScrollViewComponent scroll; scroll.contentSize = {100, 500}; scroll.scrollOffset = {0, 9999};
        ClampScroll(scroll, {100, 200});
        CHECK(Near(scroll.scrollOffset.y, 300.0f));
        CHECK((DirtyMeasure | DirtyArrange) != DirtyNone);
    }
}

void TestCanvasConfig() {
    using namespace ToolEngine::SpriteUi::Rendering;
    UICanvasRenderConfig config;
    CHECK(config.IsValid());
    config.shading = UIShadingMode::ClusterLit;
    CHECK(!config.IsValid());
    config.space = UICanvasSpace::World;
    CHECK(config.IsValid());
    config.castsWorldLight = true;
    CHECK(config.IsValid());
}

void TestWidgets() {
    using namespace ToolEngine::SpriteUi::Widgets;

    UISliderComponent slider;
    CHECK(slider.SetFromPointer(75.0f, 100.0f));
    CHECK(Near(slider.value, 0.75f));
    CHECK(slider.SetValue(5.0f));
    CHECK(Near(slider.value, 1.0f));

    slider.minValue = 10.0f;
    slider.maxValue = 0.0f;
    CHECK(!slider.SetValue(4.0f));

    UIScrollViewComponent scroll;
    scroll.contentSize = {1000, 1200};
    scroll.viewportSize = {400, 500};
    scroll.allowScrollX = true;
    scroll.allowScrollY = true;
    scroll.scrollOffset = {5000, 5000};
    scroll.ClampScroll();
    CHECK(Near(scroll.scrollOffset.x, 600.0f));
    CHECK(Near(scroll.scrollOffset.y, 700.0f));

    scroll.allowScrollY = false;
    scroll.ScrollBy({-100, -100});
    CHECK(Near(scroll.scrollOffset.y, 0.0f));
}

void TestTweenConflictPolicies() {
    using namespace ToolEngine::SpriteUi::Animation;
    ToonMotionSystem system;

    TweenTrack first;
    first.target = {77, 3};
    first.property = TweenProperty::Position;
    first.from = {0, 0, 0, 0};
    first.to = {10, 0, 0, 0};
    first.duration = 1.0f;
    first.easing = Easing::Linear;
    CHECK(system.Add(first));

    TweenTrack duplicateId = first;
    duplicateId.id = 1;
    CHECK(!system.Add(duplicateId, TweenConflict::Queue));

    TweenTrack ignored = first;
    ignored.from = {10, 0, 0, 0};
    ignored.to = {20, 0, 0, 0};
    CHECK(!system.Add(ignored, TweenConflict::Ignore));

    TweenTrack queued = ignored;
    queued.to = {30, 0, 0, 0};
    CHECK(system.Add(queued, TweenConflict::Queue));
    auto update = system.Update(1.0f);
    CHECK(update.completed.size() == 1);
    CHECK(update.samples.size() == 1);

    update = system.Update(0.5f);
    CHECK(update.samples.size() == 1);
    CHECK(Near(update.samples[0].value.x, 20.0f));

    TweenTrack replacement = first;
    replacement.to = {100, 0, 0, 0};
    CHECK(system.Add(replacement, TweenConflict::Override));
    CHECK(system.Size() == 1);
    update = system.Update(0.5f);
    CHECK(update.samples.size() == 1);
    CHECK(Near(update.samples[0].value.x, 50.0f));
}

void TestToonMotion() {
    using namespace ToolEngine::SpriteUi::Animation;

    ToonMotionSystem system;
    TweenTrack track;
    track.target = {7, 2};
    track.property = TweenProperty::Opacity;
    track.from = {0, 0, 0, 0};
    track.to = {1, 0, 0, 0};
    track.duration = 1.0f;
    track.easing = Easing::Linear;

    CHECK(system.Add(track));
    auto update = system.Update(0.5f);
    CHECK(update.samples.size() == 1);
    CHECK(Near(update.samples[0].value.x, 0.5f));
    CHECK(update.completed.empty());

    update = system.Update(0.5f);
    CHECK(update.completed.size() == 1);
    CHECK(Near(update.samples[0].value.x, 1.0f));
    CHECK(system.Size() == 0);

    TweenTrack zero = track;
    zero.duration = 0.0f;
    CHECK(system.Add(zero));
    update = system.Update(0.0f);
    CHECK(update.completed.size() == 1);

    const Vec2 shakeEnd = EvaluateShake(1.0f, 1.0f, {10, 10});
    CHECK(Near(shakeEnd.x, 0.0f));
    CHECK(Near(shakeEnd.y, 0.0f));
}

void TestToonTimeline() {
    using namespace ToolEngine::SpriteUi::Animation;

    ToonTimeline timeline;
    TimelineTrack opacity;
    opacity.target = {70, 1};
    opacity.property = TweenProperty::Opacity;
    opacity.keyframes = {
        {0.0f, {0.0f, 0, 0, 0}, Easing::SmoothStep},
        {2.0f, {1.0f, 0, 0, 0}, Easing::Linear}
    };
    CHECK(timeline.AddTrack(opacity));
    CHECK(timeline.TrackCount() == 1);
    CHECK(Near(timeline.Duration(), 2.0f));
    auto samples = timeline.Sample(-1.0f);
    CHECK(samples.size() == 1);
    CHECK(Near(samples[0].value.x, 0.0f));
    samples = timeline.Sample(1.0f);
    CHECK(samples.size() == 1);
    CHECK(Near(samples[0].value.x, 0.5f));
    samples = timeline.Sample(1.5f);
    CHECK(Near(samples[0].value.x, 0.84375f));
    samples = timeline.Sample(3.0f);
    CHECK(Near(samples[0].value.x, 1.0f));
    CHECK(timeline.Sample(std::numeric_limits<float>::quiet_NaN()).empty());
    CHECK(!timeline.AddTrack(opacity));

    TimelineTrack invalid = opacity;
    invalid.property = TweenProperty::Position;
    invalid.keyframes[1].time = invalid.keyframes[0].time;
    CHECK(!timeline.AddTrack(invalid));
    invalid.keyframes = {{std::numeric_limits<float>::infinity(), {}, Easing::Linear}};
    CHECK(!timeline.AddTrack(invalid));

    TimelineTrack position;
    position.target = {71, 1};
    position.property = TweenProperty::Position;
    position.keyframes = {{0.0f, {}, Easing::Linear},
                          {3.0f, {10.0f, 4.0f, 0, 0}, Easing::Linear}};
    CHECK(timeline.AddTrack(position));
    CHECK(Near(timeline.Duration(), 3.0f));
    CHECK(timeline.RemoveTrack(position.target, position.property));
    CHECK(Near(timeline.Duration(), 2.0f));
    CHECK(timeline.RemoveTrack(opacity.target, opacity.property));
    CHECK(Near(timeline.Duration(), 0.0f));
}

void TestToonTimelinePlayer() {
    using namespace ToolEngine::SpriteUi::Animation;

    ToonTimeline timeline;
    TimelineTrack track;
    track.target = {72, 1};
    track.property = TweenProperty::Opacity;
    track.keyframes = {{0.0f, {}, Easing::Linear},
                       {2.0f, {1, 0, 0, 0}, Easing::Linear}};
    CHECK(timeline.AddTrack(track));

    ToonTimelinePlayer player;
    std::vector<TimelineSample> samples;
    CHECK(player.State() == TimelinePlaybackState::Stopped);
    CHECK(player.Update(1.0f, timeline, samples));
    CHECK(samples.size() == 1);
    CHECK(Near(player.Time(), 0.0f));

    player.Play();
    CHECK(player.Update(0.5f, timeline, samples));
    CHECK(player.State() == TimelinePlaybackState::Playing);
    CHECK(Near(player.Time(), 0.5f));
    CHECK(Near(samples[0].value.x, 0.25f));

    player.Pause();
    CHECK(player.Update(0.5f, timeline, samples));
    CHECK(player.State() == TimelinePlaybackState::Paused);
    CHECK(Near(player.Time(), 0.5f));
    CHECK(Near(samples[0].value.x, 0.25f));

    player.Play();
    CHECK(player.Update(2.0f, timeline, samples));
    CHECK(player.State() == TimelinePlaybackState::Stopped);
    CHECK(Near(player.Time(), 2.0f));
    CHECK(Near(samples[0].value.x, 1.0f));

    CHECK(!player.Seek(std::numeric_limits<float>::infinity(), timeline));
    CHECK(player.Seek(1.5f, timeline));
    CHECK(Near(player.Time(), 1.5f));
    player.SetLooping(true);
    CHECK(player.Seek(5.0f, timeline));
    CHECK(Near(player.Time(), 1.0f));
    player.Play();
    CHECK(player.Update(2.5f, timeline, samples));
    CHECK(Near(player.Time(), 1.5f));
    CHECK(player.State() == TimelinePlaybackState::Playing);
    CHECK(!player.Update(-1.0f, timeline, samples));
    CHECK(Near(player.Time(), 1.5f));

    player.Stop();
    CHECK(player.State() == TimelinePlaybackState::Stopped);
    CHECK(Near(player.Time(), 0.0f));
    CHECK(player.Update(std::numeric_limits<float>::max(), timeline, samples));
    CHECK(Near(player.Time(), 0.0f));
}

void TestRendererAbiPremultiplicationAndBatching() {
    using namespace ToolEngine::SpriteUi::Rendering;

    CHECK(sizeof(SpriteInstanceGPU) == 80);
    const Vec4 tint = SrgbStraightToLinearPremultiplied({1, 0, 0, 0.5f});
    CHECK(Near(tint.w, 0.5f));
    CHECK(Near(tint.x, 0.5f));
    CHECK(IsValidPremultipliedTint(tint));

    UIIcon icon;
    icon.textureSlice = 3;
    icon.uvRect = {0.25f, 0.125f, 0.5f, 0.75f};
    icon.sourceSize = {32.0f, 48.0f};
    const auto iconItem = MakeIconRenderItem(
        icon, {{12, 18}, {24, 36}}, tint, {30, 2}, NoClipIndex, 1.0f, 9);
    CHECK(iconItem.has_value());
    CHECK(iconItem->textureSlice == 3);
    CHECK(Near(iconItem->uvRect[0], 0.25f));
    CHECK(Near(iconItem->uvRect[3], 0.75f));
    const FramePacket iconPacket = RenderExtractor{}.Build(42, {*iconItem});
    CHECK(iconPacket.instances.size() == 1);
    CHECK(iconPacket.instances[0].resourceIds[0] == 3);
    CHECK(Near(iconPacket.instances[0].uvRect[1], 0.125f));
    CHECK(!MakeIconRenderItem(icon, {{0, 0}, {0, 10}}, tint, {30, 2}).has_value());
    icon.uvRect[0] = 0.75f;
    CHECK(!icon.IsValid());

    UIRenderItem faded;
    faded.handle = {31, 1};
    faded.layout = {{0, 0}, {10, 10}, true, false};
    faded.tintPremultipliedLinear = tint;
    faded.opacity = 0.25f;
    const FramePacket fadedPacket = RenderExtractor{}.Build(43, {faded});
    CHECK(fadedPacket.instances.size() == 1);
    CHECK(Near(fadedPacket.instances[0].tint[0], tint.x * 0.25f));
    CHECK(Near(fadedPacket.instances[0].tint[3], tint.w * 0.25f));
    faded.opacity = 1.5f;
    const FramePacket clampedOpacityPacket = RenderExtractor{}.Build(43, {faded});
    CHECK(Near(clampedOpacityPacket.instances[0].tint[0], tint.x));
    CHECK(Near(clampedOpacityPacket.instances[0].tint[3], tint.w));
    faded.opacity = std::numeric_limits<float>::quiet_NaN();
    CHECK(RenderExtractor{}.Build(43, {faded}).instances.empty());

    std::vector<UIRenderItem> items(3);
    for (std::size_t i = 0; i < items.size(); ++i) {
        items[i].handle = {static_cast<std::uint32_t>(i + 1), 1};
        items[i].layout = {{static_cast<float>(i * 20), 0}, {10, 10}, true, false};
        items[i].tintPremultipliedLinear = {1, 1, 1, 1};
        items[i].paintOrder = i;
        items[i].textureSlice = static_cast<std::uint32_t>(i == 1 ? 2 : 1);
    }

    RenderExtractor extractor;
    ExtractionStats stats;
    const ClipRectGPU clip{0.0f, 0.0f, 100.0f, 100.0f};
    items[0].clipIndex = 0;
    const auto packet = extractor.Build(44, items, &stats, std::span<const ClipRectGPU>(&clip, 1));
    CHECK(packet.instances.size() == 3);
    CHECK(packet.clipRects.size() == 1);
    // A-B-A must remain three batches: sorting by material would break alpha order.
    CHECK(packet.batches.size() == 3);
    CHECK(IsFramePacketValid(packet));
    CHECK(stats.submittedItems == 3);
    CHECK(packet.frameId == 44);

    std::vector<UIRenderItem> tied(2);
    tied[0].handle = {9, 1};
    tied[1].handle = {3, 1};
    for (auto& item : tied) {
        item.layout = {{0, 0}, {10, 10}, true, false};
        item.tintPremultipliedLinear = {1, 1, 1, 1};
        item.paintOrder = 7;
    }
    tied[0].layout.resolvedPosition.x = 90.0f;
    tied[1].layout.resolvedPosition.x = 30.0f;
    const FramePacket tiePacket = extractor.Build(46, tied);
    CHECK(tiePacket.instances.size() == 2);
    // Equal paint keys are made deterministic by the packed generational handle.
    CHECK(Near(tiePacket.instances[0].transformRow0[2], 30.0f));
    CHECK(tiePacket.batches.size() == 1);

    items[1].clipIndex = 3;
    const auto invalidClipPacket = extractor.Build(45, items, nullptr,
        std::span<const ClipRectGPU>(&clip, 1));
    CHECK(invalidClipPacket.instances.size() == 2);
    CHECK(IsFramePacketValid(invalidClipPacket));

    FramePacket corrupt = packet;
    corrupt.batches[1].firstInstance = 0;
    CHECK(!IsFramePacketValid(corrupt));

    FramePacket badTint = packet;
    badTint.instances[0].tint[3] = 2.0f;
    CHECK(!IsFramePacketValid(badTint));

    NullRenderBackend backend;
    CHECK(backend.Submit(packet));
    CHECK(!backend.Submit(corrupt));
    CHECK(backend.GetStats().submittedFrames == 1);
}

} // namespace

// Layout engine regression tests
#include "SpriteUi/Layout/Layout.hpp"
#include "SpriteUi/Layout/LayoutTree.hpp"
static void TestFlexLayout() {
    using namespace ToolEngine::SpriteUi;
    LayoutNode root; root.style.direction=LayoutDirection::Row; root.style.width={SizeMode::Fixed,300}; root.style.height={SizeMode::Fixed,100}; root.style.gap=10;
    LayoutNode a; a.style.width={SizeMode::Fixed,100}; a.style.height={SizeMode::Fixed,40};
    LayoutNode b; b.style.width={SizeMode::Flex,0}; b.style.height={SizeMode::Fixed,50}; b.style.grow=1;
    root.children={a,b}; LayoutEngine engine; LayoutStats stats;
    (void)engine.Measure(root,{300,100},&stats); engine.Arrange(root,{{0,0},{300,100}},&stats);
    CHECK(root.children[0].rect.size.x==100.0f);
    CHECK(root.children[1].rect.size.x==190.0f);
    CHECK(root.children[1].rect.position.x==110.0f);
    CHECK(stats.measured==3 && stats.arranged==3);
}


static void TestLayoutTreeAndAlignment() {
    using namespace ToolEngine::SpriteUi;
    LayoutTree tree;
    FlexItem rootStyle;
    rootStyle.width={SizeMode::Fixed,200};
    rootStyle.height={SizeMode::Fixed,100};
    rootStyle.direction=LayoutDirection::Row;
    rootStyle.gap=10;
    const UIHandle root=tree.Create({},rootStyle);
    FlexItem childStyle;
    childStyle.width={SizeMode::Fixed,50};
    childStyle.height={SizeMode::Fixed,20};
    childStyle.alignItems=AlignItems::Center;
    const UIHandle child=tree.Create(root,childStyle);
    const UIHandle grandchild=tree.Create(child,childStyle);
    CHECK(root.IsValid() && child.IsValid() && grandchild.IsValid());
    CHECK(!tree.Create({123456u,1u},childStyle).IsValid());
    tree.Layout({{0,0},{200,100}});
    CHECK(Near(tree.Get(child)->node.rect.position.y,40.0f));
    CHECK(tree.Remove(child));
    CHECK(tree.Get(child)==nullptr);
    CHECK(tree.Get(grandchild)==nullptr);
    CHECK(tree.Get(root)!=nullptr);
    CHECK(!tree.Remove(child));
}


static void TestClipHierarchyAndInputClip() {
    using namespace ToolEngine::SpriteUi;
    using namespace ToolEngine::SpriteUi::Rendering;
    const ClipNode nodes[] = {
        {{0,0,100,100}, NoClipIndex, true},
        {{25,25,125,125}, 0, true},
        {{0,0,0,0}, 1, false}
    };
    const auto clips=BuildClipHierarchy(nodes);
    CHECK(clips.valid);
    CHECK(clips.effectiveRects.size()==2);
    CHECK(clips.nodeClipIndices[2]==clips.nodeClipIndices[1]);
    CHECK(Near(clips.effectiveRects[1].minX,25));
    CHECK(Near(clips.effectiveRects[1].maxX,100));
    const ClipNode invalid[] = {{{0,0,1,1},1,true}};
    CHECK(!BuildClipHierarchy(invalid).valid);

    Input::HitTarget target;
    target.handle={7,1};
    target.localSize={100,100};
    target.clipEnabled=true;
    target.clipRectLogical={{0,0},{50,50}};
    Input::UIInputSnapshot input;
    input.pointerPositionLogical={75,75};
    input.frameId=1;
    Input::UIEventRouter router;
    const auto events=router.Process(input,std::span<const Input::HitTarget>(&target,1));
    CHECK(events.empty());
}

static void TestFontMetricsAndTextLayout() {
    using namespace ToolEngine::SpriteUi::Text;

    FontMetrics metrics;
    metrics.fontSize = 20.0f;
    metrics.unitsPerEm = 1000.0f;
    metrics.ascent = 800.0f;
    metrics.descent = -200.0f;
    metrics.lineGap = 200.0f;
    CHECK(metrics.IsValid());
    CHECK(Near(metrics.GetScale(), 0.02f));
    CHECK(Near(metrics.GetLineHeight(), 1200.0f));

    FontMetrics invalidMetrics = metrics;
    invalidMetrics.fontSize = -1.0f;
    CHECK(!invalidMetrics.IsValid());

    // Create 3 shaped glyphs
    GlyphInstance glyphs[3];
    // Glyph 0: 'A'
    glyphs[0].glyphId = 65;
    glyphs[0].clusterByteOffset = 0;
    glyphs[0].xAdvance = 600.0f;
    glyphs[0].planeBounds[0] = 50.0f;
    glyphs[0].planeBounds[1] = 0.0f;
    glyphs[0].planeBounds[2] = 550.0f;
    glyphs[0].planeBounds[3] = 700.0f;
    glyphs[0].uvRect[0] = 0.1f; glyphs[0].uvRect[1] = 0.1f; glyphs[0].uvRect[2] = 0.2f; glyphs[0].uvRect[3] = 0.2f;

    // Glyph 1: 'B'
    glyphs[1].glyphId = 66;
    glyphs[1].clusterByteOffset = 1;
    glyphs[1].xAdvance = 600.0f;
    glyphs[1].planeBounds[0] = 50.0f;
    glyphs[1].planeBounds[1] = 0.0f;
    glyphs[1].planeBounds[2] = 550.0f;
    glyphs[1].planeBounds[3] = 700.0f;
    glyphs[1].uvRect[0] = 0.3f; glyphs[1].uvRect[1] = 0.1f; glyphs[1].uvRect[2] = 0.2f; glyphs[1].uvRect[3] = 0.2f;

    // Glyph 2: 'C'
    glyphs[2].glyphId = 67;
    glyphs[2].clusterByteOffset = 2;
    glyphs[2].xAdvance = 600.0f;
    glyphs[2].planeBounds[0] = 50.0f;
    glyphs[2].planeBounds[1] = 0.0f;
    glyphs[2].planeBounds[2] = 550.0f;
    glyphs[2].planeBounds[3] = 700.0f;
    glyphs[2].uvRect[0] = 0.5f; glyphs[2].uvRect[1] = 0.1f; glyphs[2].uvRect[2] = 0.2f; glyphs[2].uvRect[3] = 0.2f;

    TextLayoutConfig config;
    config.fontSize = 20.0f;
    auto layout = LayoutText(glyphs, metrics, config);
    CHECK(layout.quads.size() == 3);
    CHECK(layout.lineCount == 1);
    CHECK(Near(layout.size.x, 36.0f)); // 3 * (600 * 0.02) = 36.0f

    // Test caret position
    Vec2 caret0 = GetCaretPosition(layout, 0, metrics);
    CHECK(Near(caret0.x, layout.quads[0].bounds.position.x));
    Vec2 caret1 = GetCaretPosition(layout, 1, metrics);
    CHECK(Near(caret1.x, layout.quads[1].bounds.position.x));
    Vec2 caretEnd = GetCaretPosition(layout, 999, metrics);
    CHECK(Near(caretEnd.x, layout.quads[2].bounds.position.x + layout.quads[2].bounds.size.x));

    // Test find cluster
    CHECK(FindClusterAtPosition(layout, {1.0f, 5.0f}) == 0);
    CHECK(FindClusterAtPosition(layout, {25.0f, 5.0f}) == 2);

    // Test wrapping
    config.maxWidth = 20.0f;
    config.wrap = TextWrap::Char;
    auto wrappedLayout = LayoutText(glyphs, metrics, config);
    CHECK(wrappedLayout.lineCount >= 2);

    // Test render item generation
    UILayoutRect rect{{100.0f, 100.0f}, {200.0f, 50.0f}, true, false};
    auto renderItems = GenerateTextRenderItems(layout, rect, {1, 1, 1, 1}, 5);
    CHECK(renderItems.size() == 3);
    CHECK(renderItems[0].textureSlice == 5);
    CHECK(renderItems[0].flags == 0x2);
    CHECK(Near(renderItems[0].layout.resolvedPosition.x, 100.0f + layout.quads[0].bounds.position.x));
}

static void TestWidgetsAndWindowInteraction() {
    using namespace ToolEngine::SpriteUi::Widgets;
    using namespace ToolEngine::SpriteUi::Input;

    // Toggle
    UIToggleComponent toggle;
    CHECK(!toggle.isOn);
    CHECK(toggle.Toggle());
    CHECK(toggle.isOn);
    toggle.disabled = true;
    CHECK(!toggle.Toggle());
    CHECK(toggle.isOn);

    // TextInput
    UITextInputComponent textInput;
    textInput.hasFocus = true;
    textInput.UpdateBlink(0.2f);
    CHECK(textInput.IsCursorVisible(1.0f));
    textInput.UpdateBlink(0.4f); // 0.6f >= 0.5f
    CHECK(!textInput.IsCursorVisible(1.0f));

    textInput.MoveCursor(5, false);
    CHECK(textInput.cursorCluster == 5);
    CHECK(!textInput.HasSelection());
    textInput.MoveCursor(10, true);
    CHECK(textInput.cursorCluster == 10);
    CHECK(textInput.HasSelection());
    CHECK(textInput.selectionStartCluster == 5);
    textInput.ClearSelection();
    CHECK(!textInput.HasSelection());

    // Resize edges
    Vec2 pos{50.0f, 50.0f};
    Vec2 size{200.0f, 100.0f};
    CHECK(GetResizeEdge(pos, size, {52.0f, 70.0f}) == ResizeEdge::Left);
    CHECK(GetResizeEdge(pos, size, {248.0f, 70.0f}) == ResizeEdge::Right);
    CHECK(GetResizeEdge(pos, size, {100.0f, 52.0f}) == ResizeEdge::Top);
    CHECK(GetResizeEdge(pos, size, {100.0f, 148.0f}) == ResizeEdge::Bottom);
    CHECK(GetResizeEdge(pos, size, {52.0f, 52.0f}) == ResizeEdge::TopLeft);
    CHECK(GetResizeEdge(pos, size, {248.0f, 52.0f}) == ResizeEdge::TopRight);
    CHECK(GetResizeEdge(pos, size, {52.0f, 148.0f}) == ResizeEdge::BottomLeft);
    CHECK(GetResizeEdge(pos, size, {248.0f, 148.0f}) == ResizeEdge::BottomRight);
    CHECK(GetResizeEdge(pos, size, {100.0f, 80.0f}) == ResizeEdge::None);

    // Window Interaction (Dragging and Resizing)
    UIWindowComponent window;
    window.titleBarHeight = 25.0f;
    window.minSize = {100.0f, 80.0f};
    UILayoutRect layout{pos, size, true, false};
    UIRectTransform transform;
    transform.offsetMin = pos;
    transform.offsetMax = pos + size;
    UIPointerRoutingState routing;
    UIHandle winHandle{42, 1};

    // Click on titlebar
    UIInputSnapshot clickTitle;
    clickTitle.frameId = 1;
    clickTitle.pointerPositionLogical = {100.0f, 60.0f}; // Inside titlebar
    clickTitle.primaryPressed = true;
    clickTitle.primaryDown = true;
    CHECK(ProcessWindowInteraction(window, layout, transform, clickTitle, routing, winHandle));
    CHECK(window.isDragging);
    CHECK(routing.capturedEntity == winHandle);

    // Drag move
    UIInputSnapshot dragMove;
    dragMove.frameId = 2;
    dragMove.pointerPositionLogical = {110.0f, 70.0f};
    dragMove.pointerDeltaLogical = {10.0f, 10.0f};
    dragMove.primaryDown = true;
    CHECK(ProcessWindowInteraction(window, layout, transform, dragMove, routing, winHandle));
    CHECK(Near(layout.resolvedPosition.x, 60.0f));
    CHECK(Near(layout.resolvedPosition.y, 60.0f));

    // Release drag
    UIInputSnapshot release;
    release.frameId = 3;
    release.primaryReleased = true;
    CHECK(ProcessWindowInteraction(window, layout, transform, release, routing, winHandle));
    CHECK(!window.isDragging);
    CHECK(!routing.capturedEntity.IsValid());

    // Resize bottom-right
    UIInputSnapshot clickBR;
    clickBR.frameId = 4;
    clickBR.pointerPositionLogical = {layout.resolvedPosition.x + layout.resolvedSize.x - 2.0f,
                                      layout.resolvedPosition.y + layout.resolvedSize.y - 2.0f};
    clickBR.primaryPressed = true;
    clickBR.primaryDown = true;
    CHECK(ProcessWindowInteraction(window, layout, transform, clickBR, routing, winHandle));
    CHECK(window.activeResizeEdge == ResizeEdge::BottomRight);
    CHECK(routing.capturedEntity == winHandle);

    // Drag resize
    UIInputSnapshot resizeMove;
    resizeMove.frameId = 5;
    resizeMove.pointerDeltaLogical = {30.0f, 20.0f};
    resizeMove.primaryDown = true;
    CHECK(ProcessWindowInteraction(window, layout, transform, resizeMove, routing, winHandle));
    CHECK(Near(layout.resolvedSize.x, 230.0f));
    CHECK(Near(layout.resolvedSize.y, 120.0f));

    // Release resize
    release.frameId = 6;
    CHECK(ProcessWindowInteraction(window, layout, transform, release, routing, winHandle));
    CHECK(window.activeResizeEdge == ResizeEdge::None);
}

static void TestLayoutSceneBridge() {
    using namespace ToolEngine::SpriteUi;

    LayoutTree tree;
    FlexItem containerStyle;
    containerStyle.width = {SizeMode::Fixed, 400.0f};
    containerStyle.height = {SizeMode::Fixed, 300.0f};
    const UIHandle container = tree.Create({}, containerStyle);

    FlexItem childStyle;
    childStyle.width = {SizeMode::Fixed, 100.0f};
    childStyle.height = {SizeMode::Fixed, 50.0f};
    const UIHandle child = tree.Create(container, childStyle);

    tree.Layout({{0, 0}, {400, 300}});

    LayoutSceneBridge bridge;
    ElementVisual containerVis;
    containerVis.clipOverflow = true;
    containerVis.tintPremultipliedLinear = {0.1f, 0.1f, 0.1f, 1.0f};
    bridge.SetVisual(container, containerVis);

    ElementVisual childVis;
    childVis.tintPremultipliedLinear = {0.8f, 0.2f, 0.2f, 1.0f};
    bridge.SetVisual(child, childVis);

    auto result = bridge.Extract(tree);
    CHECK(result.renderItems.size() == 2);
    CHECK(result.hitTargets.size() == 2);
    CHECK(result.clipHierarchy.valid);
    CHECK(result.clipHierarchy.effectiveRects.size() == 1);
    // Child should inherit clip index from container
    CHECK(result.renderItems[1].clipIndex == 0);
    CHECK(result.hitTargets[1].clipEnabled);
    CHECK(Near(result.hitTargets[1].clipRectLogical.size.x, 400.0f));
}

static void TestUIConfigManager() {
    using namespace ToolEngine::SpriteUi::Config;

    const std::string validJson = R"({
  "schemaVersion": 1,
  "globalScale": 1.25,
  "activeTheme": 2,
  "enableVSync": true,
  "windows": [
    {
      "name": "EditorHierarchy",
      "offsetMinX": 10.0,
      "offsetMinY": 20.0,
      "offsetMaxX": 250.0,
      "offsetMaxY": 500.0,
      "isMinimized": false
    }
  ]
})";

    UISettingsConfig staged;
    CHECK(UIConfigManager::ValidateAndStage(validJson, staged));
    CHECK(staged.schemaVersion == 1);
    CHECK(Near(staged.globalScale, 1.25f));
    CHECK(staged.activeTheme == 2);
    CHECK(staged.enableVSync == true);
    CHECK(staged.windows.size() == 1);
    CHECK(staged.windows[0].name == "EditorHierarchy");
    CHECK(Near(staged.windows[0].offsetMax.x, 250.0f));

    // Test commit
    UISettingsConfig activeConfig;
    UIConfigManager::Commit(activeConfig, staged);
    CHECK(activeConfig.windows.size() == 1);
    CHECK(Near(activeConfig.globalScale, 1.25f));

    // Test rejection without schemaVersion
    const std::string badJson = R"({
  "globalScale": 2.0
})";
    UISettingsConfig badStaged;
    CHECK(!UIConfigManager::ValidateAndStage(badJson, badStaged));

    // Test serialization round-trip
    std::string serialized = UIConfigManager::Serialize(activeConfig);
    CHECK(serialized.find("\"schemaVersion\": 1") != std::string::npos);
    UISettingsConfig roundTrip;
    CHECK(UIConfigManager::ValidateAndStage(serialized, roundTrip));
    CHECK(Near(roundTrip.globalScale, 1.25f));
    CHECK(roundTrip.windows.size() == 1);

    // Test ReadFiniteFloat sanitization
    CHECK(Near(ReadFiniteFloat(5.0f, 1.0f, 0.0f, 10.0f), 5.0f));
    CHECK(Near(ReadFiniteFloat(std::numeric_limits<float>::quiet_NaN(), 2.0f), 2.0f));
    CHECK(Near(ReadFiniteFloat(100.0f, 1.0f, 0.0f, 10.0f), 10.0f));
}

static void TestFlexJustifyContent() {
    using namespace ToolEngine::SpriteUi;
    LayoutEngine engine;

    // Test Center justification
    LayoutNode root;
    root.style.direction = LayoutDirection::Row;
    root.style.width = {SizeMode::Fixed, 300.0f};
    root.style.height = {SizeMode::Fixed, 100.0f};
    root.style.justifyContent = JustifyContent::Center;

    LayoutNode a;
    a.style.width = {SizeMode::Fixed, 50.0f};
    a.style.height = {SizeMode::Fixed, 50.0f};

    LayoutNode b;
    b.style.width = {SizeMode::Fixed, 50.0f};
    b.style.height = {SizeMode::Fixed, 50.0f};

    root.children = {a, b};
    (void)engine.Measure(root, {300.0f, 100.0f});
    engine.Arrange(root, {{0.0f, 0.0f}, {300.0f, 100.0f}});

    // Free space = 300 - 100 = 200. Center offset = 100.
    CHECK(Near(root.children[0].rect.position.x, 100.0f));
    CHECK(Near(root.children[1].rect.position.x, 150.0f));

    // Test SpaceBetween justification
    root.style.justifyContent = JustifyContent::SpaceBetween;
    engine.Arrange(root, {{0.0f, 0.0f}, {300.0f, 100.0f}});
    CHECK(Near(root.children[0].rect.position.x, 0.0f));
    CHECK(Near(root.children[1].rect.position.x, 250.0f));
}

static void TestVirtualList() {
    using namespace ToolEngine::SpriteUi::Widgets;

    UIVirtualList list(1000, 30.0f, 2);
    CHECK(list.GetTotalItems() == 1000);
    CHECK(Near(list.GetItemHeight(), 30.0f));
    CHECK(Near(list.GetTotalContentHeight(), 30000.0f));
    CHECK(Near(list.GetItemOffsetY(10), 300.0f));

    // Viewport height 300 (10 items visible), scroll offset at 600 (item 20)
    const auto range = list.ComputeVisibleRange(600.0f, 300.0f);
    CHECK(range.firstIndex == 18);
    CHECK(range.lastIndex == 32);
    CHECK(range.count == 15);

    CHECK(list.IsItemVisible(20, 600.0f, 300.0f));
    CHECK(list.IsItemVisible(18, 600.0f, 300.0f));
    CHECK(!list.IsItemVisible(10, 600.0f, 300.0f));
    CHECK(!list.IsItemVisible(50, 600.0f, 300.0f));
}

static void TestTabsAndDocking() {
    using namespace ToolEngine::SpriteUi::Widgets;

    // Tabs
    UITabGroupComponent tabGroup;
    CHECK(tabGroup.AddTab("Scene", {10, 1}));
    CHECK(tabGroup.AddTab("Game", {20, 1}));
    CHECK(tabGroup.AddTab("Console", {30, 1}));
    CHECK(tabGroup.TabCount() == 3);
    CHECK(tabGroup.GetActiveContent() == (UIHandle{10, 1}));

    CHECK(tabGroup.SelectTab(1));
    CHECK(tabGroup.GetActiveContent() == (UIHandle{20, 1}));
    CHECK(tabGroup.RemoveTab(0));
    CHECK(tabGroup.TabCount() == 2);

    // Docking
    UIDockNode dockRoot({100, 1});
    CHECK(dockRoot.Split(DockSlot::Right, 0.4f, {101, 1}));
    CHECK(dockRoot.isSplit);
    CHECK(dockRoot.firstChild != nullptr);
    CHECK(dockRoot.secondChild != nullptr);

    dockRoot.Layout({{0, 0}, {1000, 600}});
    CHECK(Near(dockRoot.firstChild->bounds.size.x, 600.0f));
    CHECK(Near(dockRoot.secondChild->bounds.size.x, 400.0f));
    CHECK(Near(dockRoot.secondChild->bounds.position.x, 600.0f));
}

static void TestValueBinding() {
    using namespace ToolEngine::SpriteUi::Binding;

    UIFloatBinding health;
    CHECK(health.Update(100.0f));
    CHECK(!health.Update(100.0001f)); // Below threshold 0.001f
    CHECK(health.Update(95.0f));      // Above threshold

    UIIntBinding score;
    CHECK(score.Update(10));
    CHECK(!score.Update(10));
    CHECK(score.Update(20));

    UIBoolBinding paused;
    CHECK(paused.Update(true));
    CHECK(!paused.Update(true));
    CHECK(paused.Update(false));

    UIStringBinding playerName;
    CHECK(playerName.Update("Hero"));
    CHECK(!playerName.Update("Hero"));
    CHECK(playerName.Update("Champion"));
}

static void TestGraphsAndOscilloscope() {
    using namespace ToolEngine::SpriteUi::Widgets;

    UIOscilloscopeGraph osc;
    for (int i = 0; i < 300; ++i) {
        osc.AddSample(static_cast<float>(i));
    }
    CHECK(osc.sampleCount == UIOscilloscopeGraph::HistoryCapacity);
    CHECK(Near(osc.GetLatestSample(), 299.0f));

    float ordered[4];
    osc.GetOrderedSamples(ordered);
    // Oldest sample in a full 256 ring buffer after 300 insertions is 300 - 256 = 44
    CHECK(Near(ordered[0], 44.0f));
    CHECK(Near(ordered[1], 45.0f));

    float normalized[2];
    osc.GetNormalizedSamples(normalized, 0.0f, 300.0f);
    CHECK(normalized[0] >= 0.0f && normalized[0] <= 1.0f);

    UILinePlot plot;
    plot.AddPoint({10.0f, 20.0f});
    plot.AddPoint({50.0f, 100.0f});
    const auto bounds = plot.ComputeBounds();
    CHECK(Near(bounds.position.x, 10.0f));
    CHECK(Near(bounds.position.y, 20.0f));
    CHECK(Near(bounds.size.x, 40.0f));
    CHECK(Near(bounds.size.y, 80.0f));
}

static void TestPopupManager() {
    using namespace ToolEngine::SpriteUi::Widgets;

    UIPopupManager popupMgr;
    CHECK(!popupMgr.IsPopupOpen());

    UIHandle popupHandle{555, 1};
    popupMgr.OpenPopup(popupHandle, {{100, 100}, {150, 200}}, true);
    CHECK(popupMgr.IsPopupOpen());
    CHECK(popupMgr.GetActivePopup() == popupHandle);

    // Inside click
    auto resInside = popupMgr.HandlePointerDown({120, 150});
    CHECK(resInside == PopupInputResult::InsidePopup);
    CHECK(popupMgr.IsPopupOpen());

    // Outside click
    auto resOutside = popupMgr.HandlePointerDown({50, 50});
    CHECK(resOutside == PopupInputResult::ClosedDueToOutsideClick);
    CHECK(!popupMgr.IsPopupOpen());
}

int main() {
    TestFlexLayout();
    TestFlexJustifyContent();
    TestClipHierarchyAndInputClip();
    TestLayoutTreeAndAlignment();
    TestAnchorsAndTransforms();
    TestInputRouterClickAndCapture();
    TestMultiPointerRouter();
    TestInputRouterCancelAndOcclusion();
    TestHoverMoveEvent();
    TestInvalidInputCancelsCapture();
    TestHitTestTransformAndMaxFrameId();
    TestSpatialNavigationAndDirtyFlags();
    TestSemanticNavigationRouter();
    TestReviewRegressions();
    TestEventBubblingAndHoverConsumption();
    TestCanvasConfig();
    TestWidgets();
    TestToonMotion();
    TestToonTimeline();
    TestToonTimelinePlayer();
    TestTweenConflictPolicies();
    TestRendererAbiPremultiplicationAndBatching();
    TestFontMetricsAndTextLayout();
    TestWidgetsAndWindowInteraction();
    TestLayoutSceneBridge();
    TestUIConfigManager();
    TestVirtualList();
    TestTabsAndDocking();
    TestValueBinding();
    TestGraphsAndOscilloscope();
    TestPopupManager();

    SpriteUiTests::RunGraphSamplingTests();
    SpriteUiTests::RunTextBreakingTests();
    SpriteUiTests::RunTextWrapLayoutTests();
    SpriteUiTests::RunCaretAndHitTests();
    SpriteUiTests::RunFlexWrapTests();
    SpriteUiTests::RunFlexBaselineTests();
    SpriteUiTests::RunNineSliceTests();
    SpriteUiTests::RunRobustnessTests();
    SpriteUiTests::RunVisualTests();

    if (failures != 0) {
        std::cerr << failures << " failure(s) in " << testsRun << " checks.\n";
        return EXIT_FAILURE;
    }
    std::cout << "All " << testsRun << " checks passed.\n";
    return EXIT_SUCCESS;
}
