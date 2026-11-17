#include "SpriteUi/Layout/LayoutTree.hpp"

#include <algorithm>

namespace ToolEngine::SpriteUi {

UIHandle LayoutTree::Create(UIHandle parent, FlexItem style) {
    if (parent.IsValid() && !Get(parent)) {
        return InvalidUIHandle;
    }

    const UIHandle id = next_;
    if (++next_.index == 0) {
        ++next_.generation;
    }

    layoutDirty_ = true;
    LayoutElement element;
    element.id = id;
    element.parent = parent;
    element.node.style = style;
    elements_.emplace(id.Pack(), std::move(element));

    if (parent.IsValid()) {
        Get(parent)->children.push_back(id);
    } else {
        roots_.push_back(id);
    }

    return id;
}

void LayoutTree::RemoveSubtree(UIHandle id) {
    auto* element = Get(id);
    if (!element) return;

    // Copy children list because recursive removal invalidates element storage
    const auto children = element->children;
    for (UIHandle child : children) {
        RemoveSubtree(child);
    }

    elements_.erase(id.Pack());
    std::erase(roots_, id);
}

bool LayoutTree::Remove(UIHandle id) {
    auto* element = Get(id);
    if (!element) return false;
    layoutDirty_ = true;

    const UIHandle parent = element->parent;
    RemoveSubtree(id);

    if (parent.IsValid()) {
        if (auto* p = Get(parent)) {
            std::erase(p->children, id);
        }
    }

    return true;
}

LayoutElement* LayoutTree::Get(UIHandle id) {
    auto it = elements_.find(id.Pack());
    return (it == elements_.end()) ? nullptr : &it->second;
}

const LayoutElement* LayoutTree::Get(UIHandle id) const {
    auto it = elements_.find(id.Pack());
    return (it == elements_.end()) ? nullptr : &it->second;
}

bool LayoutTree::SetStyle(UIHandle id, FlexItem style) {
    auto* element = Get(id);
    if (!element) return false;
    layoutDirty_ = true;
    element->node.style = style;
    return true;
}

bool LayoutTree::SetVisible(UIHandle id, bool visible) {
    auto* element = Get(id);
    if (!element) return false;
    layoutDirty_ = true;
    element->visible = visible;
    return true;
}

LayoutNode LayoutTree::Build(UIHandle id) {
    const auto* element = Get(id);
    if (!element) return {};

    LayoutNode node;
    node.style = element->node.style;

    for (UIHandle child : element->children) {
        const auto* childElement = Get(child);
        if (childElement && childElement->visible) {
            node.children.push_back(Build(child));
        }
    }

    return node;
}

void LayoutTree::Commit(UIHandle id, const LayoutNode& node) {
    auto* element = Get(id);
    if (!element) return;

    element->node.rect = node.rect;
    element->node.measured = node.measured;

    std::size_t layoutChild = 0;
    for (UIHandle child : element->children) {
        const auto* childElement = Get(child);
        if (!childElement || !childElement->visible) continue;

        if (layoutChild < node.children.size()) {
            Commit(child, node.children[layoutChild]);
        }
        ++layoutChild;
    }
}

void LayoutTree::Layout(Rect viewport, LayoutStats* stats) {
    const bool sameViewport = viewport.position.x == laidOutViewport_.position.x &&
                              viewport.position.y == laidOutViewport_.position.y &&
                              viewport.size.x == laidOutViewport_.size.x && viewport.size.y == laidOutViewport_.size.y;
    if (!layoutDirty_ && sameViewport) return;   // nada mudou: o resultado anterior ainda vale
    layoutDirty_ = false;
    laidOutViewport_ = viewport;

    LayoutEngine engine;
    for (UIHandle root : roots_) {
        auto* element = Get(root);
        if (!element || !element->visible) continue;

        LayoutNode node = Build(root);
        (void)engine.Measure(node, viewport.size, stats);
        engine.Arrange(node, viewport, stats);
        Commit(root, node);
    }
}

bool LayoutTree::BringToFront(UIHandle id) {
    LayoutElement* element = Get(id);
    if (!element) return false;
    element->stackOrder = ++stackCounter_;
    return true;
}

namespace {

// Ordena uma lista de irmaos pela ordem de empilhamento (estavel: empates ficam na ordem de layout).
std::vector<UIHandle> ByStackOrder(const std::vector<UIHandle>& handles, const auto& lookup) {
    std::vector<UIHandle> out = handles;
    std::stable_sort(out.begin(), out.end(), [&](UIHandle a, UIHandle b) {
        const LayoutElement* ea = lookup(a);
        const LayoutElement* eb = lookup(b);
        return (ea ? ea->stackOrder : 0) < (eb ? eb->stackOrder : 0);
    });
    return out;
}

} // namespace

std::vector<UIHandle> LayoutTree::PaintChildren(UIHandle id) const {
    const LayoutElement* element = Get(id);
    if (!element) return {};
    return ByStackOrder(element->children, [this](UIHandle h) { return Get(h); });
}

std::vector<UIHandle> LayoutTree::PaintRoots() const {
    return ByStackOrder(roots_, [this](UIHandle h) { return Get(h); });
}

} // namespace ToolEngine::SpriteUi
