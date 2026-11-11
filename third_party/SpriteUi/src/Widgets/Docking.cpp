#include "SpriteUi/Widgets/Docking.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace ToolEngine::SpriteUi::Widgets {

bool UITabGroupComponent::AddTab(std::string title, UIHandle contentHandle, bool closeable) {
    if (!contentHandle.IsValid()) return false;
    tabs_.push_back(UITab{std::move(title), contentHandle, closeable});
    return true;
}

bool UITabGroupComponent::RemoveTab(std::size_t index) {
    if (index >= tabs_.size()) return false;
    tabs_.erase(tabs_.begin() + static_cast<std::ptrdiff_t>(index));
    if (tabs_.empty()) {
        activeTabIndex_ = 0;
    } else if (index < activeTabIndex_) {
        --activeTabIndex_; // Mantem a mesma aba ativa apos remover uma anterior.
    } else if (activeTabIndex_ >= tabs_.size()) {
        activeTabIndex_ = tabs_.size() - 1;
    }
    return true;
}

bool UITabGroupComponent::SelectTab(std::size_t index) noexcept {
    if (index >= tabs_.size()) return false;
    activeTabIndex_ = index;
    return true;
}

UIHandle UITabGroupComponent::GetActiveContent() const noexcept {
    if (tabs_.empty() || activeTabIndex_ >= tabs_.size()) {
        return InvalidUIHandle;
    }
    return tabs_[activeTabIndex_].contentHandle;
}

bool UIDockNode::Split(DockSlot slot, float ratio, UIHandle newContent) {
    if (slot == DockSlot::None || !newContent.IsValid() || isSplit) {
        return false;
    }
    if (!std::isfinite(ratio)) return false;

    if (slot == DockSlot::Center) {
        // Nunca descarta o conteudo existente: agrupa em abas.
        if (!content.IsValid() && tabs.TabCount() == 0) {
            content = newContent;
            return true;
        }
        if (tabs.TabCount() == 0) (void)tabs.AddTab("", content);
        (void)tabs.AddTab("", newContent);
        return tabs.SelectTab(tabs.TabCount() - 1);
    }

    const float clampedRatio = std::clamp(ratio, 0.05f, 0.95f);
    splitRatio = clampedRatio;

    auto originalContent = content;
    content = InvalidUIHandle;
    isSplit = true;

    firstChild = std::make_unique<UIDockNode>(originalContent);
    firstChild->tabs = std::move(tabs); // As abas acompanham o conteudo original.
    tabs = UITabGroupComponent{};
    secondChild = std::make_unique<UIDockNode>(newContent);

    if (slot == DockSlot::Left) {
        splitDirection = LayoutDirection::Row;
        splitRatio = clampedRatio;
        std::swap(firstChild, secondChild);
    } else if (slot == DockSlot::Right) {
        splitDirection = LayoutDirection::Row;
        splitRatio = 1.0f - clampedRatio;
    } else if (slot == DockSlot::Top) {
        splitDirection = LayoutDirection::Column;
        splitRatio = clampedRatio;
        std::swap(firstChild, secondChild);
    } else if (slot == DockSlot::Bottom) {
        splitDirection = LayoutDirection::Column;
        splitRatio = 1.0f - clampedRatio;
    }

    return true;
}

void UIDockNode::Layout(Rect availableBounds) {
    bounds = availableBounds;
    if (!isSplit || !firstChild || !secondChild) {
        return;
    }

    const float r = std::clamp(splitRatio, 0.05f, 0.95f);

    if (splitDirection == LayoutDirection::Row) {
        const float firstW = availableBounds.size.x * r;
        const float secondW = availableBounds.size.x - firstW;

        Rect firstRect{availableBounds.position, {firstW, availableBounds.size.y}};
        Rect secondRect{{availableBounds.position.x + firstW, availableBounds.position.y}, {secondW, availableBounds.size.y}};

        firstChild->Layout(firstRect);
        secondChild->Layout(secondRect);
    } else {
        const float firstH = availableBounds.size.y * r;
        const float secondH = availableBounds.size.y - firstH;

        Rect firstRect{availableBounds.position, {availableBounds.size.x, firstH}};
        Rect secondRect{{availableBounds.position.x, availableBounds.position.y + firstH}, {availableBounds.size.x, secondH}};

        firstChild->Layout(firstRect);
        secondChild->Layout(secondRect);
    }
}

} // namespace ToolEngine::SpriteUi::Widgets
