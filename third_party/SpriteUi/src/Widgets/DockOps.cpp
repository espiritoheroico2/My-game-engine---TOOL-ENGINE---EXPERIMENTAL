#include "SpriteUi/Widgets/DockOps.hpp"

#include <memory>
#include <utility>

namespace ToolEngine::SpriteUi::Widgets {

namespace {

bool LeafHolds(const UIDockNode& node, UIHandle window) noexcept {
    if (node.content == window) return true;
    for (const UITab& tab : node.tabs.GetTabs()) {
        if (tab.contentHandle == window) return true;
    }
    return false;
}

bool IsEmptyLeaf(const UIDockNode& node) noexcept {
    return !node.isSplit && !node.content.IsValid() && node.tabs.TabCount() == 0;
}

// Substitui um no dividido por um dos filhos quando o outro ficou vazio. Ambos vazios: vira folha vazia.
void CollapseEmpty(UIDockNode& node) noexcept {
    if (!node.isSplit) return;
    if (node.firstChild) CollapseEmpty(*node.firstChild);
    if (node.secondChild) CollapseEmpty(*node.secondChild);
    if (!node.firstChild || !node.secondChild) return;

    const bool firstEmpty = IsEmptyLeaf(*node.firstChild);
    const bool secondEmpty = IsEmptyLeaf(*node.secondChild);
    if (!firstEmpty && !secondEmpty) return;

    if (firstEmpty && secondEmpty) {
        node.isSplit = false;
        node.firstChild.reset();
        node.secondChild.reset();
        node.content = InvalidUIHandle;
        node.tabs = UITabGroupComponent{};
        return;
    }

    // Um filho ainda tem conteudo: o pai assume o lugar dele, com seus filhos e proporcao.
    std::unique_ptr<UIDockNode> keep = std::move(firstEmpty ? node.secondChild : node.firstChild);
    node.isSplit = keep->isSplit;
    node.content = keep->content;
    node.tabs = std::move(keep->tabs);
    node.splitDirection = keep->splitDirection;
    node.splitRatio = keep->splitRatio;
    node.firstChild = std::move(keep->firstChild);
    node.secondChild = std::move(keep->secondChild);
}

} // namespace

UIDockNode* FindLeafContaining(UIDockNode& root, UIHandle content) noexcept {
    if (root.isSplit) {
        if (root.firstChild) {
            if (UIDockNode* found = FindLeafContaining(*root.firstChild, content)) return found;
        }
        if (root.secondChild) return FindLeafContaining(*root.secondChild, content);
        return nullptr;
    }
    return LeafHolds(root, content) ? &root : nullptr;
}

bool RemoveFromDock(UIDockNode& root, UIHandle window) noexcept {
    UIDockNode* leaf = FindLeafContaining(root, window);
    if (!leaf) return false;

    if (leaf->tabs.TabCount() > 0) {
        const auto& tabs = leaf->tabs.GetTabs();
        std::size_t index = tabs.size();
        for (std::size_t i = 0; i < tabs.size(); ++i) {
            if (tabs[i].contentHandle == window) { index = i; break; }
        }
        if (index == tabs.size() || !leaf->tabs.RemoveTab(index)) return false;
        if (leaf->tabs.TabCount() == 1) {
            // Uma aba so: volta a ser conteudo unico, sem grupo de abas.
            leaf->content = leaf->tabs.GetTabs()[0].contentHandle;
            leaf->tabs = UITabGroupComponent{};
        }
    } else {
        leaf->content = InvalidUIHandle;
    }

    CollapseEmpty(root);
    return true;
}

bool ApplyDropResult(UIDockNode& root, UIHandle window, const DockDropResult& drop, float ratio) noexcept {
    switch (drop.action) {
    case DockAction::None:
        return false;

    case DockAction::Float:
        return RemoveFromDock(root, window);

    case DockAction::Dock:
    case DockAction::Tab: {
        if (!drop.targetLeaf.IsValid()) return false;
        if (drop.action == DockAction::Dock && drop.slot == DockSlot::None) return false;   // nada a encaixar
        UIDockNode* target = FindLeafContaining(root, drop.targetLeaf);
        if (!target || LeafHolds(*target, window)) return false;   // alvo inexistente ou o proprio grupo

        // Remover a janela pode colapsar o alvo. Por isso o alvo e buscado de novo pelo conteudo.
        (void)RemoveFromDock(root, window);
        target = FindLeafContaining(root, drop.targetLeaf);
        if (!target) return false;

        if (drop.action == DockAction::Tab) return target->Split(DockSlot::Center, ratio, window);
        return target->Split(drop.slot, ratio, window);
    }
    }
    return false;
}

} // namespace ToolEngine::SpriteUi::Widgets
