#include "SpriteUi/Widgets/WindowFrame.hpp"

#include <algorithm>

namespace ToolEngine::SpriteUi::Widgets {

namespace {

bool ContainsPoint(Rect rect, Vec2 point) noexcept {
    return point.x >= rect.position.x && point.x <= rect.position.x + rect.size.x &&
           point.y >= rect.position.y && point.y <= rect.position.y + rect.size.y;
}

Rect InsetRect(Rect rect, float inset) noexcept {
    const float width = std::max(0.0f, rect.size.x - 2.0f * inset);
    const float height = std::max(0.0f, rect.size.y - 2.0f * inset);
    return {{rect.position.x + inset, rect.position.y + inset}, {width, height}};
}

} // namespace

Rect WindowFrameHeader(Rect frame, const WindowFrameMetrics& metrics, bool docked) noexcept {
    const float height = std::clamp(metrics.headerHeight, 0.0f, std::max(0.0f, frame.size.y));
    const float width = docked ? std::min(std::max(0.0f, metrics.tabWidth), frame.size.x) : frame.size.x;
    return {frame.position, {width, height}};
}

Rect WindowFrameContent(Rect frame, const WindowFrameMetrics& metrics) noexcept {
    const Rect inside = InsetRect(frame, std::max(0.0f, metrics.borderWidth));
    const float top = std::clamp(metrics.headerHeight, 0.0f, inside.size.y);
    return {{inside.position.x, inside.position.y + top}, {inside.size.x, inside.size.y - top}};
}

Rect WindowFrameButtonRect(Rect frame, const WindowFrameMetrics& metrics, bool docked,
                           WindowFrameButton button) noexcept {
    const Rect header = WindowFrameHeader(frame, metrics, docked);
    const float size = std::max(0.0f, metrics.buttonSize);
    if (size <= 0.0f || header.size.y < size) return {};

    const float top = header.position.y + 0.5f * (header.size.y - size);
    const float closeX = header.position.x + header.size.x - metrics.buttonInset - size;
    if (button == WindowFrameButton::Close) {
        if (closeX < header.position.x) return {};
        return {{closeX, top}, {size, size}};
    }
    if (docked) return {}; // a aba so tem o botao de fechar
    const float minimizeX = closeX - metrics.buttonGap - size;
    if (minimizeX < header.position.x) return {};
    return {{minimizeX, top}, {size, size}};
}

Rect WindowFrameTitleRect(Rect frame, const WindowFrameMetrics& metrics, bool docked, float textPadding) noexcept {
    const Rect header = WindowFrameHeader(frame, metrics, docked);
    // O botao mais a esquerda que coube limita o titulo. Sem botoes, o titulo vai ate a borda direita.
    const Rect minimize = WindowFrameButtonRect(frame, metrics, docked, WindowFrameButton::Minimize);
    const Rect close = WindowFrameButtonRect(frame, metrics, docked, WindowFrameButton::Close);
    float right = header.position.x + header.size.x;
    if (close.size.x > 0.0f) right = close.position.x - metrics.buttonGap;
    if (minimize.size.x > 0.0f) right = minimize.position.x - metrics.buttonGap;
    const float left = header.position.x + textPadding;
    return {{left, header.position.y}, {std::max(0.0f, right - left), header.size.y}};
}

float WindowFrameCornerRadius(Rect frame, float requested) noexcept {
    if (!IsFinite(requested) || !IsFinite(frame.size)) return 0.0f;
    const float limit = 0.5f * std::min(frame.size.x, frame.size.y);
    return std::clamp(requested, 0.0f, std::max(0.0f, limit));
}

WindowFramePart WindowFramePartAt(Rect frame, const WindowFrameMetrics& metrics, bool docked, Vec2 point) noexcept {
    if (!IsFinite(point) || !ContainsPoint(frame, point)) return WindowFramePart::Outside;
    // Botao ausente tem tamanho zero: nao pode casar com o ponto (0, 0).
    const Rect close = WindowFrameButtonRect(frame, metrics, docked, WindowFrameButton::Close);
    if (close.size.x > 0.0f && ContainsPoint(close, point)) return WindowFramePart::Close;
    const Rect minimize = WindowFrameButtonRect(frame, metrics, docked, WindowFrameButton::Minimize);
    if (minimize.size.x > 0.0f && ContainsPoint(minimize, point)) return WindowFramePart::Minimize;
    if (ContainsPoint(WindowFrameHeader(frame, metrics, docked), point)) return WindowFramePart::Header;
    if (ContainsPoint(WindowFrameContent(frame, metrics), point)) return WindowFramePart::Content;
    return WindowFramePart::Border;
}

float WindowFrameClampScroll(float offset, float contentExtent, float viewExtent) noexcept {
    if (!IsFinite(offset) || !IsFinite(contentExtent) || !IsFinite(viewExtent)) return 0.0f;
    const float range = std::max(0.0f, contentExtent - viewExtent);
    return std::clamp(offset, 0.0f, range);
}

} // namespace ToolEngine::SpriteUi::Widgets
