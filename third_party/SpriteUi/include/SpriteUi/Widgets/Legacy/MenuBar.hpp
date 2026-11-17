#pragma once

#include "SpriteUi/Core/Math.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Versao anterior da barra de menus (layout, ponteiro e desenho por geometria). Fica em Legacy porque os nomes
// MenuItem, MenuKey e MenuBarEntry tambem existem em Widgets/Menu.hpp (modelo novo, sem geometria).
namespace ToolEngine::SpriteUi::Widgets::Legacy {

enum class MenuItemKind : std::uint8_t {
    Action,
    Separator
};

struct MenuItem {
    std::string label;
    std::string shortcut;
    MenuItemKind kind = MenuItemKind::Action;
    bool disabled = false;
    bool operator==(const MenuItem&) const = default;
};

struct MenuBarEntry {
    std::string label;
    std::vector<MenuItem> items;
    bool operator==(const MenuBarEntry&) const = default;
};

enum class MenuKey : std::uint8_t {
    Up,
    Down,
    Left,
    Right,
    Home,
    End,
    Enter,
    Escape
};

struct MenuBarEvents {
    bool handled = false;
    bool activated = false;
    bool closed = false;
    std::size_t menuIndex = 0;
    std::size_t itemIndex = 0;
};

class MenuBar {
public:
    float popupWidth = 260.0f;
    float rowHeight = 24.0f;
    float separatorHeight = 8.0f;

    void SetMenus(std::vector<MenuBarEntry> menus) {
        if (menus_ == menus) return;
        menus_ = std::move(menus);
        Close();
    }

    [[nodiscard]] const std::vector<MenuBarEntry>& Menus() const noexcept { return menus_; }
    [[nodiscard]] bool IsOpen() const noexcept { return activeMenu_.has_value(); }
    [[nodiscard]] std::optional<std::size_t> ActiveMenu() const noexcept { return activeMenu_; }
    [[nodiscard]] std::optional<std::size_t> ActiveItem() const noexcept { return activeItem_; }

    void Layout(Rect viewport, float barHeight, const std::vector<float>& headerWidths) {
        viewport_ = viewport;
        barHeight_ = std::isfinite(barHeight) ? std::clamp(barHeight, 0.0f, std::max(0.0f, viewport.size.y)) : 0.0f;
        headerRects_.clear();
        headerRects_.reserve(menus_.size());
        float x = viewport.position.x;
        const float right = viewport.position.x + std::max(0.0f, viewport.size.x);
        for (std::size_t i = 0; i < menus_.size(); ++i) {
            const float requested = i < headerWidths.size() && std::isfinite(headerWidths[i])
                                        ? std::max(0.0f, headerWidths[i])
                                        : 0.0f;
            const float width = std::min(requested, std::max(0.0f, right - x));
            headerRects_.push_back(Rect{{x, viewport.position.y}, {width, barHeight_}});
            x += width;
        }
    }

    [[nodiscard]] Rect HeaderRect(std::size_t index) const noexcept {
        return index < headerRects_.size() ? headerRects_[index] : Rect{};
    }

    [[nodiscard]] Rect PopupRect() const noexcept {
        if (!activeMenu_ || *activeMenu_ >= menus_.size()) return {};
        const float width = std::clamp(std::isfinite(popupWidth) ? popupWidth : 0.0f, 0.0f,
                                       std::max(0.0f, viewport_.size.x));
        const float height = PopupHeight(*activeMenu_);
        const Rect header = HeaderRect(*activeMenu_);
        const float x = std::clamp(header.position.x, viewport_.position.x,
                                   viewport_.position.x + std::max(0.0f, viewport_.size.x - width));
        float y = viewport_.position.y + barHeight_;
        if (y + height > viewport_.position.y + viewport_.size.y) {
            y = std::max(viewport_.position.y, viewport_.position.y + viewport_.size.y - height);
        }
        return Rect{{x, y}, {width, std::min(height, std::max(0.0f, viewport_.size.y))}};
    }

    [[nodiscard]] Rect RowRect(std::size_t menu, std::size_t item) const noexcept {
        if (!activeMenu_ || *activeMenu_ != menu || menu >= menus_.size() || item >= menus_[menu].items.size()) return {};
        const Rect popup = PopupRect();
        float y = popup.position.y;
        for (std::size_t i = 0; i < item; ++i) y += ItemHeight(menus_[menu].items[i]);
        const float height = ItemHeight(menus_[menu].items[item]);
        return Rect{{popup.position.x, y}, {popup.size.x, std::min(height, std::max(0.0f, popup.position.y + popup.size.y - y))}};
    }

    void Close() noexcept {
        activeMenu_.reset();
        activeItem_.reset();
        pressedItem_.reset();
    }

    [[nodiscard]] MenuBarEvents PointerDown(Vec2 point) {
        for (std::size_t i = 0; i < headerRects_.size(); ++i) {
            if (!headerRects_[i].Contains(point)) continue;
            if (activeMenu_ && *activeMenu_ == i) {
                Close();
                return MenuBarEvents{true, false, true};
            }
            Open(i);
            return MenuBarEvents{true};
        }
        if (activeMenu_) {
            const std::optional<std::size_t> row = RowAt(point);
            if (row) {
                pressedItem_ = row;
                activeItem_ = row;
                return MenuBarEvents{true};
            }
            if (PopupRect().Contains(point)) return MenuBarEvents{true};
            Close();
            return {};
        }
        return {};
    }

    [[nodiscard]] MenuBarEvents PointerUp(Vec2 point) {
        const std::optional<std::size_t> pressed = pressedItem_;
        pressedItem_.reset();
        if (!pressed || !activeMenu_) return {};
        const std::optional<std::size_t> released = RowAt(point);
        if (!released || *released != *pressed) return MenuBarEvents{PopupRect().Contains(point)};
        const std::size_t menu = *activeMenu_;
        const MenuItem& item = menus_[menu].items[*released];
        if (item.kind == MenuItemKind::Separator || item.disabled) return MenuBarEvents{true};
        Close();
        return MenuBarEvents{true, true, true, menu, *released};
    }

    [[nodiscard]] MenuBarEvents PointerMove(Vec2 point) {
        for (std::size_t i = 0; i < headerRects_.size(); ++i) {
            if (!headerRects_[i].Contains(point)) continue;
            if (activeMenu_) Open(i);
            return MenuBarEvents{true};
        }
        if (!activeMenu_) return {};
        if (PopupRect().Contains(point)) {
            activeItem_ = RowAt(point);
            return MenuBarEvents{true};
        }
        return {};
    }

    [[nodiscard]] MenuBarEvents PressAlt() {
        if (IsOpen()) {
            Close();
            return MenuBarEvents{true, false, true};
        }
        if (menus_.empty()) return {};
        Open(0);
        return MenuBarEvents{true};
    }

    [[nodiscard]] MenuBarEvents KeyPressed(MenuKey key) {
        if (!IsOpen()) return {};
        switch (key) {
            case MenuKey::Escape:
                Close();
                return MenuBarEvents{true, false, true};
            case MenuKey::Left:
                Open((*activeMenu_ + menus_.size() - 1) % menus_.size());
                return MenuBarEvents{true};
            case MenuKey::Right:
                Open((*activeMenu_ + 1) % menus_.size());
                return MenuBarEvents{true};
            case MenuKey::Up:
                SelectNext(-1);
                return MenuBarEvents{true};
            case MenuKey::Down:
                SelectNext(1);
                return MenuBarEvents{true};
            case MenuKey::Home:
                SelectBoundary(false);
                return MenuBarEvents{true};
            case MenuKey::End:
                SelectBoundary(true);
                return MenuBarEvents{true};
            case MenuKey::Enter:
                return ActivateSelected();
        }
        return {};
    }

private:
    [[nodiscard]] float ItemHeight(const MenuItem& item) const noexcept {
        const float value = item.kind == MenuItemKind::Separator ? separatorHeight : rowHeight;
        return std::isfinite(value) ? std::max(0.0f, value) : 0.0f;
    }

    [[nodiscard]] float PopupHeight(std::size_t menu) const noexcept {
        if (menu >= menus_.size()) return 0.0f;
        float height = 0.0f;
        for (const MenuItem& item : menus_[menu].items) height += ItemHeight(item);
        return std::min(height, std::max(0.0f, viewport_.size.y));
    }

    [[nodiscard]] std::optional<std::size_t> RowAt(Vec2 point) const noexcept {
        if (!activeMenu_ || *activeMenu_ >= menus_.size()) return std::nullopt;
        const Rect popup = PopupRect();
        if (!popup.Contains(point)) return std::nullopt;
        float y = popup.position.y;
        const auto& items = menus_[*activeMenu_].items;
        for (std::size_t i = 0; i < items.size(); ++i) {
            const float height = ItemHeight(items[i]);
            if (point.y >= y && point.y < y + height) return i;
            y += height;
        }
        return std::nullopt;
    }

    void Open(std::size_t menu) {
        if (menu >= menus_.size()) return;
        activeMenu_ = menu;
        activeItem_.reset();
        pressedItem_.reset();
        SelectNext(1);
    }

    void SelectNext(int direction) {
        if (!activeMenu_ || *activeMenu_ >= menus_.size()) return;
        const auto& items = menus_[*activeMenu_].items;
        if (items.empty()) {
            activeItem_.reset();
            return;
        }
        const std::size_t start = activeItem_.value_or(direction > 0 ? items.size() - 1 : 0);
        for (std::size_t step = 1; step <= items.size(); ++step) {
            const std::size_t index = direction > 0 ? (start + step) % items.size()
                                                   : (start + items.size() - step % items.size()) % items.size();
            if (items[index].kind == MenuItemKind::Action && !items[index].disabled) {
                activeItem_ = index;
                return;
            }
        }
        activeItem_.reset();
    }

    void SelectBoundary(bool last) {
        if (!activeMenu_ || *activeMenu_ >= menus_.size()) return;
        const auto& items = menus_[*activeMenu_].items;
        if (!last) {
            for (std::size_t i = 0; i < items.size(); ++i) {
                if (items[i].kind == MenuItemKind::Action && !items[i].disabled) {
                    activeItem_ = i;
                    return;
                }
            }
        } else {
            for (std::size_t i = items.size(); i > 0; --i) {
                if (items[i - 1].kind == MenuItemKind::Action && !items[i - 1].disabled) {
                    activeItem_ = i - 1;
                    return;
                }
            }
        }
        activeItem_.reset();
    }

    [[nodiscard]] MenuBarEvents ActivateSelected() {
        if (!activeMenu_ || !activeItem_) return MenuBarEvents{true};
        const std::size_t menu = *activeMenu_;
        const std::size_t itemIndex = *activeItem_;
        const MenuItem& item = menus_[menu].items[itemIndex];
        if (item.kind == MenuItemKind::Separator || item.disabled) return MenuBarEvents{true};
        Close();
        return MenuBarEvents{true, true, true, menu, itemIndex};
    }

    std::vector<MenuBarEntry> menus_;
    std::vector<Rect> headerRects_;
    Rect viewport_{};
    float barHeight_ = 0.0f;
    std::optional<std::size_t> activeMenu_;
    std::optional<std::size_t> activeItem_;
    std::optional<std::size_t> pressedItem_;
};

} // namespace ToolEngine::SpriteUi::Widgets::Legacy
