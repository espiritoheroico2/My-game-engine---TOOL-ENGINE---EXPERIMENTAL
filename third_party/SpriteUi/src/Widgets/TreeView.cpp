#include "SpriteUi/Widgets/TreeView.hpp"

#include <algorithm>
#include <cstddef>

namespace ToolEngine::SpriteUi::Widgets {

// Adicionar um no reconstroi as linhas (O(n)). Monte a arvore antes do laco de quadros.
std::size_t TreeControl::AddNode(std::string label, std::size_t parent) {
    const bool parentValid = parent != NoControlIndex && parent < nodes_.size();
    const std::size_t index = nodes_.size();
    Node node;
    node.label = std::move(label);
    node.parent = parentValid ? parent : NoControlIndex;
    nodes_.push_back(std::move(node));

    if (parentValid) {
        nodes_[parent].children.push_back(index);
    } else {
        roots_.push_back(index);
    }
    RebuildRows();
    return index;
}

bool TreeControl::SetExpanded(std::size_t node, bool expanded) {
    if (node >= nodes_.size() || !HasChildren(node) || nodes_[node].expanded == expanded) return false;

    nodes_[node].expanded = expanded;
    if (expanded) {
        InsertDescendantRows(node);
        return true;
    }
    EraseDescendantRows(node);
    // Recolher pode esconder a selecao: ela sobe para o no recolhido, que continua visivel.
    if (selected_ != NoControlIndex && RowOf(selected_) == NoControlIndex) {
        selected_ = node;
    }
    return true;
}

bool TreeControl::Toggle(std::size_t node) {
    if (disabled) return false;
    return SetExpanded(node, !IsExpanded(node));
}

bool TreeControl::Select(std::size_t node) {
    if (disabled || node == selected_ || RowOf(node) == NoControlIndex) return false;
    selected_ = node;
    return true;
}

bool TreeControl::Key(TreeKey key) {
    if (disabled) return false;
    switch (key) {
        case TreeKey::Up:
            return MoveSelection(-1);
        case TreeKey::Down:
            return MoveSelection(1);
        case TreeKey::Right:
            return KeyRight();
        case TreeKey::Left:
            return KeyLeft();
        case TreeKey::Enter:
            return selected_ != NoControlIndex && SetExpanded(selected_, !IsExpanded(selected_));
    }
    return false;
}

void TreeControl::RebuildRows() {
    ++rebuildCount_;
    rows_.clear();
    for (std::size_t root : roots_) {
        AppendSubtree(root, 0, rows_);
    }
}

void TreeControl::AppendSubtree(std::size_t node, std::size_t depth, std::vector<TreeRow>& out) const {
    out.push_back({node, depth});
    if (!nodes_[node].expanded) return;
    for (std::size_t child : nodes_[node].children) {
        AppendSubtree(child, depth + 1, out);
    }
}

// Um no escondido nao tem linha: a expansao so mexe na lista quando o no esta visivel.
void TreeControl::InsertDescendantRows(std::size_t node) {
    const std::size_t row = RowOf(node);
    if (row == NoControlIndex) return;
    std::vector<TreeRow> descendants;
    for (std::size_t child : nodes_[node].children) {
        AppendSubtree(child, rows_[row].depth + 1, descendants);
    }
    rows_.insert(rows_.begin() + static_cast<std::ptrdiff_t>(row + 1), descendants.begin(), descendants.end());
}

void TreeControl::EraseDescendantRows(std::size_t node) {
    const std::size_t row = RowOf(node);
    if (row == NoControlIndex) return;
    const std::size_t depth = rows_[row].depth;
    const auto first = rows_.begin() + static_cast<std::ptrdiff_t>(row + 1);
    const auto last = std::find_if(first, rows_.end(), [depth](const TreeRow& r) { return r.depth <= depth; });
    rows_.erase(first, last);
}

std::size_t TreeControl::RowOf(std::size_t node) const noexcept {
    const auto it = std::find_if(rows_.begin(), rows_.end(), [node](const TreeRow& row) { return row.node == node; });
    return it == rows_.end() ? NoControlIndex : static_cast<std::size_t>(it - rows_.begin());
}

bool TreeControl::MoveSelection(int delta) {
    if (rows_.empty()) return false;

    const std::size_t last = rows_.size() - 1;
    std::size_t target = 0;
    if (selected_ == NoControlIndex) {
        target = delta > 0 ? 0 : last;
    } else {
        const std::size_t row = RowOf(selected_);
        if (row == NoControlIndex) return false;
        const long next = std::clamp(static_cast<long>(row) + delta, 0L, static_cast<long>(last));
        target = static_cast<std::size_t>(next);
        if (target == row) return false;
    }
    selected_ = rows_[target].node;
    return true;
}

bool TreeControl::KeyRight() {
    if (selected_ == NoControlIndex || !HasChildren(selected_)) return false;
    if (!IsExpanded(selected_)) return SetExpanded(selected_, true);
    selected_ = nodes_[selected_].children.front();
    return true;
}

bool TreeControl::KeyLeft() {
    if (selected_ == NoControlIndex) return false;
    if (IsExpanded(selected_)) return SetExpanded(selected_, false);
    if (Parent(selected_) == NoControlIndex) return false;
    selected_ = Parent(selected_);
    return true;
}

} // namespace ToolEngine::SpriteUi::Widgets
