#include "SpriteUi/Elements/Window.hpp"

#include "SpriteUi/Elements/Paint.hpp"
#include "SpriteUi/Rendering/Primitives.hpp"

#include <algorithm>
#include <utility>

namespace ToolEngine::SpriteUi::Elements {

namespace {

bool ContainsPoint(Rect rect, Vec2 point) noexcept {
    return point.x >= rect.position.x && point.x <= rect.position.x + rect.size.x &&
           point.y >= rect.position.y && point.y <= rect.position.y + rect.size.y;
}

bool PastThreshold(Vec2 from, Vec2 to, float threshold) noexcept {
    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    return dx * dx + dy * dy >= threshold * threshold;
}

Rect Shifted(Rect rect, Vec2 delta) noexcept {
    return {{rect.position.x + delta.x, rect.position.y + delta.y}, rect.size};
}

// Desenha o traco de um botao: linha horizontal para minimizar, X para fechar.
void AppendGlyph(const Window& window, WindowButton button, const WindowStyle& style, float zOrder,
                 std::uint64_t& paint, std::vector<Rendering::UIRenderItem>& out) {
    const Rect rect = WindowButtonRect(window, button);
    const Vec2 c{rect.position.x + 0.5f * rect.size.x, rect.position.y + 0.5f * rect.size.y};
    const float h = style.glyphHalfSize;
    const float t = style.glyphThickness;
    if (button == WindowButton::Minimize) {
        if (auto line = Rendering::MakeLine(window.handle, {c.x - h, c.y + h}, {c.x + h, c.y + h}, t, style.glyph,
                                            Rendering::NoClipIndex, zOrder, paint++)) {
            out.push_back(*line);
        }
        return;
    }
    if (button == WindowButton::Close) {
        for (const auto& [from, to] : {std::pair{Vec2{c.x - h, c.y - h}, Vec2{c.x + h, c.y + h}},
                                       std::pair{Vec2{c.x + h, c.y - h}, Vec2{c.x - h, c.y + h}}}) {
            if (auto line = Rendering::MakeLine(window.handle, from, to, t, style.glyph, Rendering::NoClipIndex,
                                                zOrder, paint++)) {
                out.push_back(*line);
            }
        }
    }
}

} // namespace

WindowEvents Window::OnPointer(const PointerEvent& event) noexcept {
    WindowEvents events;
    if (states.disabled || !IsFinite(event.position)) return events;
    switch (event.phase) {
    case PointerPhase::Down:
        if (!event.inside) break;
        events.broughtToFront = true;
        grab_ = event.position;
        start_ = border;
        pressedButton_ = WindowButtonAt(*this, event.position);
        if (pressedButton_ != WindowButton::None) break;
        edges_ = WindowEdgesAt(*this, event.position);
        if (edges_ != EdgeNone) {
            drag_ = Drag::Resize;
        } else if (ContainsPoint(TitleBarRect(), event.position)) {
            drag_ = Drag::Pending;
        }
        break;
    case PointerPhase::Move:
        if (drag_ == Drag::Pending) {
            if (!PastThreshold(grab_, event.position, dragThreshold)) break;
            drag_ = Drag::Move;
        }
        if (drag_ == Drag::Move) {
            SetRect(Shifted(start_, {event.position.x - grab_.x, event.position.y - grab_.y}));
            events.moved = true;
        } else if (drag_ == Drag::Resize) {
            SetRect(ResizedWindowRect(start_, edges_, {event.position.x - grab_.x, event.position.y - grab_.y},
                                      minSize));
            events.resized = true;
        }
        break;
    case PointerPhase::Up: {
        const bool onSameButton = pressedButton_ != WindowButton::None && event.inside &&
                                  WindowButtonAt(*this, event.position) == pressedButton_;
        events.minimizeRequested = onSameButton && pressedButton_ == WindowButton::Minimize;
        events.closeRequested = onSameButton && pressedButton_ == WindowButton::Close;
        drag_ = Drag::None;
        pressedButton_ = WindowButton::None;
        edges_ = EdgeNone;
        break;
    }
    case PointerPhase::Cancel:
        if (drag_ == Drag::Move || drag_ == Drag::Resize) SetRect(start_);
        drag_ = Drag::None;
        pressedButton_ = WindowButton::None;
        edges_ = EdgeNone;
        break;
    case PointerPhase::Enter:
    case PointerPhase::Leave:
        break;
    }
    return events;
}

Rect Window::TitleBarRect() const noexcept {
    return {border.position, {border.size.x, std::clamp(titleBarHeight, 0.0f, std::max(0.0f, border.size.y))}};
}

// Move a janela para next e ajusta os offsets (a janela usa ancoras (0, 0)): o layout seguinte chega ao mesmo lugar.
void Window::SetRect(Rect next) noexcept {
    const Vec2 dMin{next.position.x - border.position.x, next.position.y - border.position.y};
    const Vec2 dMax{next.position.x + next.size.x - (border.position.x + border.size.x),
                    next.position.y + next.size.y - (border.position.y + border.size.y)};
    transform.offsetMin = {transform.offsetMin.x + dMin.x, transform.offsetMin.y + dMin.y};
    transform.offsetMax = {transform.offsetMax.x + dMax.x, transform.offsetMax.y + dMax.y};
    border = next;
}

Rect WindowButtonRect(const Window& window, WindowButton button) noexcept {
    if (button == WindowButton::None || window.buttonSize <= 0.0f) return {};
    const Rect area = ShrinkRect(window.TitleBarRect(), Insets{0.0f, window.padding.right, 0.0f, window.padding.left});
    const float size = window.buttonSize;
    const float top = area.position.y + 0.5f * (area.size.y - size);
    const float closeX = area.position.x + area.size.x - size;
    if (button == WindowButton::Close) return {{closeX, top}, {size, size}};
    return {{closeX - window.buttonGap - size, top}, {size, size}};
}

WindowButton WindowButtonAt(const Window& window, Vec2 point) noexcept {
    if (ContainsPoint(WindowButtonRect(window, WindowButton::Close), point)) return WindowButton::Close;
    if (ContainsPoint(WindowButtonRect(window, WindowButton::Minimize), point)) return WindowButton::Minimize;
    return WindowButton::None;
}

std::uint8_t WindowEdgesAt(const Window& window, Vec2 point) noexcept {
    const Rect box = window.border;
    const float margin = window.resizeMargin;
    if (margin <= 0.0f || !ContainsPoint(box, point)) return EdgeNone;
    std::uint8_t edges = EdgeNone;
    if (point.x - box.position.x < margin) edges |= EdgeLeft;
    if (box.position.x + box.size.x - point.x < margin) edges |= EdgeRight;
    if (point.y - box.position.y < margin) edges |= EdgeTop;
    if (box.position.y + box.size.y - point.y < margin) edges |= EdgeBottom;
    return edges;
}

Rect ResizedWindowRect(Rect start, std::uint8_t edges, Vec2 delta, Vec2 minSize) noexcept {
    float left = start.position.x;
    float top = start.position.y;
    float right = start.position.x + start.size.x;
    float bottom = start.position.y + start.size.y;
    if (edges & EdgeLeft) left += delta.x;
    if (edges & EdgeRight) right += delta.x;
    if (edges & EdgeTop) top += delta.y;
    if (edges & EdgeBottom) bottom += delta.y;

    // Minimo: a borda que se move e puxada de volta; a oposta fica parada.
    if (right - left < minSize.x) {
        if (edges & EdgeLeft) {
            left = right - minSize.x;
        } else {
            right = left + minSize.x;
        }
    }
    if (bottom - top < minSize.y) {
        if (edges & EdgeTop) {
            top = bottom - minSize.y;
        } else {
            bottom = top + minSize.y;
        }
    }
    return {{left, top}, {right - left, bottom - top}};
}

// Barra de titulo por dentro do contorno: a faixa da borda fica com a cor do contorno, como antes.
Rect TitleBarInside(const Window& window, float borderWidth) noexcept {
    const Rect bar = window.TitleBarRect();
    return {{bar.position.x + borderWidth, bar.position.y + borderWidth},
            {std::max(0.0f, bar.size.x - 2.0f * borderWidth), std::max(0.0f, bar.size.y - borderWidth)}};
}

std::vector<Rendering::UIRenderItem> BuildWindowItems(const Window& window, const WindowStyle& style, float zOrder,
                                                      std::uint64_t paintOrder) {
    std::vector<Rendering::UIRenderItem> out;
    const Rect box = window.border;
    if (!IsFinite(box.position) || !IsFinite(box.size)) return out;

    std::uint64_t paint = paintOrder;
    AppendBox(window.handle, box, style.cornerRadius, style.borderWidth, style.frame, style.frameBorder,
              Rendering::NoClipIndex, zOrder, paint, out);
    const float innerRadius = std::max(0.0f, style.cornerRadius - style.borderWidth);
    AppendFill(window.handle, TitleBarInside(window, style.borderWidth), innerRadius, style.titleBar,
               Rendering::NoClipIndex, zOrder, paint, out);
    for (WindowButton button : {WindowButton::Minimize, WindowButton::Close}) {
        const Rect rect = WindowButtonRect(window, button);
        if (rect.size.x <= 0.0f) continue;
        out.push_back(Rendering::MakeSolidRect(window.handle, rect, style.buttonBackground, Rendering::NoClipIndex,
                                               zOrder, paint++));
        AppendGlyph(window, button, style, zOrder, paint, out);
    }
    return out;
}

} // namespace ToolEngine::SpriteUi::Elements
