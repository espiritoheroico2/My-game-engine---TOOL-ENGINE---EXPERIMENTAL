#pragma once

#include <cstdint>

namespace ToolEngine::SpriteUi::Rendering {

enum class UICanvasSpace : std::uint8_t { Screen, World };
enum class UIShadingMode : std::uint8_t { Unlit, ClusterLit };

struct UICanvasRenderConfig {
    UICanvasSpace space = UICanvasSpace::Screen;
    UIShadingMode shading = UIShadingMode::Unlit;
    std::uint32_t lightLayerMask = 0xFFFFFFFFu;
    bool castsWorldLight = false;

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        const bool knownSpace = space == UICanvasSpace::Screen || space == UICanvasSpace::World;
        const bool knownShading = shading == UIShadingMode::Unlit ||
                                  shading == UIShadingMode::ClusterLit;
        if (!knownSpace || !knownShading) return false;
        if (space == UICanvasSpace::Screen) {
            return shading == UIShadingMode::Unlit && !castsWorldLight;
        }
        return true;
    }
};

} // namespace ToolEngine::SpriteUi::Rendering
