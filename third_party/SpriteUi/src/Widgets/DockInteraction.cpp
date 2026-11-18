#include "SpriteUi/Widgets/DockInteraction.hpp"

#include <algorithm>
#include <cmath>

namespace ToolEngine::SpriteUi::Widgets {

DockSlot ComputeDropSlot(Rect leaf, Vec2 pointer, float edgeFraction) noexcept {
    if (!leaf.Contains(pointer)) return DockSlot::None;
    if (!(edgeFraction > 0.0f) || !(edgeFraction <= 0.5f)) return DockSlot::None;

    // Distancia normalizada a cada borda, dividida pela largura da zona. Menor que 1 = dentro da zona.
    const float zoneW = edgeFraction * leaf.size.x;
    const float zoneH = edgeFraction * leaf.size.y;
    const float left = (pointer.x - leaf.position.x) / zoneW;
    const float right = (leaf.position.x + leaf.size.x - pointer.x) / zoneW;
    const float top = (pointer.y - leaf.position.y) / zoneH;
    const float bottom = (leaf.position.y + leaf.size.y - pointer.y) / zoneH;

    DockSlot best = DockSlot::Center;
    float bestValue = 1.0f;   // so vence quem estiver dentro da zona
    const struct { float value; DockSlot slot; } candidates[] = {
        {left, DockSlot::Left}, {right, DockSlot::Right}, {top, DockSlot::Top}, {bottom, DockSlot::Bottom}};
    for (const auto& c : candidates) {
        if (c.value < bestValue) {
            bestValue = c.value;
            best = c.slot;
        }
    }
    return best;
}

Rect ClampWindowToScreen(Rect window, Rect screen) noexcept {
    if (!(screen.size.x > 0.0f) || !(screen.size.y > 0.0f)) return window;
    window.size.x = std::min(window.size.x, screen.size.x);
    window.size.y = std::min(window.size.y, screen.size.y);
    const float maxX = screen.position.x + screen.size.x - window.size.x;
    const float maxY = screen.position.y + screen.size.y - window.size.y;
    window.position.x = std::clamp(window.position.x, screen.position.x, maxX);
    window.position.y = std::clamp(window.position.y, screen.position.y, maxY);
    return window;
}

void DockDragController::BeginPress(UIHandle window, Vec2 pointer) noexcept {
    window_ = window;
    pressPosition_ = pointer;
    lastPosition_ = pointer;
    pressed_ = true;
    dragging_ = false;
}

bool DockDragController::Move(Vec2 pointer) noexcept {
    if (!pressed_) return false;
    lastPosition_ = pointer;
    if (!dragging_) {
        const Vec2 d = pointer - pressPosition_;
        if (std::sqrt(d.x * d.x + d.y * d.y) >= config_.dragThreshold) dragging_ = true;
    }
    return dragging_;
}

DockDropResult DockDragController::Release(Vec2 pointer, std::span<const DockLeafRect> leaves) noexcept {
    DockDropResult result{};
    const bool wasDragging = pressed_ && dragging_;
    lastPosition_ = pointer;
    pressed_ = false;
    dragging_ = false;
    if (!wasDragging) return result;   // clique sem arrasto: nenhuma acao

    for (const DockLeafRect& leaf : leaves) {
        if (!leaf.leaf.IsValid() || !leaf.bounds.Contains(pointer)) continue;
        const DockSlot slot = ComputeDropSlot(leaf.bounds, pointer, config_.edgeFraction);
        result.targetLeaf = leaf.leaf;
        if (slot == DockSlot::Center) {
            result.action = DockAction::Tab;
        } else if (slot != DockSlot::None) {
            result.action = DockAction::Dock;
            result.slot = slot;
        }
        return result;
    }
    result.action = DockAction::Float;   // fora de todas as folhas
    return result;
}

void DockDragController::Cancel() noexcept {
    pressed_ = false;
    dragging_ = false;
}

} // namespace ToolEngine::SpriteUi::Widgets
