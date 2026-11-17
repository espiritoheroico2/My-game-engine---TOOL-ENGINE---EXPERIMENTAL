#pragma once

#include "SpriteUi/Core/DrawOrder.hpp"
#include "SpriteUi/Core/Handle.hpp"
#include "SpriteUi/Core/Transform2D.hpp"
#include "SpriteUi/Input/InputRouter.hpp"
#include "SpriteUi/Layout/LayoutTree.hpp"
#include "SpriteUi/Rendering/DrawList.hpp"

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace ToolEngine::SpriteUi {

struct ElementVisual {
    Vec4 tintPremultipliedLinear{1.0f, 1.0f, 1.0f, 1.0f};
    float uvRect[4]{0.0f, 0.0f, 1.0f, 1.0f};
    std::uint32_t textureSlice = Rendering::WhiteTextureSlice;
    std::uint32_t materialIndex = 0;
    std::uint32_t flags = 0;
    // Camada minima do elemento. A camada efetiva e a maior entre a sua e a do pai, e o elemento de arrasto vai
    // para UILayer::DragPreview. zOrder e um deslocamento relativo ao pai (ver Core/DrawOrder.hpp).
    std::int32_t layer = static_cast<std::int32_t>(UILayer::Background);
    float zOrder = 0.0f;
    bool clipOverflow = false;
    bool interactable = true;
    // Elemento desativado: cobre o ponteiro (nao deixa passar o evento para o que esta por baixo), mas nao recebe
    // nada. Vale para o acerto; o desenho segue o estado do proprio elemento.
    bool enabled = true;
    bool focusable = false;
    bool blocksInput = true;
};

struct SceneExtractionResult {
    std::vector<Rendering::UIRenderItem> renderItems;
    std::vector<Input::HitTarget> hitTargets;
    Rendering::ClipHierarchyResult clipHierarchy;
    // Chave de ordem efetiva de cada elemento visivel. Um texto gerado pelo host para um elemento deve usar esta
    // chave (camada, zOrder e paintOrder do dono), senao ele pode aparecer por baixo de outra janela.
    std::unordered_map<std::uint64_t, DrawOrderKey> orders;
    // Itens de desenho que nao foram emitidos porque o recorte deixa a area toda de fora (culling). O acerto de
    // ponteiro continua igual: quem e descartado do desenho nao muda a ordem de quem sobra.
    std::uint32_t culledRenderItems = 0;

    [[nodiscard]] std::optional<DrawOrderKey> OrderOf(UIHandle handle) const;
};

// Generates render items, hit targets, and hierarchical clip nodes automatically from LayoutTree.
class LayoutSceneBridge {
public:
    void SetVisual(UIHandle handle, const ElementVisual& visual);
    void RemoveVisual(UIHandle handle);
    [[nodiscard]] const ElementVisual* GetVisual(UIHandle handle) const;

    // Elemento sendo arrastado: a subarvore de root vai para a camada DragPreview (por cima de todas).
    // ClearDragRoot desfaz. Nao muda a arvore nem os visuais.
    void SetDragRoot(UIHandle root) noexcept { dragRoot_ = root; }
    void ClearDragRoot() noexcept { dragRoot_ = UIHandle{}; }

    [[nodiscard]] SceneExtractionResult Extract(const LayoutTree& tree) const;

private:
    UIHandle dragRoot_{};
    std::unordered_map<std::uint64_t, ElementVisual> visuals_;
};

} // namespace ToolEngine::SpriteUi

