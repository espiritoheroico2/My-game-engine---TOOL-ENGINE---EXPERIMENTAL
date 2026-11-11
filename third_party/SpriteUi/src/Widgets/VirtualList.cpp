#include "SpriteUi/Widgets/VirtualList.hpp"

namespace ToolEngine::SpriteUi::Widgets {

VirtualListRange UIVirtualList::ComputeVisibleRange(float scrollOffsetY, float viewportHeight) const noexcept {
    VirtualListRange result{};
    if (totalItems_ == 0 || viewportHeight <= 0.0f || !IsFinite(scrollOffsetY) || !IsFinite(viewportHeight)) {
        return result;
    }

    const float scrollY = std::max(0.0f, scrollOffsetY);
    const float firstFloat = std::floor(scrollY / itemHeight_);
    const float lastFloat = std::ceil((scrollY + viewportHeight) / itemHeight_);

    // Limita ao ultimo item antes do cast: floats gigantes tornariam a conversao indefinida.
    const float maxIndex = static_cast<float>(totalItems_ - 1);
    const std::size_t rawFirst = static_cast<std::size_t>(std::clamp(firstFloat, 0.0f, maxIndex));
    const std::size_t rawLast = static_cast<std::size_t>(std::clamp(lastFloat, 0.0f, maxIndex));

    // Apply overscan
    std::size_t first = (rawFirst > overscan_) ? (rawFirst - overscan_) : 0;
    std::size_t last = std::min(totalItems_ - 1, rawLast + overscan_);

    if (first >= totalItems_) {
        first = totalItems_ - 1;
    }
    if (last < first) {
        last = first;
    }

    result.firstIndex = first;
    result.lastIndex = last;
    result.count = (last >= first) ? (last - first + 1) : 0;
    return result;
}

bool UIVirtualList::IsItemVisible(std::size_t index, float scrollOffsetY, float viewportHeight) const noexcept {
    if (index >= totalItems_) return false;
    const auto range = ComputeVisibleRange(scrollOffsetY, viewportHeight);
    return index >= range.firstIndex && index <= range.lastIndex;
}

} // namespace ToolEngine::SpriteUi::Widgets

