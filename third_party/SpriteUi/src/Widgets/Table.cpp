#include "SpriteUi/Widgets/Table.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace ToolEngine::SpriteUi::Widgets {

namespace {

[[nodiscard]] float Sanitize(float value) noexcept {
    return IsFinite(value) ? value : 0.0f;
}

} // namespace

std::vector<float> ResolveColumnWidths(const std::vector<TableColumn>& columns, float totalWidth) {
    std::vector<float> widths(columns.size(), 0.0f);
    const float total = std::max(0.0f, Sanitize(totalWidth));

    float fixedSum = 0.0f;
    float weightSum = 0.0f;
    for (std::size_t i = 0; i < columns.size(); ++i) {
        const TableColumn& col = columns[i];
        const float min = std::max(0.0f, Sanitize(col.minWidth));
        if (col.sizing == ColumnSizing::Fixed) {
            widths[i] = std::max(min, Sanitize(col.width));
            fixedSum += widths[i];
        } else {
            weightSum += std::max(0.0f, Sanitize(col.weight));
        }
    }

    const float remaining = std::max(0.0f, total - fixedSum);
    for (std::size_t i = 0; i < columns.size(); ++i) {
        const TableColumn& col = columns[i];
        if (col.sizing != ColumnSizing::Proportional) continue;
        const float share = weightSum > 0.0f ? remaining * std::max(0.0f, Sanitize(col.weight)) / weightSum : 0.0f;
        widths[i] = std::max(std::max(0.0f, Sanitize(col.minWidth)), share);
    }
    return widths;
}

void TableControl::SetColumns(std::vector<TableColumn> columns) {
    columns_ = std::move(columns);
    UpdateWidths();
}

void TableControl::SetSize(float width, float height) {
    width_ = Sanitize(width);
    height_ = Sanitize(height);
    UpdateWidths();
    rows.SetViewportHeight(std::max(0.0f, height_ - headerHeight_));
}

void TableControl::SetHeaderHeight(float height) {
    headerHeight_ = std::max(0.0f, Sanitize(height));
    rows.SetViewportHeight(std::max(0.0f, height_ - headerHeight_));
}

void TableControl::SetRowHeight(float height) noexcept {
    rows.SetItemHeight(height);
}

void TableControl::UpdateWidths() noexcept {
    widths_ = ResolveColumnWidths(columns_, width_);
}

float TableControl::ColumnX(std::size_t column) const noexcept {
    const std::size_t count = std::min(column, widths_.size());
    return std::accumulate(widths_.begin(), widths_.begin() + static_cast<std::ptrdiff_t>(count), 0.0f);
}

float TableControl::RowTop(std::size_t row) const noexcept {
    return headerHeight_ + rows.items.GetItemOffsetY(row) - rows.scrollOffsetY;
}

std::size_t TableControl::RowAt(Vec2 local) const noexcept {
    if (!IsFinite(local) || local.y < headerHeight_ || local.x < 0.0f || local.x >= width_) return NoControlIndex;
    const float rowIndex = std::floor((local.y - headerHeight_ + rows.scrollOffsetY) / rows.items.GetItemHeight());
    if (rowIndex < 0.0f || rowIndex >= static_cast<float>(rows.items.GetTotalItems())) return NoControlIndex;
    return static_cast<std::size_t>(rowIndex);
}

bool TableControl::ScrollBy(float deltaY) noexcept {
    if (!IsFinite(deltaY)) return false;
    const float before = rows.scrollOffsetY;
    rows.scrollOffsetY += deltaY;
    rows.ClampScroll();
    return rows.scrollOffsetY != before;
}

} // namespace ToolEngine::SpriteUi::Widgets
