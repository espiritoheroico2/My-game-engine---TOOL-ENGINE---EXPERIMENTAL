#pragma once

#include "SpriteUi/Core/Math.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace ToolEngine::SpriteUi::Widgets {

struct VirtualListRange {
    std::size_t firstIndex = 0;
    std::size_t lastIndex = 0; // Inclusive
    std::size_t count = 0;
};

// Computes visible item ranges for high-density virtualized lists / data grids (Section 10).
// Maintains constant O(1) CPU/GPU cost even for collections with 100,000+ items.
class UIVirtualList {
public:
    UIVirtualList() = default;
    UIVirtualList(std::size_t totalItems, float itemHeight, std::size_t overscan = 2) noexcept
        : totalItems_{totalItems}, itemHeight_{std::max(1.0f, itemHeight)}, overscan_{overscan} {}

    void SetTotalItems(std::size_t count) noexcept { totalItems_ = count; }
    [[nodiscard]] std::size_t GetTotalItems() const noexcept { return totalItems_; }

    void SetItemHeight(float height) noexcept { itemHeight_ = std::max(1.0f, height); }
    [[nodiscard]] float GetItemHeight() const noexcept { return itemHeight_; }

    void SetOverscan(std::size_t overscan) noexcept { overscan_ = overscan; }
    [[nodiscard]] std::size_t GetOverscan() const noexcept { return overscan_; }

    [[nodiscard]] float GetTotalContentHeight() const noexcept {
        return static_cast<float>(totalItems_) * itemHeight_;
    }

    [[nodiscard]] float GetItemOffsetY(std::size_t index) const noexcept {
        return static_cast<float>(index) * itemHeight_;
    }

    [[nodiscard]] VirtualListRange ComputeVisibleRange(float scrollOffsetY, float viewportHeight) const noexcept;

    [[nodiscard]] bool IsItemVisible(std::size_t index, float scrollOffsetY, float viewportHeight) const noexcept;

private:
    std::size_t totalItems_ = 0;
    float itemHeight_ = 24.0f;
    std::size_t overscan_ = 2;
};

} // namespace ToolEngine::SpriteUi::Widgets

