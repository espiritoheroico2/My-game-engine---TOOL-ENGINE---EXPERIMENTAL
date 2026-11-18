#include "SpriteUi/Layout/LayoutBridge.hpp"

namespace ToolEngine::SpriteUi {

void LayoutSceneBridge::SetVisual(UIHandle handle, const ElementVisual& visual) {
    if (handle.IsValid()) {
        visuals_[handle.Pack()] = visual;
    }
}

void LayoutSceneBridge::RemoveVisual(UIHandle handle) {
    if (handle.IsValid()) {
        visuals_.erase(handle.Pack());
    }
}

const ElementVisual* LayoutSceneBridge::GetVisual(UIHandle handle) const {
    if (!handle.IsValid()) return nullptr;
    auto it = visuals_.find(handle.Pack());
    return it == visuals_.end() ? nullptr : &it->second;
}

namespace {

struct TraversalState {
    std::uint64_t paintCounter = 0;
    std::vector<Rendering::ClipNode> clipNodes;
    std::unordered_map<std::uint64_t, std::uint32_t> elementClipNodeIndices;
    std::vector<std::pair<UIHandle, std::uint32_t>> elementInheritedClip;
};

void CollectClipsAndPaint(
    UIHandle id,
    const LayoutTree& tree,
    const LayoutSceneBridge& bridge,
    std::uint32_t parentClipNodeIdx,
    TraversalState& state)
{
    const auto* elem = tree.Get(id);
    if (!elem || !elem->visible) return;

    const auto* visual = bridge.GetVisual(id);
    std::uint32_t currentClipNodeIdx = parentClipNodeIdx;

    if (visual && visual->clipOverflow && elem->node.rect.size.x > 0.0f && elem->node.rect.size.y > 0.0f) {
        Rendering::ClipNode node;
        node.rect.minX = elem->node.rect.position.x;
        node.rect.minY = elem->node.rect.position.y;
        node.rect.maxX = elem->node.rect.position.x + elem->node.rect.size.x;
        node.rect.maxY = elem->node.rect.position.y + elem->node.rect.size.y;
        node.parentIndex = parentClipNodeIdx;
        node.enabled = true;

        const std::uint32_t nodeIdx = static_cast<std::uint32_t>(state.clipNodes.size());
        state.clipNodes.push_back(node);
        state.elementClipNodeIndices[id.Pack()] = nodeIdx;
        currentClipNodeIdx = nodeIdx;
    }

    state.elementInheritedClip.push_back({id, currentClipNodeIdx});

    for (UIHandle child : elem->children) {
        CollectClipsAndPaint(child, tree, bridge, currentClipNodeIdx, state);
    }
}

} // namespace

SceneExtractionResult LayoutSceneBridge::Extract(const LayoutTree& tree) const {
    SceneExtractionResult result;

    TraversalState state;
    for (UIHandle root : tree.Roots()) {
        CollectClipsAndPaint(root, tree, *this, Rendering::NoClipIndex, state);
    }

    result.clipHierarchy = Rendering::BuildClipHierarchy(state.clipNodes);

    std::unordered_map<std::uint64_t, std::uint32_t> elementEffectiveClipIndex;
    for (const auto& [handle, clipNodeIdx] : state.elementInheritedClip) {
        if (clipNodeIdx != Rendering::NoClipIndex && clipNodeIdx < result.clipHierarchy.nodeClipIndices.size()) {
            elementEffectiveClipIndex[handle.Pack()] = result.clipHierarchy.nodeClipIndices[clipNodeIdx];
        }
    }

    // Traverse again to emit HitTargets and RenderItems in paint order
    std::uint64_t paintCounter = 0;

    auto emitElement = [&](auto self, UIHandle id, UIHandle hitAncestor) -> void {
        const auto* elem = tree.Get(id);
        if (!elem || !elem->visible) return;

        const auto* visual = GetVisual(id);
        const std::uint64_t currentPaint = ++paintCounter;

        std::uint32_t effectiveClip = Rendering::NoClipIndex;
        auto clipIt = elementEffectiveClipIndex.find(id.Pack());
        if (clipIt != elementEffectiveClipIndex.end()) {
            effectiveClip = clipIt->second;
        }

        // HitTarget
        const bool emitsTarget = visual == nullptr || visual->interactable;
        if (emitsTarget) {
            Input::HitTarget target;
            target.handle = id;
            // Ancestral mais proximo que tambem possui HitTarget: o bubbling nao quebra
            // quando um pai intermediario e nao interagivel.
            target.parent = hitAncestor;
            target.localSize = elem->node.rect.size;
            target.localToScreen.tx = elem->node.rect.position.x;
            target.localToScreen.ty = elem->node.rect.position.y;
            target.paintOrder = currentPaint;
            target.visible = elem->visible;
            target.enabled = true;
            target.blocksInput = visual ? visual->blocksInput : true;
            target.focusable = visual ? visual->focusable : false;

            if (effectiveClip != Rendering::NoClipIndex && effectiveClip < result.clipHierarchy.effectiveRects.size()) {
                const auto& cr = result.clipHierarchy.effectiveRects[effectiveClip];
                target.clipEnabled = true;
                target.clipRectLogical = {{cr.minX, cr.minY}, {cr.maxX - cr.minX, cr.maxY - cr.minY}};
            }

            result.hitTargets.push_back(target);
        }

        // RenderItem
        if (visual && elem->node.rect.size.x > 0.0f && elem->node.rect.size.y > 0.0f) {
            Rendering::UIRenderItem item;
            item.handle = id;
            item.layout.resolvedPosition = elem->node.rect.position;
            item.layout.resolvedSize = elem->node.rect.size;
            item.layout.visible = elem->visible;

            item.transform.anchorMin = {0.0f, 0.0f};
            item.transform.anchorMax = {0.0f, 0.0f};
            item.transform.offsetMin = {0.0f, 0.0f};
            item.transform.offsetMax = elem->node.rect.size;
            item.transform.pivot = {0.5f, 0.5f};
            item.transform.scale = {1.0f, 1.0f};

            for (int k = 0; k < 4; ++k) item.uvRect[k] = visual->uvRect[k];
            item.tintPremultipliedLinear = visual->tintPremultipliedLinear;
            item.textureSlice = visual->textureSlice;
            item.materialIndex = visual->materialIndex;
            item.clipIndex = effectiveClip;
            item.flags = visual->flags;
            item.zOrder = visual->zOrder;
            item.paintOrder = currentPaint;
            item.visible = elem->visible;

            result.renderItems.push_back(item);
        }

        for (UIHandle child : elem->children) {
            self(self, child, emitsTarget ? id : hitAncestor);
        }
    };

    for (UIHandle root : tree.Roots()) {
        emitElement(emitElement, root, InvalidUIHandle);
    }

    return result;
}

} // namespace ToolEngine::SpriteUi

