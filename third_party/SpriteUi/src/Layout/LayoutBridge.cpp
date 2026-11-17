#include "SpriteUi/Layout/LayoutBridge.hpp"

#include <algorithm>

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

// Verdadeiro quando o retangulo nao tem nenhuma area dentro do recorte (todo de fora).
bool FullyClipped(Rect rect, const Rendering::ClipRectGPU& clip) noexcept {
    const float x0 = std::max(rect.position.x, clip.minX);
    const float y0 = std::max(rect.position.y, clip.minY);
    const float x1 = std::min(rect.position.x + rect.size.x, clip.maxX);
    const float y1 = std::min(rect.position.y + rect.size.y, clip.maxY);
    return x1 <= x0 || y1 <= y0;
}

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

    for (UIHandle child : tree.PaintChildren(id)) {
        CollectClipsAndPaint(child, tree, bridge, currentClipNodeIdx, state);
    }
}

} // namespace

SceneExtractionResult LayoutSceneBridge::Extract(const LayoutTree& tree) const {
    SceneExtractionResult result;

    TraversalState state;
    for (UIHandle root : tree.PaintRoots()) {
        CollectClipsAndPaint(root, tree, *this, Rendering::NoClipIndex, state);
    }

    result.clipHierarchy = Rendering::BuildClipHierarchy(state.clipNodes);

    std::unordered_map<std::uint64_t, std::uint32_t> elementEffectiveClipIndex;
    for (const auto& [handle, clipNodeIdx] : state.elementInheritedClip) {
        if (clipNodeIdx != Rendering::NoClipIndex && clipNodeIdx < result.clipHierarchy.nodeClipIndices.size()) {
            elementEffectiveClipIndex[handle.Pack()] = result.clipHierarchy.nodeClipIndices[clipNodeIdx];
        }
    }

    // Segunda passada: caixas de acerto e itens de desenho, na ordem de pintura. A camada e o zOrder efetivos
    // passam do pai para o filho (camada: a maior; zOrder: a soma), assim a janela inteira sobe junta.
    std::uint64_t paintCounter = 0;

    auto emitElement = [&](auto self, UIHandle id, UIHandle hitAncestor, std::int32_t parentLayer,
                           float parentZ, bool inDrag, bool disabledAncestor) -> void {
        const auto* elem = tree.Get(id);
        if (!elem || !elem->visible) return;

        const auto* visual = GetVisual(id);
        const std::uint64_t currentPaint = ++paintCounter;

        const bool dragging = inDrag || (dragRoot_.IsValid() && id == dragRoot_);
        std::int32_t layer = std::max(visual ? visual->layer : parentLayer, parentLayer);
        if (dragging) layer = std::max(layer, static_cast<std::int32_t>(UILayer::DragPreview));
        const float zOrder = parentZ + (visual ? visual->zOrder : 0.0f);

        result.orders[id.Pack()] = DrawOrderKey{layer, zOrder, currentPaint, id.Pack()};

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
            target.layer = layer;
            target.zOrder = zOrder;
            target.visible = elem->visible;
            // Filho de um elemento desativado tambem fica desativado.
            target.enabled = !disabledAncestor && (visual ? visual->enabled : true);
            target.blocksInput = visual ? visual->blocksInput : true;
            target.focusable = visual ? visual->focusable : false;

            if (effectiveClip != Rendering::NoClipIndex && effectiveClip < result.clipHierarchy.effectiveRects.size()) {
                const auto& cr = result.clipHierarchy.effectiveRects[effectiveClip];
                target.clipEnabled = true;
                target.clipRectLogical = {{cr.minX, cr.minY}, {cr.maxX - cr.minX, cr.maxY - cr.minY}};
            }

            result.hitTargets.push_back(target);
        }

        // RenderItem. Elemento todo fora do recorte nao gera item de desenho (culling); o acerto nao muda.
        const bool culled = effectiveClip != Rendering::NoClipIndex &&
                            effectiveClip < result.clipHierarchy.effectiveRects.size() &&
                            FullyClipped(elem->node.rect, result.clipHierarchy.effectiveRects[effectiveClip]);
        if (culled && visual) ++result.culledRenderItems;
        if (visual && !culled && elem->node.rect.size.x > 0.0f && elem->node.rect.size.y > 0.0f) {
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
            item.layer = layer;
            item.zOrder = zOrder;
            item.paintOrder = currentPaint;
            item.visible = elem->visible;

            result.renderItems.push_back(item);
        }

        const bool disabledHere = disabledAncestor || (visual && !visual->enabled);
        for (UIHandle child : tree.PaintChildren(id)) {
            self(self, child, emitsTarget ? id : hitAncestor, layer, zOrder, dragging, disabledHere);
        }
    };

    for (UIHandle root : tree.PaintRoots()) {
        emitElement(emitElement, root, InvalidUIHandle, static_cast<std::int32_t>(UILayer::Background), 0.0f,
                    false, false);
    }

    return result;
}

std::optional<DrawOrderKey> SceneExtractionResult::OrderOf(UIHandle handle) const {
    const auto it = orders.find(handle.Pack());
    if (it == orders.end()) return std::nullopt;
    return it->second;
}

} // namespace ToolEngine::SpriteUi

