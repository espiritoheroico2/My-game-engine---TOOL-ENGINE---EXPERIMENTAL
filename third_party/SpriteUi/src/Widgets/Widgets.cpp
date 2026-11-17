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
        window.dragPending = false;
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

        // Check title bar for dragging. O arraste so comeca depois do limiar: um clique na barra nao move a janela.
        const Rect titleBarRect{pos, {size.x, window.titleBarHeight}};
        if (titleBarRect.Contains(mouse)) {
            window.isDragging = false;
            window.dragPending = true;
            window.pressPosition = mouse;
            window.activeResizeEdge = ResizeEdge::None;
            routing.capturedEntity = windowHandle;
            routing.focusedEntity = windowHandle;
            routing.consumedThisFrame = true;
            return true;
        }
    }

    // 3. Process active dragging or resizing
    if (input.primaryDown && routing.capturedEntity == windowHandle) {
        if (window.dragPending) {
            const Vec2 travel = mouse - window.pressPosition;
            if (travel.x * travel.x + travel.y * travel.y < window.dragThreshold * window.dragThreshold) {
                routing.consumedThisFrame = true;   // tremor do ponteiro: segura a janela no lugar
                return true;
            }
            // Passou do limiar: a janela anda o deslocamento total desde o pressionamento, sem salto.
            window.dragPending = false;
            window.isDragging = true;
            transform.offsetMin += travel;
            transform.offsetMax += travel;
            layout.resolvedPosition += travel;
            layout.dirty = true;
            routing.consumedThisFrame = true;
            return true;
        }
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

