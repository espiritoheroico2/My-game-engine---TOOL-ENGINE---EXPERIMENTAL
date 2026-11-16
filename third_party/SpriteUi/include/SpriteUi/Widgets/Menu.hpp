#pragma once

#include "SpriteUi/Widgets/MenuBar.hpp"

#include <cstddef>
#include <limits>
#include <vector>

namespace ToolEngine::SpriteUi::Widgets {

inline constexpr std::size_t NoMenuItemIndex = std::numeric_limits<std::size_t>::max();

enum class MenuEvent {
    None,
    Activated
};

class MenuControl {
public:
    void Open(std::vector<MenuItem> items) {
        items_ = std::move(items);
        open_ = true;
        highlighted_ = NoMenuItemIndex;
        for (std::size_t i = 0; i < items_.size(); ++i) {
            if (items_[i].kind == MenuItemKind::Action && !items_[i].disabled) {
                highlighted_ = i;
                break;
            }
        }
    }

    void Close() noexcept {
        open_ = false;
        items_.clear();
        activationPath_.clear();
    }

    [[nodiscard]] bool IsOpen() const noexcept { return open_; }
    [[nodiscard]] const std::vector<MenuItem>& Items(std::size_t depth) const noexcept {
        return depth == 0 ? items_ : empty_;
    }
    [[nodiscard]] std::size_t Highlighted(std::size_t depth) const noexcept {
        return depth == 0 ? highlighted_ : NoMenuItemIndex;
    }
    [[nodiscard]] const std::vector<std::size_t>& ActivationPath() const noexcept { return activationPath_; }

    [[nodiscard]] MenuEvent Click(std::size_t depth, std::size_t item) {
        if (!open_ || depth != 0 || item >= items_.size() ||
            items_[item].kind == MenuItemKind::Separator || items_[item].disabled) return MenuEvent::None;
        activationPath_ = {item};
        return MenuEvent::Activated;
    }

private:
    std::vector<MenuItem> items_;
    std::vector<std::size_t> activationPath_;
    std::size_t highlighted_ = NoMenuItemIndex;
    bool open_ = false;
    inline static const std::vector<MenuItem> empty_{};
};

} // namespace ToolEngine::SpriteUi::Widgets
