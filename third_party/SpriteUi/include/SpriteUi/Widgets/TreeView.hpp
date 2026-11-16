#pragma once

#include <cstddef>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace ToolEngine::SpriteUi::Widgets {

inline constexpr std::size_t NoControlIndex = std::numeric_limits<std::size_t>::max();

enum class TreeKey {
    Up,
    Down,
    Left,
    Right,
    Enter
};

struct TreeRow {
    std::size_t node = NoControlIndex;
    std::size_t depth = 0;
};

class TreeControl {
public:
    [[nodiscard]] std::size_t AddNode(std::string label, std::size_t parent = NoControlIndex) {
        if (parent >= nodes_.size()) parent = NoControlIndex;
        const std::size_t index = nodes_.size();
        nodes_.push_back(Node{std::move(label), parent});
        if (parent == NoControlIndex) roots_.push_back(index);
        else nodes_[parent].children.push_back(index);
        rowsDirty_ = true;
        return index;
    }

    [[nodiscard]] std::size_t NodeCount() const noexcept { return nodes_.size(); }
    [[nodiscard]] const std::vector<TreeRow>& VisibleRows() const noexcept {
        ensureRows();
        return visibleRows_;
    }

    [[nodiscard]] bool HasChildren(std::size_t node) const noexcept {
        return node < nodes_.size() && !nodes_[node].children.empty();
    }

    [[nodiscard]] bool IsExpanded(std::size_t node) const noexcept {
        return node < nodes_.size() && nodes_[node].expanded;
    }

    bool SetExpanded(std::size_t node, bool expanded) {
        if (!HasChildren(node) || nodes_[node].expanded == expanded) return false;
        nodes_[node].expanded = expanded;
        rowsDirty_ = true;
        rebuildRows();
        return true;
    }

    bool Toggle(std::size_t node) {
        if (!HasChildren(node)) return false;
        return SetExpanded(node, !nodes_[node].expanded);
    }

    [[nodiscard]] std::size_t Selected() const noexcept { return selected_; }

    bool Select(std::size_t node) noexcept {
        ensureRows();
        if (node >= nodes_.size() || !isVisible(node) || selected_ == node) return false;
        selected_ = node;
        return true;
    }

    bool Key(TreeKey key) {
        ensureRows();
        if (visibleRows_.empty()) return false;
        if (selected_ >= nodes_.size()) return Select(visibleRows_.front().node);
        switch (key) {
            case TreeKey::Up:
                return moveSelection(-1);
            case TreeKey::Down:
                return moveSelection(1);
            case TreeKey::Left:
                if (HasChildren(selected_) && IsExpanded(selected_)) {
                    return SetExpanded(selected_, false);
                }
                if (nodes_[selected_].parent != NoControlIndex) return Select(nodes_[selected_].parent);
                return false;
            case TreeKey::Right:
                if (!HasChildren(selected_)) return false;
                if (!IsExpanded(selected_)) return SetExpanded(selected_, true);
                return Select(nodes_[selected_].children.front());
            case TreeKey::Enter:
                return Toggle(selected_);
        }
        return false;
    }

private:
    struct Node {
        std::string label;
        std::size_t parent = NoControlIndex;
        std::vector<std::size_t> children;
        bool expanded = true;
    };

    [[nodiscard]] bool isVisible(std::size_t node) const noexcept {
        for (const TreeRow& row : visibleRows_) if (row.node == node) return true;
        return false;
    }

    bool moveSelection(int direction) noexcept {
        std::size_t row = 0;
        while (row < visibleRows_.size() && visibleRows_[row].node != selected_) ++row;
        if (row == visibleRows_.size()) return Select(visibleRows_.front().node);
        if (direction < 0) {
            if (row == 0) return false;
            return Select(visibleRows_[row - 1].node);
        }
        if (row + 1 >= visibleRows_.size()) return false;
        return Select(visibleRows_[row + 1].node);
    }

    void ensureRows() const {
        if (rowsDirty_) rebuildRows();
    }

    void rebuildRows() const {
        visibleRows_.clear();
        std::vector<TreeRow> stack;
        for (auto root = roots_.rbegin(); root != roots_.rend(); ++root) stack.push_back({*root, 0});
        while (!stack.empty()) {
            const TreeRow row = stack.back();
            stack.pop_back();
            visibleRows_.push_back(row);
            if (!nodes_[row.node].expanded) continue;
            const auto& children = nodes_[row.node].children;
            for (auto child = children.rbegin(); child != children.rend(); ++child) {
                stack.push_back({*child, row.depth + 1});
            }
        }
        if (selected_ < nodes_.size() && !isVisible(selected_)) {
            std::size_t hidden = selected_;
            while (nodes_[hidden].parent != NoControlIndex && !isVisible(hidden)) hidden = nodes_[hidden].parent;
            selected_ = isVisible(hidden) ? hidden : NoControlIndex;
        }
        rowsDirty_ = false;
    }

    std::vector<Node> nodes_;
    std::vector<std::size_t> roots_;
    mutable std::vector<TreeRow> visibleRows_;
    mutable std::size_t selected_ = NoControlIndex;
    mutable bool rowsDirty_ = true;
};

} // namespace ToolEngine::SpriteUi::Widgets
