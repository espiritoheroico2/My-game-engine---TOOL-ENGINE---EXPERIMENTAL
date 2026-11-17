#include "SpriteUi/Elements/TabBar.hpp"

#include "SpriteUi/Elements/Paint.hpp"
#include "SpriteUi/Rendering/Primitives.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace ToolEngine::SpriteUi::Elements {

namespace {

using Widgets::NoControlIndex;

float TabStride(const TabBar& tab) noexcept {
    return tab.tabWidth + tab.tabGap;
}

float TabHeight(const TabBar& tab) noexcept {
    return std::max(0.0f, tab.border.size.y - 2.0f * tab.stripPadding);
}

// Largura de todas as abas, incluindo as folgas. Zero sem abas.
float ContentWidth(const TabBar& tab) noexcept {
    const std::size_t count = tab.control.labels.size();
    if (count == 0) return 0.0f;
    return static_cast<float>(count) * TabStride(tab) - tab.tabGap;
}

float VisibleWidth(const TabBar& tab) noexcept {
    return std::max(0.0f, tab.border.size.x - 2.0f * tab.stripPadding);
}

float MaxScroll(const TabBar& tab) noexcept {
    return std::max(0.0f, ContentWidth(tab) - VisibleWidth(tab));
}

// Faixa de indices [first, end) das abas que tocam a area visivel. Sem abas, vazia.
std::pair<std::size_t, std::size_t> VisibleTabRange(const TabBar& tab) noexcept {
    const std::size_t count = tab.control.labels.size();
    const float stride = TabStride(tab);
    if (count == 0 || stride <= 0.0f) return {0, 0};
    const float first = std::clamp(std::floor(tab.scrollX / stride), 0.0f, static_cast<float>(count));
    const float end = std::clamp(std::ceil((tab.scrollX + VisibleWidth(tab)) / stride), first,
                                 static_cast<float>(count));
    return {static_cast<std::size_t>(first), static_cast<std::size_t>(end)};
}

} // namespace

TabEvents TabBar::OnPointer(const PointerEvent& event) noexcept {
    TabEvents events;
    const GestureResult gesture = gesture_.Apply(states, event);
    if (!gesture.clicked || states.disabled) return events;
    const std::size_t index = TabAt(*this, event.position);
    if (index == NoControlIndex) return events;
    events.changed = control.Select(index);
    EnsureActiveVisible();
    return events;
}

TabEvents TabBar::KeyPressed(TabKey key) noexcept {
    TabEvents events;
    if (states.disabled || !states.focused) return events;
    switch (key) {
    case TabKey::Left:
        events.changed = MoveActive(-1);
        break;
    case TabKey::Right:
        events.changed = MoveActive(1);
        break;
    case TabKey::Enter:
        events.activated = control.activeIndex != NoControlIndex;
        break;
    }
    return events;
}

void TabBar::ArrangePanel(ElementTree& tree) const {
    if (control.activeIndex >= children.size()) return;
    tree.Arrange(children[control.activeIndex], PanelArea());
}

Rect TabBar::PanelArea() const noexcept {
    return {{border.position.x, border.position.y + border.size.y + panelGap}, {border.size.x, panelHeight}};
}

// Move a aba ativa uma posicao; nas pontas, nao faz nada. Sem aba ativa, a primeira seta leva a aba 0.
bool TabBar::MoveActive(int delta) noexcept {
    const std::size_t count = control.labels.size();
    if (count == 0) return false;
    const std::size_t active = control.activeIndex;
    std::size_t target = 0;
    if (active != NoControlIndex) {
        target = active;
        if (delta > 0 && active + 1 < count) target = active + 1;
        if (delta < 0 && active > 0) target = active - 1;
    }
    const bool changed = control.Select(target);
    EnsureActiveVisible();
    return changed;
}

// Rolagem minima para a aba ativa caber inteira na faixa. Sem aba ativa, nao rola.
void TabBar::EnsureActiveVisible() noexcept {
    if (control.activeIndex == NoControlIndex) return;
    const float left = static_cast<float>(control.activeIndex) * TabStride(*this);
    const float right = left + tabWidth;
    if (left < scrollX) scrollX = left;
    if (right > scrollX + VisibleWidth(*this)) scrollX = right - VisibleWidth(*this);
    scrollX = std::clamp(scrollX, 0.0f, MaxScroll(*this));
}

Rect TabRect(const TabBar& tab, std::size_t index) noexcept {
    const float x = tab.border.position.x + tab.stripPadding + static_cast<float>(index) * TabStride(tab) -
                    tab.scrollX;
    return {{x, tab.border.position.y + tab.stripPadding}, {tab.tabWidth, TabHeight(tab)}};
}

std::size_t TabAt(const TabBar& tab, Vec2 point) noexcept {
    const float stride = TabStride(tab);
    if (!IsFinite(point) || stride <= 0.0f) return NoControlIndex;
    const Rect strip = tab.border;
    const bool inside = point.x >= strip.position.x && point.x <= strip.position.x + strip.size.x &&
                        point.y >= strip.position.y && point.y <= strip.position.y + strip.size.y;
    if (!inside) return NoControlIndex;
    const float local = point.x - strip.position.x - tab.stripPadding + tab.scrollX;
    if (local < 0.0f) return NoControlIndex;
    const float slot = std::floor(local / stride);
    if (slot >= static_cast<float>(tab.control.labels.size())) return NoControlIndex;
    if (local - slot * stride > tab.tabWidth) return NoControlIndex;  // Na folga entre duas abas.
    return static_cast<std::size_t>(slot);
}

std::vector<Rendering::UIRenderItem> BuildTabBarItems(const TabBar& tab, const TabBarStyle& style,
                                                      std::uint32_t clipIndex, float zOrder,
                                                      std::uint64_t paintOrder) {
    std::vector<Rendering::UIRenderItem> out;
    if (!IsFinite(tab.border.position) || !IsFinite(tab.border.size)) return out;

    std::uint64_t paint = paintOrder;
    AppendFill(tab.handle, tab.border, style.cornerRadius, style.strip, clipIndex, zOrder, paint, out);
    const auto [first, end] = VisibleTabRange(tab);
    for (std::size_t i = first; i < end; ++i) {
        const Rect rect = TabRect(tab, i);
        const bool active = i == tab.control.activeIndex;
        const Vec4 color = active ? style.activeTab : style.tab;
        if (active && tab.states.focused && !tab.states.disabled) {
            AppendBox(tab.handle, rect, style.cornerRadius, style.focusRingWidth, color, style.focusRing, clipIndex,
                      zOrder, paint, out);
        } else {
            AppendFill(tab.handle, rect, style.cornerRadius, color, clipIndex, zOrder, paint, out);
        }
        if (!active) continue;
        const Rect mark{{rect.position.x, rect.position.y + rect.size.y - style.activeMarkHeight},
                        {rect.size.x, style.activeMarkHeight}};
        out.push_back(Rendering::MakeSolidRect(tab.handle, mark, style.activeMark, clipIndex, zOrder, paint++));
    }
    return out;
}

} // namespace ToolEngine::SpriteUi::Elements
