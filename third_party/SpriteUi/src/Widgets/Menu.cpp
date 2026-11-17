#include "SpriteUi/Widgets/Menu.hpp"

#include <cstddef>
#include <utility>

namespace ToolEngine::SpriteUi::Widgets {

namespace {

[[nodiscard]] bool Selectable(const MenuItem& item) noexcept {
    if (item.disabled || item.kind == MenuItemKind::Separator) return false;
    return item.kind != MenuItemKind::Submenu || !item.children.empty();
}

[[nodiscard]] std::size_t FirstSelectable(const std::vector<MenuItem>& items) noexcept {
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (Selectable(items[i])) return i;
    }
    return NoControlIndex;
}

[[nodiscard]] std::size_t LastSelectable(const std::vector<MenuItem>& items) noexcept {
    for (std::size_t i = items.size(); i-- > 0;) {
        if (Selectable(items[i])) return i;
    }
    return NoControlIndex;
}

// Proximo item selecionavel a partir de `from` (com volta). Sem destaque, Up vai ao ultimo e Down ao primeiro.
[[nodiscard]] std::size_t NextSelectable(const std::vector<MenuItem>& items, std::size_t from, int step) noexcept {
    if (items.empty()) return NoControlIndex;
    if (from == NoControlIndex) return step > 0 ? FirstSelectable(items) : LastSelectable(items);

    const auto count = static_cast<long long>(items.size());
    long long index = static_cast<long long>(from);
    for (long long tries = 0; tries < count; ++tries) {
        index = (index + step + count) % count;
        if (Selectable(items[static_cast<std::size_t>(index)])) return static_cast<std::size_t>(index);
    }
    return NoControlIndex;
}

} // namespace

// MenuControl

void MenuControl::Open(const std::vector<MenuItem>& items) {
    root_ = items;
    path_.clear();
    highlights_.assign(1, FirstSelectable(root_));
    open_ = true;
}

void MenuControl::Close() noexcept {
    open_ = false;
    root_.clear();
    path_.clear();
    highlights_.clear();
}

const std::vector<MenuItem>& MenuControl::Items(std::size_t depth) const noexcept {
    const std::vector<MenuItem>* items = &root_;
    for (std::size_t level = 0; level < depth && level < path_.size(); ++level) {
        items = &(*items)[path_[level]].children;
    }
    return *items;
}

std::size_t MenuControl::Highlighted(std::size_t depth) const noexcept {
    return depth < highlights_.size() ? highlights_[depth] : NoControlIndex;
}

std::size_t MenuControl::OpenSubmenu(std::size_t depth) const noexcept {
    return depth < path_.size() ? path_[depth] : NoControlIndex;
}

MenuEvent MenuControl::Key(MenuKey key) noexcept {
    if (!open_) return MenuEvent::None;
    switch (key) {
    case MenuKey::Up:
        return MoveHighlight(-1);
    case MenuKey::Down:
        return MoveHighlight(+1);
    case MenuKey::Home:
        return SetHighlight(FirstSelectable(Current()));
    case MenuKey::End:
        return SetHighlight(LastSelectable(Current()));
    case MenuKey::Right:
        return OpenHighlightedSubmenu();
    case MenuKey::Left:
        return path_.empty() ? MenuEvent::None : PopSubmenu();
    case MenuKey::Enter:
        return ActivateHighlighted();
    case MenuKey::Escape:
        if (!path_.empty()) return PopSubmenu();
        Close();
        return MenuEvent::Dismissed;
    }
    return MenuEvent::None;
}

MenuEvent MenuControl::Click(std::size_t depth, std::size_t index) noexcept {
    if (!open_ || depth >= Depth()) return MenuEvent::None;
    if (depth < path_.size() && path_[depth] == index) return MenuEvent::None; // Submenu ja aberto.

    const bool closedDeeper = depth < path_.size();
    if (closedDeeper) {
        path_.resize(depth);
        highlights_.resize(depth + 1);
    }
    const std::vector<MenuItem>& items = Items(depth);
    if (index >= items.size() || !Selectable(items[index])) {
        return closedDeeper ? MenuEvent::Moved : MenuEvent::None;
    }
    highlights_[depth] = index;
    if (items[index].kind == MenuItemKind::Submenu) {
        PushSubmenu(index);
        return MenuEvent::Moved;
    }
    return Activate(depth, index);
}

bool MenuControl::Hover(std::size_t index) noexcept {
    if (!open_) return false;
    const std::vector<MenuItem>& items = Current();
    if (index >= items.size() || !Selectable(items[index]) || highlights_.back() == index) return false;
    highlights_.back() = index;
    return true;
}

MenuEvent MenuControl::MoveHighlight(int step) noexcept {
    const std::size_t current = highlights_.back();
    const std::size_t next = NextSelectable(Current(), current, step);
    if (next == NoControlIndex || next == current) return MenuEvent::None;
    highlights_.back() = next;
    return MenuEvent::Moved;
}

MenuEvent MenuControl::SetHighlight(std::size_t index) noexcept {
    if (index == NoControlIndex || index == highlights_.back()) return MenuEvent::None;
    highlights_.back() = index;
    return MenuEvent::Moved;
}

MenuEvent MenuControl::OpenHighlightedSubmenu() noexcept {
    const std::size_t index = highlights_.back();
    const std::vector<MenuItem>& items = Current();
    if (index >= items.size() || items[index].kind != MenuItemKind::Submenu || !Selectable(items[index])) {
        return MenuEvent::None;
    }
    PushSubmenu(index);
    return MenuEvent::Moved;
}

MenuEvent MenuControl::ActivateHighlighted() noexcept {
    const std::size_t depth = path_.size();
    const std::size_t index = highlights_.back();
    const std::vector<MenuItem>& items = Current();
    if (index >= items.size() || !Selectable(items[index])) return MenuEvent::None;
    if (items[index].kind == MenuItemKind::Submenu) {
        PushSubmenu(index);
        return MenuEvent::Moved;
    }
    return Activate(depth, index);
}

void MenuControl::PushSubmenu(std::size_t index) noexcept {
    path_.push_back(index);
    highlights_.push_back(FirstSelectable(Items(path_.size())));
}

MenuEvent MenuControl::PopSubmenu() noexcept {
    path_.pop_back();
    highlights_.pop_back();
    return MenuEvent::Moved;
}

std::vector<MenuItem>& MenuControl::MutableItems(std::size_t depth) noexcept {
    std::vector<MenuItem>* items = &root_;
    for (std::size_t level = 0; level < depth && level < path_.size(); ++level) {
        items = &(*items)[path_[level]].children;
    }
    return *items;
}

MenuEvent MenuControl::Activate(std::size_t depth, std::size_t index) noexcept {
    MenuItem& item = MutableItems(depth)[index];
    if (item.checkable) {
        // Caixa de selecao: alterna e o menu continua aberto para o usuario ver o novo valor.
        item.checked = !item.checked;
        return MenuEvent::Moved;
    }
    activation_.assign(path_.begin(), path_.begin() + static_cast<std::ptrdiff_t>(depth));
    activation_.push_back(index);
    Close();
    return MenuEvent::Activated;
}

// MenuBarControl

void MenuBarControl::SetMenus(std::vector<MenuBarEntry> menus) noexcept {
    Close();
    menus_ = std::move(menus);
}

bool MenuBarControl::OpenMenu(std::size_t index) noexcept {
    if (index >= menus_.size() || menus_[index].disabled) return false;
    if (IsOpen() && active_ == index) return false;
    dropdown_.Open(menus_[index].items);
    active_ = index;
    return true;
}

bool MenuBarControl::ClickHeader(std::size_t index) noexcept {
    if (IsOpen() && active_ == index) {
        Close();
        return true;
    }
    return OpenMenu(index);
}

bool MenuBarControl::HoverHeader(std::size_t index) noexcept {
    return IsOpen() && OpenMenu(index);
}

void MenuBarControl::Close() noexcept {
    dropdown_.Close();
    active_ = NoControlIndex;
}

MenuEvent MenuBarControl::Key(MenuKey key) noexcept {
    if (!IsOpen()) return MenuEvent::None;
    const MenuEvent event = dropdown_.Key(key);
    if (event != MenuEvent::None) return Finish(event);
    if (key == MenuKey::Left) return SwitchMenu(-1);
    if (key == MenuKey::Right) return SwitchMenu(+1);
    return MenuEvent::None;
}

MenuEvent MenuBarControl::Click(std::size_t depth, std::size_t index) noexcept {
    if (!IsOpen()) return MenuEvent::None;
    return Finish(dropdown_.Click(depth, index));
}

MenuEvent MenuBarControl::Finish(MenuEvent event) noexcept {
    SyncActiveMenu();
    if (event == MenuEvent::Activated) activatedMenu_ = active_;
    if (event == MenuEvent::Activated || event == MenuEvent::Dismissed) active_ = NoControlIndex;
    return event;
}

void MenuBarControl::SyncActiveMenu() noexcept {
    if (active_ == NoControlIndex || active_ >= menus_.size() || !dropdown_.IsOpen()) return;
    // A raiz do popup e a copia da arvore do menu; so 'checked' muda durante a interacao.
    menus_[active_].items = dropdown_.Items(0);
}

MenuEvent MenuBarControl::SwitchMenu(int step) noexcept {
    const auto count = static_cast<long long>(menus_.size());
    if (count == 0 || active_ == NoControlIndex) return MenuEvent::None;
    long long index = static_cast<long long>(active_);
    for (long long tries = 1; tries < count; ++tries) {
        index = (index + step + count) % count;
        if (OpenMenu(static_cast<std::size_t>(index))) return MenuEvent::Moved;
    }
    return MenuEvent::None;
}

} // namespace ToolEngine::SpriteUi::Widgets
