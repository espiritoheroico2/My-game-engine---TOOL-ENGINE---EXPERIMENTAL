#include "SpriteUi/Elements/Panel.hpp"

#include "SpriteUi/Elements/Paint.hpp"
#include "SpriteUi/Rendering/Primitives.hpp"

#include <algorithm>
#include <cmath>

namespace ToolEngine::SpriteUi::Elements {

namespace {

// Tamanho do conteudo; valores nao finitos contam como zero.
Vec2 SafeContentSize(const Panel& panel) noexcept {
    return IsFinite(panel.contentSize) ? panel.contentSize : Vec2{0.0f, 0.0f};
}

Vec2 ClampScroll(Vec2 value, Vec2 max) noexcept {
    return {std::clamp(value.x, 0.0f, max.x), std::clamp(value.y, 0.0f, max.y)};
}

} // namespace

void Panel::ScrollBy(Vec2 delta) noexcept {
    if (!IsFinite(delta)) return;
    scroll = ClampScroll({scroll.x + delta.x, scroll.y + delta.y}, MaxScroll());
}

void Panel::ScrollByWheel(float steps) noexcept {
    if (!std::isfinite(steps)) return;
    ScrollBy({0.0f, steps * wheelStep});
}

Vec2 Panel::MaxScroll() const noexcept {
    const Vec2 area = ContentRect().size;
    const Vec2 content = SafeContentSize(*this);
    return {std::max(0.0f, content.x - area.x), std::max(0.0f, content.y - area.y)};
}

void Panel::ArrangeChildren(ElementTree& tree) {
    scroll = ClampScroll(scroll, MaxScroll());
    const Rect area = ContentRect();
    const Vec2 content = SafeContentSize(*this);
    const Rect virtualArea{{area.position.x - scroll.x, area.position.y - scroll.y},
                           {std::max(area.size.x, content.x), std::max(area.size.y, content.y)}};
    for (UIHandle child : children) tree.Arrange(child, virtualArea);
}

bool Panel::ChildVisible(const Element& child) const noexcept {
    const Rect area = ContentRect();
    const Rect box = child.border;
    return box.position.x < area.position.x + area.size.x && box.position.x + box.size.x > area.position.x &&
           box.position.y < area.position.y + area.size.y && box.position.y + box.size.y > area.position.y;
}

std::vector<Rendering::UIRenderItem> BuildPanelItems(const Panel& panel, const PanelStyle& style, float zOrder,
                                                     std::uint64_t paintOrder) {
    std::vector<Rendering::UIRenderItem> out;
    const Rect box = panel.border;
    if (!IsFinite(box.position) || !IsFinite(box.size)) return out;

    std::uint64_t paint = paintOrder;
    AppendBox(panel.handle, box, style.cornerRadius, style.borderWidth, style.background, style.border,
              Rendering::NoClipIndex, zOrder, paint, out);
    const Rect area = panel.ContentRect();
    const float maxScrollY = panel.MaxScroll().y;
    if (maxScrollY > 0.0f && area.size.y > 0.0f) {
        // A barra ocupa a fracao da area que o conteudo ocupa; sua posicao segue a rolagem.
        const float contentHeight = area.size.y + maxScrollY;
        const float thumbHeight = area.size.y * area.size.y / contentHeight;
        const float ratio = std::clamp(panel.scroll.y / maxScrollY, 0.0f, 1.0f);
        const Rect thumb{{area.position.x + area.size.x - style.thumbInset - style.thumbWidth,
                          area.position.y + ratio * (area.size.y - thumbHeight)},
                         {style.thumbWidth, thumbHeight}};
        out.push_back(Rendering::MakeSolidRect(panel.handle, thumb, style.thumb, Rendering::NoClipIndex, zOrder,
                                               paint++));
    }
    return out;
}

} // namespace ToolEngine::SpriteUi::Elements
