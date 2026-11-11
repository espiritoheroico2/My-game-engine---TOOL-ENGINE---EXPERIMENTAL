#pragma once

#include <cstdint>

namespace ToolEngine::SpriteUi::Rendering {

// Bits of SpriteInstanceGPU::resourceIds[3] / UIRenderItem::flags.
// Keep in sync with shaders/SpriteUI.hlsl (checked by a contract test).
inline constexpr std::uint32_t FlagMsdfText = 0x2u;     // Sample an MSDF atlas page as text coverage.
inline constexpr std::uint32_t FlagRoundedPanel = 0x4u; // Analytic SDF rounded rectangle (see PanelSdf.hpp).
inline constexpr std::uint32_t FlagBitmapText = 0x8u;   // Sample the red channel of a grayscale glyph atlas.

} // namespace ToolEngine::SpriteUi::Rendering
