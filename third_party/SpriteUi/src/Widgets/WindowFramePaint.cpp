#include "SpriteUi/Widgets/WindowFramePaint.hpp"

#include "SpriteUi/Elements/Paint.hpp"
#include "SpriteUi/Rendering/Primitives.hpp"

#include <algorithm>
#include <utility>

namespace ToolEngine::SpriteUi::Widgets {

namespace {

Rect Inset(Rect rect, float inset) noexcept {
    return {{rect.position.x + inset, rect.position.y + inset},
            {std::max(0.0f, rect.size.x - 2.0f * inset), std::max(0.0f, rect.size.y - 2.0f * inset)}};
}

const Vec4& HeaderColor(const WindowFrameState& state, const WindowFrameColors& colors) noexcept {
    if (state.dragging) return colors.headerDragging;
    return state.active ? colors.headerActive : colors.headerInactive;
}

// Cabecalho com cantos de cima arredondados e cantos de baixo retos. Desenha o retangulo arredondado e depois
// uma faixa reta de raio de altura no fundo, que cobre os cantos de baixo.
void AppendHeader(UIHandle owner, Rect header, float radius, Vec4 color, float zOrder, std::uint64_t& paint,
                  std::vector<Rendering::UIRenderItem>& out) {
    const float r = WindowFrameCornerRadius(header, radius);
    Elements::AppendFill(owner, header, r, color, Rendering::NoClipIndex, zOrder, paint, out);
    if (r <= 0.0f) return;
    const Rect squareBottom{{header.position.x, header.position.y + header.size.y - r}, {header.size.x, r}};
    Elements::AppendFill(owner, squareBottom, 0.0f, color, Rendering::NoClipIndex, zOrder, paint, out);
}

// Traco de um botao: linha para minimizar, X para fechar.
void AppendGlyph(Rect button, WindowFrameButton kind, Vec4 color, UIHandle owner, float zOrder, std::uint64_t& paint,
                 std::vector<Rendering::UIRenderItem>& out) {
    const Vec2 c{button.position.x + 0.5f * button.size.x, button.position.y + 0.5f * button.size.y};
    const float h = kWindowFrameGlyphHalfSize;
    const float t = kWindowFrameGlyphThickness;
    if (kind == WindowFrameButton::Minimize) {
        if (auto line = Rendering::MakeLine(owner, {c.x - h, c.y + h}, {c.x + h, c.y + h}, t, color,
                                            Rendering::NoClipIndex, zOrder, paint++)) {
            out.push_back(*line);
        }
        return;
    }
    const std::pair<Vec2, Vec2> diagonals[2] = {{{c.x - h, c.y - h}, {c.x + h, c.y + h}},
                                                {{c.x + h, c.y - h}, {c.x - h, c.y + h}}};
    for (const auto& [from, to] : diagonals) {
        if (auto line = Rendering::MakeLine(owner, from, to, t, color, Rendering::NoClipIndex, zOrder, paint++)) {
            out.push_back(*line);
        }
    }
}

} // namespace

std::vector<Rendering::UIRenderItem> BuildWindowFrameItems(UIHandle owner, Rect frame,
                                                           const WindowFrameMetrics& metrics,
                                                           const WindowFrameState& state,
                                                           const WindowFrameColors& colors, float zOrder,
                                                           std::uint64_t paintOrder) {
    std::vector<Rendering::UIRenderItem> out;
    if (!IsFinite(frame.position) || !IsFinite(frame.size) || frame.size.x <= 0.0f || frame.size.y <= 0.0f) {
        return out;
    }
    std::uint64_t paint = paintOrder;
    const float radius = WindowFrameCornerRadius(frame, metrics.cornerRadius);
    const Vec4& border = state.dragging ? colors.borderDragging : colors.border;
    Elements::AppendBox(owner, frame, radius, metrics.borderWidth, colors.body, border, Rendering::NoClipIndex, zOrder,
                        paint, out);

    const float innerRadius = std::max(0.0f, radius - metrics.borderWidth);
    const Rect header = Inset(WindowFrameHeader(frame, metrics, state.docked), metrics.borderWidth);
    AppendHeader(owner, header, innerRadius, HeaderColor(state, colors), zOrder, paint, out);

    for (WindowFrameButton kind : {WindowFrameButton::Minimize, WindowFrameButton::Close}) {
        const Rect button = WindowFrameButtonRect(frame, metrics, state.docked, kind);
        if (button.size.x <= 0.0f) continue;
        AppendGlyph(button, kind, colors.glyph, owner, zOrder, paint, out);
    }
    return out;
}

} // namespace ToolEngine::SpriteUi::Widgets
