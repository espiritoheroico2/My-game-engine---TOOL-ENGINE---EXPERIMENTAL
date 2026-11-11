#pragma once

#include "SpriteUi/Core/Handle.hpp"
#include "SpriteUi/Core/Transform2D.hpp"
#include "SpriteUi/Input/InputRouter.hpp"
#include "SpriteUi/Layout/LayoutTree.hpp"
#include "SpriteUi/Rendering/DrawList.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace ToolEngine::SpriteUi {

struct ElementVisual {
    Vec4 tintPremultipliedLinear{1.0f, 1.0f, 1.0f, 1.0f};
    float uvRect[4]{0.0f, 0.0f, 1.0f, 1.0f};
    std::uint32_t textureSlice = Rendering::WhiteTextureSlice;
    std::uint32_t materialIndex = 0;
    std::uint32_t flags = 0;
    float zOrder = 0.0f;
    bool clipOverflow = false;
    bool interactable = true;
    bool focusable = false;
    bool blocksInput = true;
};

struct SceneExtractionResult {
    std::vector<Rendering::UIRenderItem> renderItems;
    std::vector<Input::HitTarget> hitTargets;
    Rendering::ClipHierarchyResult clipHierarchy;
};

// Generates render items, hit targets, and hierarchical clip nodes automatically from LayoutTree.
class LayoutSceneBridge {
public:
    void SetVisual(UIHandle handle, const ElementVisual& visual);
    void RemoveVisual(UIHandle handle);
    [[nodiscard]] const ElementVisual* GetVisual(UIHandle handle) const;

    [[nodiscard]] SceneExtractionResult Extract(const LayoutTree& tree) const;

private:
    std::unordered_map<std::uint64_t, ElementVisual> visuals_;
};

} // namespace ToolEngine::SpriteUi

