#include "SpriteUi/SpriteUi.hpp"

#include <iostream>
#include <span>

using namespace ToolEngine::SpriteUi;

namespace {
const char* EventName(Input::UIEventType type) {
    using Input::UIEventType;
    switch (type) {
    case UIEventType::PointerEnter: return "PointerEnter";
    case UIEventType::PointerLeave: return "PointerLeave";
    case UIEventType::PointerDown: return "PointerDown";
    case UIEventType::PointerMove: return "PointerMove";
    case UIEventType::PointerUp: return "PointerUp";
    case UIEventType::Click: return "Click";
    case UIEventType::Wheel: return "Wheel";
    case UIEventType::Cancel: return "Cancel";
    case UIEventType::FocusGained: return "FocusGained";
    case UIEventType::FocusLost: return "FocusLost";
    }
    return "Unknown";
}
}

int main() {
    using namespace ToolEngine::SpriteUi::Input;
    using namespace ToolEngine::SpriteUi::Widgets;

    const UILayoutRect parentLayout{{0.0f, 0.0f}, {640.0f, 360.0f}, true, false};
    UIRectTransform parentTransform;
    const auto parentMatrix = BuildHitTestTransform(parentLayout, parentTransform);

    const UILayoutRect buttonLayout{{80.0f, 90.0f}, {220.0f, 56.0f}, true, false};
    UIRectTransform buttonTransform;
    buttonTransform.pivot = {0.5f, 0.5f};
    buttonTransform.rotationRadians = 0.08f;
    const auto buttonMatrix = BuildHitTestTransform(buttonLayout, buttonTransform);
    if (!parentMatrix || !buttonMatrix) return 1;

    HitTarget panel;
    panel.handle = {1, 1};
    panel.localToScreen = *parentMatrix;
    panel.localSize = parentLayout.resolvedSize;
    panel.blocksInput = true;

    HitTarget button;
    button.handle = {2, 1};
    button.parent = panel.handle;
    button.localToScreen = *buttonMatrix;
    button.localSize = buttonLayout.resolvedSize;
    button.focusable = true;
    button.blocksInput = false; // O painel pai consome input do mundo.
    button.paintOrder = 10;

    const HitTarget targets[] = {panel, button};
    const Vec2 clickPosition = buttonMatrix->TransformPoint({110.0f, 28.0f});

    UIEventRouter pointerRouter;
    UIInputSnapshot down;
    down.frameId = 1;
    down.pointerPositionLogical = clickPosition;
    down.primaryDown = true;
    down.primaryPressed = true;
    const auto downEvents = pointerRouter.Process(down, targets);

    UIInputSnapshot up;
    up.frameId = 2;
    up.pointerPositionLogical = clickPosition;
    up.primaryReleased = true;
    const auto upEvents = pointerRouter.Process(up, targets);

    std::cout << "SpriteUI - exemplo de input\n";
    for (const UIEvent& event : downEvents) {
        std::cout << "  " << EventName(event.type) << " -> handle " << event.target.Pack() << '\n';
    }
    for (const UIEvent& event : upEvents) {
        std::cout << "  " << EventName(event.type) << " -> handle " << event.target.Pack() << '\n';
    }
    std::cout << "  Input consumido: "
              << (pointerRouter.GetState().consumedThisFrame ? "sim" : "nao") << '\n';

    const UIFocusCandidate focus[] = {
        {button.handle, {{80.0f, 90.0f}, {220.0f, 56.0f}}, true, true, true, 0},
        {{3, 1}, {{360.0f, 90.0f}, {180.0f, 56.0f}}, true, true, true, 1}
    };
    UINavigationRouter navigation;
    (void)navigation.Process(UINavigationCommand::None, focus);
    const auto activated = navigation.Process(UINavigationCommand::Activate, focus);
    std::cout << "  Evento de navegacao Activate: " << activated.size() << '\n';

    UISliderComponent slider;
    (void)slider.SetFromPointer(75.0f, 100.0f);
    std::cout << "  Slider value: " << slider.value << '\n';

    UIScrollViewComponent scroll;
    scroll.contentSize = {1200.0f, 1600.0f};
    scroll.viewportSize = {600.0f, 400.0f};
    scroll.allowScrollX = true;
    scroll.allowScrollY = true;
    scroll.ScrollBy({100.0f, 250.0f});
    std::cout << "  Scroll offset: " << scroll.scrollOffset.x << ", "
              << scroll.scrollOffset.y << '\n';

    return 0;
}
