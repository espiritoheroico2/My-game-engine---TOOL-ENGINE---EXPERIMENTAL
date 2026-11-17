#include "SpriteUi/Widgets/Controls.hpp"

#include <algorithm>
#include <cstddef>
#include <numeric>
#include <utility>

namespace ToolEngine::SpriteUi::Widgets {

namespace {

[[nodiscard]] bool IsValidIndex(std::size_t index, std::size_t count) noexcept {
    return index != NoControlIndex && index < count;
}

} // namespace

ControlState ResolveControlState(const UIButtonState& button) noexcept {
    if (button.disabled) return ControlState::Disabled;
    if (button.pressed) return ControlState::Pressed;
    if (button.hovered) return ControlState::Hover;
    return ControlState::Normal;
}

// Checkbox

bool CheckboxControl::Toggle() noexcept {
    if (button.disabled) return false;
    checked = !checked;
    return true;
}

// Slider

float SliderControl::KeyStep() const noexcept {
    if (IsFinite(slider.step) && slider.step > 0.0f) return slider.step;
    const float range = slider.maxValue - slider.minValue;
    if (!IsFinite(range) || range <= 0.0f) return 0.0f;
    return range * kContinuousKeyFraction;
}

bool SliderControl::SetFromPointer(float localX, float trackWidth) noexcept {
    if (disabled) return false;
    const float before = slider.value;
    if (!slider.SetFromPointer(localX, trackWidth)) return false;
    return slider.value != before;
}

bool SliderControl::SetValue(float value) noexcept {
    const float before = slider.value;
    if (!slider.SetValue(value)) return false;
    return slider.value != before;
}

bool SliderControl::Key(SliderKey key) noexcept {
    if (disabled) return false;
    const float before = slider.value;
    switch (key) {
    case SliderKey::Home:
        if (!slider.SetValue(slider.minValue)) return false;
        break;
    case SliderKey::End:
        if (!slider.SetValue(slider.maxValue)) return false;
        break;
    case SliderKey::Left:
    case SliderKey::Down:
    case SliderKey::Right:
    case SliderKey::Up: {
        const float step = KeyStep();
        if (step <= 0.0f) return false;
        const bool decrease = key == SliderKey::Left || key == SliderKey::Down;
        if (!slider.SetValue(slider.value + (decrease ? -step : step))) return false;
        break;
    }
    }
    return slider.value != before;
}

// Dropdown

bool DropdownControl::Open() noexcept {
    if (disabled || open || options.empty()) return false;
    open = true;
    highlightedIndex = IsValidIndex(selectedIndex, options.size()) ? selectedIndex : 0;
    return true;
}

bool DropdownControl::Close() noexcept {
    const bool wasOpen = open;
    open = false;
    highlightedIndex = NoControlIndex;
    return wasOpen;
}

bool DropdownControl::Select(std::size_t index) noexcept {
    if (disabled || !IsValidIndex(index, options.size())) return false;
    const bool changed = index != selectedIndex;
    selectedIndex = index;
    Close();
    return changed;
}

bool DropdownControl::Move(int delta) noexcept {
    if (disabled || options.empty()) return false;
    const std::size_t last = options.size() - 1;

    if (open) {
        const std::size_t current = IsValidIndex(highlightedIndex, options.size()) ? highlightedIndex : 0;
        std::size_t next = current;
        if (delta > 0 && current < last) next = current + 1;
        if (delta < 0 && current > 0) next = current - 1;
        const bool changed = next != highlightedIndex;
        highlightedIndex = next;
        return changed;
    }

    std::size_t next = 0;
    if (IsValidIndex(selectedIndex, options.size())) {
        next = selectedIndex;
        if (delta > 0 && next < last) ++next;
        if (delta < 0 && next > 0) --next;
    }
    const bool changed = next != selectedIndex;
    selectedIndex = next;
    return changed;
}

bool DropdownControl::MoveDown() noexcept {
    return Move(+1);
}
bool DropdownControl::MoveUp() noexcept {
    return Move(-1);
}

bool DropdownControl::Confirm() noexcept {
    if (disabled || !open || !IsValidIndex(highlightedIndex, options.size())) return false;
    return Select(highlightedIndex);
}

// TabBar

TabBarControl::TabBarControl(std::vector<std::string> tabLabels) : labels{std::move(tabLabels)} {
    activeIndex = labels.empty() ? NoControlIndex : 0;
}

bool TabBarControl::Select(std::size_t index) noexcept {
    if (disabled || !IsValidIndex(index, labels.size()) || index == activeIndex) return false;
    activeIndex = index;
    return true;
}

// ListView

void ListViewControl::SetItemCount(std::size_t count) noexcept {
    std::vector<ListItemId> ids(count);
    std::iota(ids.begin(), ids.end(), ListItemId{0});
    SetItemIds(std::move(ids));
}

void ListViewControl::SetItemIds(std::vector<ListItemId> ids) noexcept {
    ids_ = std::move(ids);
    items.SetTotalItems(ids_.size());
    if (SelectedIndex() == NoControlIndex) selectedId = NoListItem;
    ClampScroll();
}

std::size_t ListViewControl::SelectedIndex() const noexcept {
    return IndexOf(selectedId);
}

std::size_t ListViewControl::IndexOf(ListItemId id) const noexcept {
    if (id == NoListItem) return NoControlIndex;
    const auto it = std::find(ids_.begin(), ids_.end(), id);
    return it == ids_.end() ? NoControlIndex : static_cast<std::size_t>(it - ids_.begin());
}

void ListViewControl::SetItemHeight(float height) noexcept {
    items.SetItemHeight(height);
    ClampScroll();
}

void ListViewControl::SetViewportHeight(float height) noexcept {
    viewportHeight = (IsFinite(height) && height > 0.0f) ? height : 0.0f;
    ClampScroll();
}

float ListViewControl::MaxScrollY() const noexcept {
    return std::max(0.0f, items.GetTotalContentHeight() - viewportHeight);
}

void ListViewControl::ClampScroll() noexcept {
    if (!IsFinite(scrollOffsetY)) scrollOffsetY = 0.0f;
    scrollOffsetY = std::clamp(scrollOffsetY, 0.0f, MaxScrollY());
}

bool ListViewControl::Select(std::size_t index) noexcept {
    if (!IsValidIndex(index, ids_.size())) return false;
    return SelectId(ids_[index]);
}

bool ListViewControl::SelectId(ListItemId id) noexcept {
    if (disabled || IndexOf(id) == NoControlIndex) return false;
    const bool changed = id != selectedId;
    selectedId = id;
    EnsureVisible();
    return changed;
}

bool ListViewControl::Key(ListKey key) noexcept {
    const std::size_t count = ids_.size();
    if (disabled || count == 0) return false;

    const std::size_t current = SelectedIndex();
    const bool hasSelection = IsValidIndex(current, count);
    std::size_t target = current;
    switch (key) {
    case ListKey::Up:
        target = (!hasSelection || current == 0) ? 0 : current - 1;
        break;
    case ListKey::Down:
        target = !hasSelection ? 0 : std::min(current + 1, count - 1);
        break;
    case ListKey::Home:
        target = 0;
        break;
    case ListKey::End:
        target = count - 1;
        break;
    }
    return Select(target);
}

VirtualListRange ListViewControl::VisibleRange() const noexcept {
    return items.ComputeVisibleRange(scrollOffsetY, viewportHeight);
}

void ListViewControl::EnsureVisible() noexcept {
    const std::size_t index = SelectedIndex();
    if (!IsValidIndex(index, ids_.size()) || viewportHeight <= 0.0f) return;
    const float top = items.GetItemOffsetY(index);
    const float bottom = top + items.GetItemHeight();
    if (top < scrollOffsetY) {
        scrollOffsetY = top;
    } else if (bottom > scrollOffsetY + viewportHeight) {
        scrollOffsetY = bottom - viewportHeight;
    }
    ClampScroll();
}

} // namespace ToolEngine::SpriteUi::Widgets
