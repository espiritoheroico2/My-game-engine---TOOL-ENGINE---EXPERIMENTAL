#include "SpriteUi/Elements/Dropdown.hpp"

#include "SpriteUi/Elements/Paint.hpp"
#include "SpriteUi/Rendering/Primitives.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace ToolEngine::SpriteUi::Elements {

namespace {

using Widgets::NoControlIndex;

// Maior rolagem possivel: o conteudo que nao cabe na lista. Zero quando tudo cabe.
float MaxPopupScroll(const Dropdown& dropdown) noexcept {
    const float content = static_cast<float>(dropdown.control.options.size()) * dropdown.rowHeight;
    return std::max(0.0f, content - dropdown.Popup().size.y);
}

// Indice da opcao sob o ponto, dentro da lista aberta; NoControlIndex fora dela.
std::size_t RowAtPoint(const Dropdown& dropdown, Vec2 point) noexcept {
    const Rect popup = dropdown.Popup();
    if (!IsFinite(point) || dropdown.rowHeight <= 0.0f) return NoControlIndex;
    const bool inside = point.x >= popup.position.x && point.x <= popup.position.x + popup.size.x &&
                        point.y >= popup.position.y && point.y <= popup.position.y + popup.size.y;
    if (!inside) return NoControlIndex;
    const float row = (point.y - popup.position.y + dropdown.popupScrollY) / dropdown.rowHeight;
    if (row < 0.0f || row >= static_cast<float>(dropdown.control.options.size())) return NoControlIndex;
    return static_cast<std::size_t>(row);
}

Vec4 ButtonColor(const Dropdown& dropdown, const DropdownStyle& style) noexcept {
    if (dropdown.states.disabled) return style.disabledButton;
    if (dropdown.control.open || dropdown.states.pressed) return style.selectedRow;
    if (dropdown.states.hovered) return LightenColor(style.button, style.hoverAmount);
    return style.button;
}

// Seta em chevron dentro da faixa da seta: para baixo quando fechada, para cima quando aberta.
void AppendChevron(UIHandle owner, Rect button, const DropdownStyle& style, bool up, float zOrder,
                   std::uint64_t& paint, std::vector<Rendering::UIRenderItem>& out) {
    const float cx = button.position.x + button.size.x - 0.5f * style.arrowAreaWidth;
    const float cy = button.position.y + 0.5f * button.size.y;
    const float hw = style.chevronHalfWidth;
    const float hh = up ? -style.chevronHalfHeight : style.chevronHalfHeight;
    const Vec2 left{cx - hw, cy - hh};
    const Vec2 tip{cx, cy + hh};
    const Vec2 right{cx + hw, cy - hh};
    for (const auto& [from, to] : {std::pair{left, tip}, std::pair{tip, right}}) {
        if (auto segment = Rendering::MakeLine(owner, from, to, style.chevronThickness, style.chevron,
                                               Rendering::NoClipIndex, zOrder, paint++)) {
            out.push_back(*segment);
        }
    }
}

} // namespace

DropdownEvents Dropdown::OnPointer(const PointerEvent& event, Rect window) noexcept {
    const GestureResult gesture = gesture_.Apply(states, event);
    DropdownEvents events;
    if (!gesture.clicked) return events;
    if (control.open) {
        events.closed = control.Close();
        popup_ = {};
    } else {
        OpenAt(window);
        events.opened = control.open;
    }
    return events;
}

DropdownEvents Dropdown::ChooseAt(Vec2 point, Rect window) noexcept {
    DropdownEvents events;
    if (states.disabled || !control.open) return events;
    Layout(window);
    const std::size_t row = RowAtPoint(*this, point);
    if (row == NoControlIndex) return events;
    events.valueChanged = control.Select(row);
    events.closed = true;
    popup_ = {};
    return events;
}

DropdownEvents Dropdown::CloseOutside() noexcept {
    DropdownEvents events;
    events.closed = control.Close();
    popup_ = {};
    return events;
}

DropdownEvents Dropdown::KeyPressed(DropdownKey key, Rect window) noexcept {
    DropdownEvents events;
    if (states.disabled || !states.focused) return events;
    switch (key) {
    case DropdownKey::Down:
        if (control.open) {
            (void)control.MoveDown();
            EnsureHighlightVisible();
        } else {
            OpenAt(window);
            events.opened = control.open;
        }
        break;
    case DropdownKey::Up:
        if (control.open) {
            (void)control.MoveUp();
            EnsureHighlightVisible();
        }
        break;
    case DropdownKey::Enter:
        if (control.open) {
            events.valueChanged = control.Confirm();
            events.closed = true;
            popup_ = {};
        } else {
            OpenAt(window);
            events.opened = control.open;
        }
        break;
    case DropdownKey::Escape:
        events.closed = control.Close();
        popup_ = {};
        break;
    }
    return events;
}

void Dropdown::Layout(Rect window) noexcept {
    popup_ = control.open ? DropdownPopupRect(*this, window) : Rect{};
    popupScrollY = std::clamp(popupScrollY, 0.0f, MaxPopupScroll(*this));
}

void Dropdown::OpenAt(Rect window) noexcept {
    if (!control.Open()) return;
    Layout(window);
    EnsureHighlightVisible();
}

// Rolagem minima para a linha destacada caber na lista. Sem destaque ou lista fechada, nao rola.
void Dropdown::EnsureHighlightVisible() noexcept {
    if (!control.open || control.highlightedIndex == NoControlIndex) return;
    const float top = static_cast<float>(control.highlightedIndex) * rowHeight;
    const float bottom = top + rowHeight;
    if (top < popupScrollY) popupScrollY = top;
    if (bottom > popupScrollY + popup_.size.y) popupScrollY = bottom - popup_.size.y;
    popupScrollY = std::clamp(popupScrollY, 0.0f, MaxPopupScroll(*this));
}

Rect DropdownPopupRect(const Dropdown& dropdown, Rect window) noexcept {
    const std::size_t count = dropdown.control.options.size();
    if (count == 0) return {};
    const Rect button = dropdown.border;
    const float natural = static_cast<float>(count) * dropdown.rowHeight;
    const float below = window.position.y + window.size.y - (button.position.y + button.size.y);
    const float above = button.position.y - window.position.y;
    // Abre abaixo se a lista inteira cabe ali; senao acima se couber; senao no lado com mais espaco.
    const bool opensBelow = natural <= below || (natural > above && below >= above);
    const float room = opensBelow ? below : above;
    const float height = std::max(0.0f, std::min(natural, room));
    const float top = opensBelow ? button.position.y + button.size.y : button.position.y - height;

    const float maxLeft = window.position.x + std::max(0.0f, window.size.x - button.size.x);
    const float left = std::clamp(button.position.x, window.position.x, maxLeft);
    return {{left, top}, {button.size.x, height}};
}

Rect DropdownRowRect(const Dropdown& dropdown, std::size_t index) noexcept {
    const Rect popup = dropdown.Popup();
    const float top = popup.position.y + static_cast<float>(index) * dropdown.rowHeight - dropdown.popupScrollY;
    return {{popup.position.x, top}, {popup.size.x, dropdown.rowHeight}};
}

Rect DropdownValueRect(const Dropdown& dropdown, const DropdownStyle& style) noexcept {
    const Rect content = dropdown.ContentRect();
    return {content.position, {std::max(0.0f, content.size.x - style.arrowAreaWidth), content.size.y}};
}

std::vector<Rendering::UIRenderItem> BuildDropdownButtonItems(const Dropdown& dropdown, const DropdownStyle& style,
                                                              float zOrder, std::uint64_t paintOrder) {
    std::vector<Rendering::UIRenderItem> out;
    const Rect box = dropdown.border;
    if (!IsFinite(box.position) || !IsFinite(box.size)) return out;

    std::uint64_t paint = paintOrder;
    const Vec4 color = ButtonColor(dropdown, style);
    if (dropdown.states.focused && !dropdown.states.disabled) {
        AppendBox(dropdown.handle, box, style.cornerRadius, style.focusRingWidth, color, style.focusRing,
                  Rendering::NoClipIndex, zOrder, paint, out);
    } else {
        AppendFill(dropdown.handle, box, style.cornerRadius, color, Rendering::NoClipIndex, zOrder, paint, out);
    }
    AppendChevron(dropdown.handle, box, style, dropdown.control.open, zOrder, paint, out);
    return out;
}

std::vector<Rendering::UIRenderItem> BuildDropdownPopupItems(const Dropdown& dropdown, const DropdownStyle& style,
                                                             std::uint32_t clipIndex, float zOrder,
                                                             std::uint64_t paintOrder) {
    std::vector<Rendering::UIRenderItem> out;
    const Rect popup = dropdown.Popup();
    if (!dropdown.control.open || popup.size.y <= 0.0f || !IsFinite(popup.position) || !IsFinite(popup.size)) {
        return out;
    }

    std::uint64_t paint = paintOrder;
    AppendBox(dropdown.handle, popup, style.cornerRadius, style.popupBorderWidth, style.popup, style.popupBorder,
              clipIndex, zOrder, paint, out);
    const float rowHeight = dropdown.rowHeight;
    const float rowRadius = std::max(0.0f, style.cornerRadius - style.popupBorderWidth);
    const auto first = static_cast<std::size_t>(std::max(0.0f, std::floor(dropdown.popupScrollY / rowHeight)));
    const auto bottomRow = std::ceil((dropdown.popupScrollY + popup.size.y) / rowHeight);
    const std::size_t visibleEnd = bottomRow > 0.0f ? static_cast<std::size_t>(bottomRow) : std::size_t{0};
    const std::size_t end = std::min(dropdown.control.options.size(), visibleEnd);
    for (std::size_t i = first; i < end; ++i) {
        const Rect row = RowInsideBorder(DropdownRowRect(dropdown, i), popup, style.popupBorderWidth);
        if (i == dropdown.control.highlightedIndex) {
            AppendFill(dropdown.handle, row, rowRadius, style.highlight, clipIndex, zOrder, paint, out);
        } else if (i == dropdown.control.selectedIndex) {
            AppendFill(dropdown.handle, row, rowRadius, style.selectedRow, clipIndex, zOrder, paint, out);
        }
    }
    return out;
}

} // namespace ToolEngine::SpriteUi::Elements
