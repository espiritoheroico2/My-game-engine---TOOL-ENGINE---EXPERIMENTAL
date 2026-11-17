#include "SpriteUi/Elements/ListBox.hpp"

#include "SpriteUi/Rendering/Primitives.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace ToolEngine::SpriteUi::Elements {

namespace {

using Widgets::NoControlIndex;
using Widgets::NoListItem;

bool Contains(const std::vector<ListBoxItemId>& ids, ListBoxItemId id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

// Contorno de foco desenhado com recorte: quatro faixas finas por dentro do retangulo.
void AppendFocusRing(UIHandle owner, Rect box, const ListBoxStyle& style, std::uint32_t clipIndex, float zOrder,
                     std::uint64_t& paint, std::vector<Rendering::UIRenderItem>& out) {
    const float w = style.focusRingWidth;
    const Rect edges[] = {
        {box.position, {box.size.x, w}},
        {{box.position.x, box.position.y + box.size.y - w}, {box.size.x, w}},
        {{box.position.x, box.position.y + w}, {w, std::max(0.0f, box.size.y - 2.0f * w)}},
        {{box.position.x + box.size.x - w, box.position.y + w}, {w, std::max(0.0f, box.size.y - 2.0f * w)}},
    };
    for (const Rect& edge : edges) {
        out.push_back(Rendering::MakeSolidRect(owner, edge, style.focusRing, clipIndex, zOrder, paint++));
    }
}

// Barra de rolagem na borda direita: trilha de altura da area e polegar proporcional a parte visivel.
void AppendScrollBar(const ListBox& list, const ListBoxStyle& style, std::uint32_t clipIndex, float zOrder,
                     std::uint64_t& paint, std::vector<Rendering::UIRenderItem>& out) {
    const float maxScroll = list.view.MaxScrollY();
    if (maxScroll <= 0.0f) return;
    const Rect content = list.ContentRect();
    const Rect track{{content.position.x + content.size.x - style.scrollWidth, content.position.y},
                     {style.scrollWidth, content.size.y}};
    out.push_back(Rendering::MakeSolidRect(list.handle, track, style.scrollTrack, clipIndex, zOrder, paint++));

    const float total = list.view.items.GetTotalContentHeight();
    const float thumbHeight = std::clamp(list.view.viewportHeight * list.view.viewportHeight / total,
                                         std::min(style.minThumbHeight, track.size.y), track.size.y);
    const float travel = track.size.y - thumbHeight;
    const float thumbY = track.position.y + travel * (list.view.scrollOffsetY / maxScroll);
    const Rect thumb{{track.position.x, thumbY}, {track.size.x, thumbHeight}};
    out.push_back(Rendering::MakeSolidRect(list.handle, thumb, style.scrollThumb, clipIndex, zOrder, paint++));
}

} // namespace

ListBox::ListBox() {
    view.SetItemHeight(kDefaultRowHeight);
}

void ListBox::SetItems(std::vector<ListBoxItemId> ids) noexcept {
    ids_ = std::move(ids);
    view.SetItemIds(ids_);
    std::erase_if(selected_, [this](ListBoxItemId id) { return IndexOf(id) == NoControlIndex; });
    if (IndexOf(anchor_) == NoControlIndex) anchor_ = NoListItem;
}

bool ListBox::AddItem(ListBoxItemId id) {
    if (id == NoListItem || IndexOf(id) != NoControlIndex) return false;
    ids_.push_back(id);
    view.SetItemIds(ids_);
    return true;
}

ListBoxItemId ListBox::ItemAt(std::size_t index) const noexcept {
    return index < ids_.size() ? ids_[index] : NoListItem;
}

bool ListBox::IsSelected(ListBoxItemId id) const noexcept {
    return Contains(selected_, id);
}

void ListBox::UpdateViewport() noexcept {
    view.SetViewportHeight(ContentRect().size.y);
}

float ListBox::PreferredHeight(float maxHeight) const noexcept {
    const float content = view.items.GetTotalContentHeight();
    return std::clamp(content, 0.0f, std::max(0.0f, maxHeight));
}

ListBoxEvents ListBox::OnPointer(const PointerEvent& event, ListModifiers modifiers) noexcept {
    UpdateViewport();
    const GestureResult gesture = gesture_.Apply(states, event);
    ListBoxEvents events;
    events.pressStarted = gesture.pressStarted;
    events.pressEnded = gesture.pressEnded;
    events.clicked = gesture.clicked;
    if (!gesture.clicked) return events;
    const std::size_t row = RowAt(event.position);
    if (row != NoControlIndex) events.selectionChanged = Click(ids_[row], modifiers);
    return events;
}

bool ListBox::Click(ListBoxItemId id, ListModifiers modifiers) noexcept {
    UpdateViewport();
    const std::size_t index = IndexOf(id);
    if (states.disabled || index == NoControlIndex) return false;

    std::vector<ListBoxItemId> next;
    if (modifiers.shift) {
        const std::size_t anchor = IndexOf(anchor_);
        if (modifiers.ctrl) next = selected_;
        AddRange(next, anchor == NoControlIndex ? index : anchor, index);
    } else if (modifiers.ctrl) {
        next = selected_;
        const auto found = std::find(next.begin(), next.end(), id);
        if (found == next.end()) {
            next.push_back(id);
        } else {
            next.erase(found);
        }
        anchor_ = id;
    } else {
        next.push_back(id);
        anchor_ = id;
    }
    return ApplySelection(std::move(next), index);
}

bool ListBox::Key(ListBoxKey key, ListModifiers modifiers) noexcept {
    UpdateViewport();
    if (states.disabled || ids_.empty()) return false;

    const std::size_t current = view.SelectedIndex();
    const std::size_t target = TargetIndex(key, current);
    if (!modifiers.shift) {
        anchor_ = ids_[target];
        return ApplySelection({ids_[target]}, target);
    }

    std::size_t from = IndexOf(anchor_);
    if (from == NoControlIndex) from = current == NoControlIndex ? target : current;
    std::vector<ListBoxItemId> next;
    if (modifiers.ctrl) next = selected_;
    AddRange(next, from, target);
    return ApplySelection(std::move(next), target);
}

std::size_t ListBox::RowAt(Vec2 point) const noexcept {
    const Rect content = ContentRect();
    const float rowHeight = RowHeight();
    if (!IsFinite(point) || rowHeight <= 0.0f) return NoControlIndex;
    const bool insideX = point.x >= content.position.x && point.x <= content.position.x + content.size.x;
    const float fromTop = point.y - content.position.y;
    if (!insideX || fromTop < 0.0f || fromTop > content.size.y) return NoControlIndex;
    const float row = (fromTop + view.scrollOffsetY) / rowHeight;
    if (row >= static_cast<float>(ids_.size())) return NoControlIndex;
    return static_cast<std::size_t>(row);
}

Rect ListBox::RowRect(std::size_t index) const noexcept {
    if (index >= ids_.size()) return {};
    const Rect content = ContentRect();
    const float top = content.position.y + static_cast<float>(index) * RowHeight() - view.scrollOffsetY;
    return {{content.position.x, top}, {content.size.x, RowHeight()}};
}

std::size_t ListBox::IndexOf(ListBoxItemId id) const noexcept {
    if (id == NoListItem) return NoControlIndex;
    const auto found = std::find(ids_.begin(), ids_.end(), id);
    return found == ids_.end() ? NoControlIndex : static_cast<std::size_t>(found - ids_.begin());
}

std::size_t ListBox::TargetIndex(ListBoxKey key, std::size_t current) const noexcept {
    const std::size_t last = ids_.size() - 1;
    if (current == NoControlIndex) return key == ListBoxKey::End ? last : 0;
    const std::size_t page = PageRows();
    switch (key) {
    case ListBoxKey::Up:
        return current > 0 ? current - 1 : 0;
    case ListBoxKey::Down:
        return std::min(current + 1, last);
    case ListBoxKey::Home:
        return 0;
    case ListBoxKey::End:
        return last;
    case ListBoxKey::PageUp:
        return current > page ? current - page : 0;
    case ListBoxKey::PageDown:
        return std::min(current + page, last);
    }
    return current;
}

// Linhas inteiras que cabem na area visivel; nunca menos de uma.
std::size_t ListBox::PageRows() const noexcept {
    const float rows = std::floor(view.viewportHeight / RowHeight());
    return rows >= 1.0f ? static_cast<std::size_t>(rows) : 1;
}

// Acrescenta em out os itens entre from e to (inclusive, em qualquer ordem), sem repetir os que ja estao la.
void ListBox::AddRange(std::vector<ListBoxItemId>& out, std::size_t from, std::size_t to) const {
    const auto [low, high] = std::minmax(from, to);
    for (std::size_t i = low; i <= high; ++i) {
        if (!Contains(out, ids_[i])) out.push_back(ids_[i]);
    }
}

bool ListBox::ApplySelection(std::vector<ListBoxItemId> next, std::size_t cursor) noexcept {
    const bool changed = next != selected_;
    selected_ = std::move(next);
    (void)view.Select(cursor);
    return changed;
}

std::vector<Rendering::UIRenderItem> BuildListBoxItems(const ListBox& list, const ListBoxStyle& style,
                                                       std::uint32_t clipIndex, float zOrder,
                                                       std::uint64_t paintOrder) {
    std::vector<Rendering::UIRenderItem> out;
    if (!IsFinite(list.border.position) || !IsFinite(list.border.size)) return out;
    std::uint64_t paint = paintOrder;
    out.push_back(Rendering::MakeSolidRect(list.handle, list.border, style.background, clipIndex, zOrder, paint++));

    const Widgets::VirtualListRange visible = list.view.items.ComputeVisibleRange(list.view.scrollOffsetY,
                                                                          list.view.viewportHeight);
    if (list.ItemCount() > 0) {
        const bool focused = list.states.focused && !list.states.disabled;
        const std::size_t cursor = list.view.SelectedIndex();
        for (std::size_t i = visible.firstIndex; i < visible.firstIndex + visible.count; ++i) {
            const Rect row = list.RowRect(i);
            if (list.IsSelected(list.ItemAt(i))) {
                out.push_back(Rendering::MakeSolidRect(list.handle, row, style.selected, clipIndex, zOrder, paint++));
            }
            const Rect grid{{row.position.x, row.position.y + row.size.y - style.gridWidth},
                            {row.size.x, style.gridWidth}};
            out.push_back(Rendering::MakeSolidRect(list.handle, grid, style.gridLine, clipIndex, zOrder, paint++));
            if (focused && i == cursor) AppendFocusRing(list.handle, row, style, clipIndex, zOrder, paint, out);
        }
    }
    AppendScrollBar(list, style, clipIndex, zOrder, paint, out);
    return out;
}

} // namespace ToolEngine::SpriteUi::Elements
