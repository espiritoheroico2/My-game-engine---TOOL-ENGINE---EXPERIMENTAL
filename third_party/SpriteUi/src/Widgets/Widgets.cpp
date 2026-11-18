#include "SpriteUi/Widgets/Widgets.hpp"

namespace ToolEngine::SpriteUi::Widgets {

bool ProcessWindowInteraction(
    UIWindowComponent& window,
    UILayoutRect& layout,
    UIRectTransform& transform,
    const Input::UIInputSnapshot& input,
    Input::UIPointerRoutingState& routing,
    UIHandle windowHandle) noexcept
{
    if (!layout.visible || !IsFinite(layout.resolvedPosition) || !IsFinite(layout.resolvedSize) ||
        !IsFinite(input.pointerPositionLogical) || !IsFinite(input.pointerDeltaLogical)) {
        return false;
    }

    const Vec2 pos = layout.resolvedPosition;
    const Vec2 size = layout.resolvedSize;
    const Vec2 mouse = input.pointerPositionLogical;

    // 1. Release capture on pointer up
    if (input.primaryReleased && routing.capturedEntity == windowHandle) {
        window.isDragging = false;
        window.activeResizeEdge = ResizeEdge::None;
        routing.capturedEntity = InvalidUIHandle;
        return true;
    }

    // 2. Start drag or resize on pointer down
    if (input.primaryPressed) {
        // Check resize edges first (edges surround the window)
        const ResizeEdge edge = GetResizeEdge(pos, size, mouse);
        if (edge != ResizeEdge::None) {
            window.activeResizeEdge = edge;
            window.isDragging = false;
            routing.capturedEntity = windowHandle;
            routing.focusedEntity = windowHandle;
            routing.consumedThisFrame = true;
            return true;
        }

        // Check title bar for dragging
        const Rect titleBarRect{pos, {size.x, window.titleBarHeight}};
        if (titleBarRect.Contains(mouse)) {
            window.isDragging = true;
            window.activeResizeEdge = ResizeEdge::None;
            routing.capturedEntity = windowHandle;
            routing.focusedEntity = windowHandle;
            routing.consumedThisFrame = true;
            return true;
        }
    }

    // 3. Process active dragging or resizing
    if (input.primaryDown && routing.capturedEntity == windowHandle) {
        if (window.isDragging) {
            transform.offsetMin += input.pointerDeltaLogical;
            transform.offsetMax += input.pointerDeltaLogical;
            layout.resolvedPosition += input.pointerDeltaLogical;
            layout.dirty = true;
            routing.consumedThisFrame = true;
            return true;
        }

        if (window.activeResizeEdge != ResizeEdge::None) {
            const ResizeEdge edge = window.activeResizeEdge;
            const float deltaX = input.pointerDeltaLogical.x;
            const float deltaY = input.pointerDeltaLogical.y;

            // Horizontal resize
            if (edge == ResizeEdge::Right || edge == ResizeEdge::TopRight || edge == ResizeEdge::BottomRight) {
                const float newWidth = std::max(window.minSize.x, layout.resolvedSize.x + deltaX);
                const float dw = newWidth - layout.resolvedSize.x;
                transform.offsetMax.x += dw;
                layout.resolvedSize.x = newWidth;
            } else if (edge == ResizeEdge::Left || edge == ResizeEdge::TopLeft || edge == ResizeEdge::BottomLeft) {
                const float newWidth = std::max(window.minSize.x, layout.resolvedSize.x - deltaX);
                const float dx = layout.resolvedSize.x - newWidth;
                transform.offsetMin.x += dx;
                layout.resolvedPosition.x += dx;
                layout.resolvedSize.x = newWidth;
            }

            // Vertical resize
            if (edge == ResizeEdge::Bottom || edge == ResizeEdge::BottomLeft || edge == ResizeEdge::BottomRight) {
                const float newHeight = std::max(window.minSize.y, layout.resolvedSize.y + deltaY);
                const float dh = newHeight - layout.resolvedSize.y;
                transform.offsetMax.y += dh;
                layout.resolvedSize.y = newHeight;
            } else if (edge == ResizeEdge::Top || edge == ResizeEdge::TopLeft || edge == ResizeEdge::TopRight) {
                const float newHeight = std::max(window.minSize.y, layout.resolvedSize.y - deltaY);
                const float dy = layout.resolvedSize.y - newHeight;
                transform.offsetMin.y += dy;
                layout.resolvedPosition.y += dy;
                layout.resolvedSize.y = newHeight;
            }

            layout.dirty = true;
            routing.consumedThisFrame = true;
            return true;
        }
    }

    return false;
}

} // namespace ToolEngine::SpriteUi::Widgets

