#pragma once

#include "SpriteUi/Core/Handle.hpp"
#include "SpriteUi/Core/Math.hpp"
#include "SpriteUi/Core/Transform2D.hpp"
#include "SpriteUi/Input/InputRouter.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ToolEngine::SpriteUi::Widgets {

struct UISliderComponent {
    float value = 0.5f; // Value in domain [minValue, maxValue].
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float step = 0.0f;  // Zero means continuous.
    bool isDragging = false;

    [[nodiscard]] bool SetValue(float candidate) noexcept {
        if (!IsFinite(candidate) || !IsFinite(minValue) || !IsFinite(maxValue) ||
            maxValue < minValue || !IsFinite(step) || step < 0.0f) {
            return false;
        }
        float next = std::clamp(candidate, minValue, maxValue);
        if (step > 0.0f) {
            next = minValue + std::round((next - minValue) / step) * step;
            next = std::clamp(next, minValue, maxValue);
        }
        value = next;
        return true;
    }

    // Valor normalizado [0,1] apenas para desenho; o valor armazenado permanece no dominio real.
    [[nodiscard]] float Normalized() const noexcept {
        const float range = maxValue - minValue;
        if (!IsFinite(value) || !IsFinite(range) || range <= 1.0e-12f) return 0.0f;
        return Clamp01((value - minValue) / range);
    }

    [[nodiscard]] bool SetFromPointer(float localX, float viewportWidth) noexcept {
        if (!IsFinite(localX) || !IsFinite(viewportWidth) || viewportWidth <= 1.0e-6f ||
            !IsFinite(minValue) || !IsFinite(maxValue) || maxValue < minValue) {
            return false;
        }
        const float normalized = Clamp01(localX / viewportWidth);
        return SetValue(minValue + normalized * (maxValue - minValue));
    }
};

struct UIScrollViewComponent {
    Vec2 scrollOffset{};
    Vec2 contentSize{};
    Vec2 viewportSize{};
    bool allowScrollX = false;
    bool allowScrollY = true;

    void ClampScroll() noexcept {
        if (!IsFinite(scrollOffset)) scrollOffset = {};
        if (!IsFinite(contentSize)) contentSize = {};
        if (!IsFinite(viewportSize)) viewportSize = {};

        const float maxX = std::max(0.0f, contentSize.x - viewportSize.x);
        const float maxY = std::max(0.0f, contentSize.y - viewportSize.y);
        scrollOffset.x = allowScrollX ? std::clamp(scrollOffset.x, 0.0f, maxX) : 0.0f;
        scrollOffset.y = allowScrollY ? std::clamp(scrollOffset.y, 0.0f, maxY) : 0.0f;
    }

    void ScrollBy(Vec2 delta) noexcept {
        if (!IsFinite(delta)) return;
        scrollOffset += delta;
        ClampScroll();
    }
};

// Forma livre normativa (documento V4.2, secao 3.1): desconta a viewport do conteudo.
inline void ClampScroll(UIScrollViewComponent& scroll, Vec2 viewportSize) noexcept {
    scroll.viewportSize = viewportSize;
    scroll.ClampScroll();
}

struct UIButtonState {
    bool hovered = false;
    bool pressed = false;
    bool disabled = false;
};

struct UIToggleComponent {
    bool isOn = false;
    bool disabled = false;
    Vec4 onColor{0.2f, 0.8f, 0.2f, 1.0f};
    Vec4 offColor{0.5f, 0.5f, 0.5f, 1.0f};

    [[nodiscard]] bool Toggle() noexcept {
        if (disabled) return false;
        isOn = !isOn;
        return true;
    }
};

enum class ResizeEdge : std::uint8_t {
    None,
    Left,
    Right,
    Top,
    Bottom,
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight
};

[[nodiscard]] inline ResizeEdge GetResizeEdge(
    Vec2 pos,
    Vec2 size,
    Vec2 pointerPos,
    float edgeThickness = 6.0f) noexcept
{
    if (!IsFinite(pos) || !IsFinite(size) || !IsFinite(pointerPos) ||
        size.x <= 0.0f || size.y <= 0.0f || edgeThickness <= 0.0f) {
        return ResizeEdge::None;
    }

    const bool inX = pointerPos.x >= pos.x && pointerPos.x <= pos.x + size.x;
    const bool inY = pointerPos.y >= pos.y && pointerPos.y <= pos.y + size.y;
    if (!inX || !inY) return ResizeEdge::None;

    const bool left = pointerPos.x <= pos.x + edgeThickness;
    const bool right = pointerPos.x >= pos.x + size.x - edgeThickness;
    const bool top = pointerPos.y <= pos.y + edgeThickness;
    const bool bottom = pointerPos.y >= pos.y + size.y - edgeThickness;

    if (top && left) return ResizeEdge::TopLeft;
    if (top && right) return ResizeEdge::TopRight;
    if (bottom && left) return ResizeEdge::BottomLeft;
    if (bottom && right) return ResizeEdge::BottomRight;
    if (left) return ResizeEdge::Left;
    if (right) return ResizeEdge::Right;
    if (top) return ResizeEdge::Top;
    if (bottom) return ResizeEdge::Bottom;

    return ResizeEdge::None;
}

struct UIWindowComponent {
    char title[64]{"Window"};
    bool isMinimized = false;
    Vec2 minSize{150.0f, 100.0f};
    float titleBarHeight = 30.0f;
    ResizeEdge activeResizeEdge = ResizeEdge::None;
    bool isDragging = false;
};

// Pure logic for window title dragging and 8-edge resizing.
// Modifies layout/transform offsets in-place and manages capture/focus.
bool ProcessWindowInteraction(
    UIWindowComponent& window,
    UILayoutRect& layout,
    UIRectTransform& transform,
    const Input::UIInputSnapshot& input,
    Input::UIPointerRoutingState& routing,
    UIHandle windowHandle) noexcept;

struct UITextInputComponent {
    bool hasFocus = false;
    std::uint32_t cursorCluster = 0;
    std::int32_t selectionStartCluster = -1; // -1 means no selection
    float blinkTimer = 0.0f;

    [[nodiscard]] bool IsCursorVisible(float blinkPeriod = 1.0f) const noexcept {
        if (!hasFocus || blinkPeriod <= 0.0f) return false;
        return std::fmod(blinkTimer, blinkPeriod) < (blinkPeriod * 0.5f);
    }

    void UpdateBlink(float dt) noexcept {
        if (!IsFinite(dt) || dt <= 0.0f) return;
        blinkTimer += dt;
    }

    void ClearSelection() noexcept {
        selectionStartCluster = -1;
    }

    [[nodiscard]] bool HasSelection() const noexcept {
        return selectionStartCluster >= 0 &&
               static_cast<std::uint32_t>(selectionStartCluster) != cursorCluster;
    }

    void MoveCursor(std::uint32_t targetCluster, bool extendSelection = false) noexcept {
        if (extendSelection) {
            if (selectionStartCluster < 0) {
                selectionStartCluster = static_cast<std::int32_t>(cursorCluster);
            }
        } else {
            ClearSelection();
        }
        cursorCluster = targetCluster;
        blinkTimer = 0.0f; // Reset blink so cursor shows immediately
    }
};

} // namespace ToolEngine::SpriteUi::Widgets
