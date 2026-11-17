#include "SpriteUi/Elements/MenuBar.hpp"

#include "SpriteUi/Core/DrawOrder.hpp"
#include "SpriteUi/Elements/Paint.hpp"
#include "SpriteUi/Rendering/Primitives.hpp"
#include "SpriteUi/Text/UIScale.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace ToolEngine::SpriteUi::Elements {

namespace {

using Widgets::MenuEvent;
using Widgets::MenuItem;
using Widgets::MenuItemKind;
using Widgets::MenuKey;
using Widgets::NoControlIndex;

// Recuo dos separadores dentro da largura do popup.
constexpr float kSeparatorInset = 8.0f;
// Altura da faixa a 100 por cento de escala (ver MenuBarHeightFor).
constexpr float kMenuBarBaseHeight = 28.0f;

bool ContainsPoint(Rect rect, Vec2 point) noexcept {
    return point.x >= rect.position.x && point.x <= rect.position.x + rect.size.x &&
           point.y >= rect.position.y && point.y <= rect.position.y + rect.size.y;
}

Rect Intersect(Rect a, Rect b) noexcept {
    const float left = std::max(a.position.x, b.position.x);
    const float top = std::max(a.position.y, b.position.y);
    const float right = std::min(a.position.x + a.size.x, b.position.x + b.size.x);
    const float bottom = std::min(a.position.y + a.size.y, b.position.y + b.size.y);
    return {{left, top}, {std::max(0.0f, right - left), std::max(0.0f, bottom - top)}};
}

float ItemHeight(const MenuItem& item, const MenuBar& bar) noexcept {
    return item.kind == MenuItemKind::Separator ? bar.separatorHeight : bar.rowHeight;
}

float NaturalHeight(const MenuBar& bar, std::size_t depth) noexcept {
    float total = 0.0f;
    for (const MenuItem& item : bar.control.Dropdown().Items(depth)) total += ItemHeight(item, bar);
    return total;
}

MenuBarEvents FromMenuEvent(MenuEvent event) noexcept {
    MenuBarEvents out;
    out.changed = event != MenuEvent::None;
    out.activated = event == MenuEvent::Activated;
    out.closed = event == MenuEvent::Activated || event == MenuEvent::Dismissed;
    return out;
}

// Procura o atalho em uma lista de itens, descendo nos submenus. path guarda o caminho ate o achado.
bool FindInItems(const std::vector<MenuItem>& items, std::string_view text, std::vector<std::size_t>& path) {
    for (std::size_t i = 0; i < items.size(); ++i) {
        const MenuItem& item = items[i];
        if (item.disabled) continue;
        path.push_back(i);
        if (item.kind == MenuItemKind::Action && item.shortcut == text) return true;
        if (item.kind == MenuItemKind::Submenu && FindInItems(item.children, text, path)) return true;
        path.pop_back();
    }
    return false;
}

} // namespace

MenuBarEvents MenuBar::OnPointer(const PointerEvent& event, Rect window) noexcept {
    MenuBarEvents events;
    if (states.disabled || !IsFinite(event.position)) return events;

    const bool hovering = event.phase == PointerPhase::Move || event.phase == PointerPhase::Enter;
    if (event.phase == PointerPhase::Leave) {
        hoveredHeader_ = NoControlIndex;
    } else if (hovering) {
        hoveredHeader_ = HeaderAt(event.position);
    }
    if (hovering && control.IsOpen()) {
        const std::size_t header = HeaderAt(event.position);
        if (header != NoControlIndex) {
            events.changed = control.HoverHeader(header);
        } else if (const auto hit = RowAt(event.position); hit && hit->depth + 1 == control.Dropdown().Depth()) {
            events.changed = control.HoverItem(hit->index);
        }
        if (events.changed) hoverTime_ = 0.0f;
    }

    const GestureResult gesture = gesture_.Apply(states, event);
    if (gesture.clicked) {
        const bool wasOpen = control.IsOpen();
        const std::size_t header = HeaderAt(event.position);
        if (header != NoControlIndex) {
            events.changed = control.ClickHeader(header);
        } else if (const auto hit = RowAt(event.position)) {
            events = FromMenuEvent(control.Click(hit->depth, hit->index));
        } else if (wasOpen) {
            control.Close();
            events.changed = true;
        }
        barActive_ = false;
        hoverTime_ = 0.0f;
        events.closed = wasOpen && !control.IsOpen();
    }
    Refresh(window);
    return events;
}

MenuBarEvents MenuBar::KeyPressed(MenuKey key, Rect window) noexcept {
    MenuBarEvents events;
    if (states.disabled) return events;
    if (control.IsOpen()) {
        events = FromMenuEvent(control.Key(key));
    } else if (barActive_) {
        const std::size_t count = control.Menus().size();
        switch (key) {
        case MenuKey::Left:
            if (headerCursor_ > 0) {
                --headerCursor_;
                events.changed = true;
            }
            break;
        case MenuKey::Right:
            if (headerCursor_ + 1 < count) {
                ++headerCursor_;
                events.changed = true;
            }
            break;
        case MenuKey::Down:
        case MenuKey::Enter:
            events.changed = control.OpenMenu(headerCursor_);
            barActive_ = false;
            break;
        case MenuKey::Escape:
            barActive_ = false;
            events.changed = true;
            events.closed = true;
            break;
        case MenuKey::Up:
        case MenuKey::Home:
        case MenuKey::End:
            break;
        }
    }
    if (events.changed) hoverTime_ = 0.0f;
    Refresh(window);
    return events;
}

MenuBarEvents MenuBar::PressAlt() noexcept {
    MenuBarEvents events;
    if (states.disabled) return events;
    if (control.IsOpen()) {
        control.Close();
        events.changed = true;
        events.closed = true;
    } else {
        barActive_ = true;
        headerCursor_ = 0;
        events.changed = true;
    }
    return events;
}

MenuBarEvents MenuBar::Tick(float seconds, Rect window) noexcept {
    MenuBarEvents events;
    if (!control.IsOpen() || !std::isfinite(seconds) || seconds < 0.0f) return events;
    const std::size_t depth = control.Dropdown().Depth() - 1;
    const std::size_t index = control.Dropdown().Highlighted(depth);
    const auto& items = control.Dropdown().Items(depth);
    const bool waiting = index != NoControlIndex && index < items.size() &&
                         items[index].kind == MenuItemKind::Submenu && control.Dropdown().OpenSubmenu(depth) == NoControlIndex;
    if (!waiting) {
        hoverTime_ = 0.0f;
        return events;
    }
    hoverTime_ += seconds;
    if (hoverTime_ < submenuDelay) return events;
    hoverTime_ = 0.0f;
    events = FromMenuEvent(control.Key(MenuKey::Right));
    Refresh(window);
    return events;
}

std::optional<MenuShortcut> MenuBar::FindShortcut(std::string_view shortcut) const {
    if (shortcut.empty()) return std::nullopt;
    const auto& menus = control.Menus();
    for (std::size_t m = 0; m < menus.size(); ++m) {
        if (menus[m].disabled) continue;
        std::vector<std::size_t> path;
        if (FindInItems(menus[m].items, shortcut, path)) return MenuShortcut{m, path};
    }
    return std::nullopt;
}

Rect MenuBar::HeaderRect(std::size_t index) const noexcept {
    if (index >= control.Menus().size()) return {};
    const float x = border.position.x + static_cast<float>(index) * headerWidth - headerScrollX;
    return {{x, border.position.y}, {headerWidth, border.size.y}};
}

Rect MenuBar::IconRect(std::size_t depth, std::size_t index) const noexcept {
    const Rect row = RowRect(depth, index);
    if (row.size.y <= 0.0f) return {};
    const float x = row.position.x + 0.5f * (kContentInsetX - kIconSize);
    const float y = row.position.y + 0.5f * (row.size.y - kIconSize);
    return {{x, y}, {kIconSize, kIconSize}};
}

Rect MenuBar::ShortcutRect(std::size_t depth, std::size_t index) const noexcept {
    const Rect row = RowRect(depth, index);
    if (row.size.y <= 0.0f) return {};
    // Coluna do atalho: a largura do texto nao e conhecida pelo core, entao o host alinha o texto a direita dela.
    const float width = std::min(kShortcutColumnWidth, row.size.x);
    const float right = row.position.x + row.size.x - 0.5f * kContentInsetX;
    return {{right - width, row.position.y}, {width, row.size.y}};
}

float MenuBarHeightFor(float uiScale) noexcept {
    return std::round(kMenuBarBaseHeight * Text::SanitizeUIScale(uiScale));
}

Rect MenuBar::PopupRect(std::size_t depth) const noexcept {
    return depth < popups_.size() ? popups_[depth] : Rect{};
}

Rect MenuBar::RowRect(std::size_t depth, std::size_t index) const noexcept {
    if (depth >= popups_.size() || index >= control.Dropdown().Items(depth).size()) return {};
    const Rect popup = popups_[depth];
    const auto& items = control.Dropdown().Items(depth);
    float top = popup.position.y - ScrollOf(depth);
    for (std::size_t i = 0; i < index; ++i) top += ItemHeight(items[i], *this);
    return {{popup.position.x, top}, {popup.size.x, ItemHeight(items[index], *this)}};
}

// Popups: o do nivel 0 desce do cabecalho ativo; cada nivel seguinte fica ao lado da linha do submenu aberto,
// do lado direito ou, se nao couber, do esquerdo. Altura e o conteudo ou o espaco abaixo do topo, o menor (o
// resto rola). Sem limite de itens. Tudo fica dentro da janela.
void MenuBar::Layout(Rect window) noexcept {
    popups_.clear();
    const std::size_t depth = control.Dropdown().Depth();
    popupScroll_.resize(depth, 0.0f);
    const float windowRight = window.position.x + window.size.x;
    const float windowBottom = window.position.y + window.size.y;
    for (std::size_t d = 0; d < depth; ++d) {
        float x = 0.0f;
        float y = 0.0f;
        if (d == 0) {
            const Rect header = HeaderRect(control.ActiveMenu());
            x = header.position.x;
            y = border.position.y + border.size.y;
        } else {
            const Rect previous = popups_[d - 1];
            const Rect row = RowRect(d - 1, control.Dropdown().OpenSubmenu(d - 1));
            x = previous.position.x + previous.size.x;
            if (x + popupWidth > windowRight) x = previous.position.x - popupWidth;
            y = row.position.y;
        }
        // Nivel 0 fica abaixo da barra e rola no que sobra. Um submenu que nao cabe abaixo sobe para caber.
        const float natural = NaturalHeight(*this, d);
        const float below = std::max(0.0f, windowBottom - y);
        float height = std::min(natural, below);
        if (d > 0 && natural > below) {
            height = std::min(natural, std::max(0.0f, windowBottom - window.position.y));
            y = std::max(window.position.y, windowBottom - height);
        }
        x = std::clamp(x, window.position.x, std::max(window.position.x, windowRight - popupWidth));
        y = std::max(y, window.position.y);
        popups_.push_back({{x, y}, {popupWidth, height}});
        popupScroll_[d] = std::clamp(popupScroll_[d], 0.0f, std::max(0.0f, natural - height));
    }
}

std::size_t MenuBar::HeaderAt(Vec2 point) const noexcept {
    if (!IsFinite(point) || headerWidth <= 0.0f || !ContainsPoint(border, point)) return NoControlIndex;
    const float slot = std::floor((point.x - border.position.x + headerScrollX) / headerWidth);
    if (slot < 0.0f || slot >= static_cast<float>(control.Menus().size())) return NoControlIndex;
    return static_cast<std::size_t>(slot);
}

// Linha sob o ponto, procurando do nivel mais fundo para o mais raso (o submenu esta por cima).
std::optional<MenuBar::RowHit> MenuBar::RowAt(Vec2 point) const noexcept {
    for (std::size_t d = popups_.size(); d-- > 0;) {
        if (!ContainsPoint(popups_[d], point)) continue;
        const auto& items = control.Dropdown().Items(d);
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (ContainsPoint(RowRect(d, i), point)) return RowHit{d, i};
        }
        return std::nullopt;
    }
    return std::nullopt;
}

float MenuBar::ScrollOf(std::size_t depth) const noexcept {
    return depth < popupScroll_.size() ? popupScroll_[depth] : 0.0f;
}

// Depois de qualquer mudanca: recalcula os popups, mantem o item destacado visivel e a faixa no cabecalho ativo.
void MenuBar::Refresh(Rect window) noexcept {
    const std::size_t count = control.Menus().size();
    if (headerCursor_ >= count) headerCursor_ = count > 0 ? count - 1 : 0;
    if (control.ActiveMenu() != lastActive_) {
        popupScroll_.clear();
        lastActive_ = control.ActiveMenu();
    }
    Layout(window);
    EnsureDeepestRowVisible();
    const std::size_t focus = control.IsOpen() ? control.ActiveMenu() : (barActive_ ? headerCursor_ : NoControlIndex);
    EnsureHeaderVisible(focus);
}

// Rolagem minima para o cabecalho index caber na faixa; sempre prende a rolagem nos limites.
void MenuBar::EnsureHeaderVisible(std::size_t index) noexcept {
    const float visible = std::max(0.0f, border.size.x);
    const float contentWidth = static_cast<float>(control.Menus().size()) * headerWidth;
    if (index != NoControlIndex) {
        const float left = static_cast<float>(index) * headerWidth;
        const float right = left + headerWidth;
        if (left < headerScrollX) headerScrollX = left;
        if (right > headerScrollX + visible) headerScrollX = right - visible;
    }
    headerScrollX = std::clamp(headerScrollX, 0.0f, std::max(0.0f, contentWidth - visible));
}

// Rolagem minima para o item destacado do nivel mais fundo caber no popup.
void MenuBar::EnsureDeepestRowVisible() noexcept {
    if (!control.IsOpen()) return;
    const std::size_t depth = control.Dropdown().Depth() - 1;
    const std::size_t highlighted = control.Dropdown().Highlighted(depth);
    const auto& items = control.Dropdown().Items(depth);
    if (depth >= popups_.size() || highlighted == NoControlIndex || highlighted >= items.size()) return;
    float top = 0.0f;
    for (std::size_t i = 0; i < highlighted; ++i) top += ItemHeight(items[i], *this);
    const float itemHeight = ItemHeight(items[highlighted], *this);
    const float viewport = popups_[depth].size.y;
    float& scroll = popupScroll_[depth];
    if (top < scroll) scroll = top;
    if (top + itemHeight > scroll + viewport) scroll = top + itemHeight - viewport;
    scroll = std::clamp(scroll, 0.0f, std::max(0.0f, NaturalHeight(*this, depth) - viewport));
}

std::vector<Rendering::UIRenderItem> BuildMenuBarItems(const MenuBar& bar, const MenuBarStyle& style,
                                                       std::uint32_t clipIndex, float zOrder,
                                                       std::uint64_t paintOrder) {
    std::vector<Rendering::UIRenderItem> out;
    const Rect strip = bar.border;
    if (!IsFinite(strip.position) || !IsFinite(strip.size)) return out;

    std::uint64_t paint = paintOrder;
    out.push_back(Rendering::MakeSolidRect(bar.handle, strip, style.bar, clipIndex, zOrder, paint++));
    const std::size_t open = bar.control.IsOpen() ? bar.control.ActiveMenu() : NoControlIndex;
    for (std::size_t i = 0; i < bar.control.Menus().size(); ++i) {
        const Rect header = bar.HeaderRect(i);
        if (header.position.x + header.size.x <= strip.position.x ||
            header.position.x >= strip.position.x + strip.size.x) {
            continue;  // Fora da faixa: nao gera item.
        }
        Vec4 color = style.header;
        if (i == open) {
            color = style.headerOpen;
        } else if (i == bar.HoveredHeader()) {
            color = style.headerHover;
        }
        out.push_back(Rendering::MakeSolidRect(bar.handle, header, color, clipIndex, zOrder, paint++));
    }

    const float popupZ = zOrder + 0.1f;
    for (std::size_t d = 0; d < bar.control.Dropdown().Depth(); ++d) {
        const Rect popup = bar.PopupRect(d);
        if (popup.size.y <= 0.0f) continue;
        const std::size_t popupBegin = out.size();
        // Contorno antes das linhas: cada destaque fica por dentro dele (ver RowInsideBorder).
        AppendBox(bar.handle, popup, style.cornerRadius, style.borderWidth, style.popup, style.popupBorder,
                  Rendering::NoClipIndex, popupZ, paint, out);
        const float rowRadius = std::max(0.0f, style.cornerRadius - style.borderWidth);
        const auto& items = bar.control.Dropdown().Items(d);
        const std::size_t highlighted = bar.control.Dropdown().Highlighted(d);
        for (std::size_t i = 0; i < items.size(); ++i) {
            const Rect row = Intersect(bar.RowRect(d, i), popup);  // So a parte da linha dentro do popup.
            if (row.size.y <= 0.0f) continue;
            if (items[i].kind == MenuItemKind::Separator) {
                const float y = row.position.y + 0.5f * (row.size.y - style.separatorThickness);
                const Rect line{{row.position.x + kSeparatorInset, y},
                                {std::max(0.0f, row.size.x - 2.0f * kSeparatorInset), style.separatorThickness}};
                out.push_back(Rendering::MakeSolidRect(bar.handle, line, style.separator, Rendering::NoClipIndex,
                                                       popupZ, paint++));
                continue;
            }
            if (i == highlighted) {
                AppendFill(bar.handle, RowInsideBorder(row, popup, style.borderWidth), rowRadius, style.highlight,
                           Rendering::NoClipIndex, popupZ, paint, out);
            }
            if (items[i].checkable) {
                // Caixa na coluna de icone: quadro com contorno e, se ligada, marca cheia por dentro.
                const float s = style.checkSize;
                const float column = MenuBar::ContentInsetX();
                const Rect box{{row.position.x + 0.5f * (column - s), row.position.y + 0.5f * (row.size.y - s)},
                               {s, s}};
                AppendBox(bar.handle, box, 0.0f, style.borderWidth, style.popup, style.popupBorder,
                          Rendering::NoClipIndex, popupZ, paint, out);
                if (items[i].checked) {
                    const Rect mark{{box.position.x + 2.0f, box.position.y + 2.0f},
                                    {std::max(0.0f, s - 4.0f), std::max(0.0f, s - 4.0f)}};
                    AppendFill(bar.handle, mark, 0.0f, style.check, Rendering::NoClipIndex, popupZ, paint, out);
                }
            }
        }
        // Popups ficam na camada de menus (acima da janela, abaixo do arrasto e das dicas).
        for (std::size_t k = popupBegin; k < out.size(); ++k) {
            out[k].layer = static_cast<std::int32_t>(UILayer::Popup);
        }
    }
    return out;
}

} // namespace ToolEngine::SpriteUi::Elements
