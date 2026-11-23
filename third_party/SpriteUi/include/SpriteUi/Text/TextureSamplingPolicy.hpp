#pragma once

#include <cstdint>

namespace ToolEngine::SpriteUi::Text {

// Sampler requerido pelo conteudo da textura. A escolha e parte do contrato do atlas;
// cada backend deve traduzir a politica para o sampler nativo, sem assumir um sampler global.
enum class TextureKind : std::uint8_t {
    Color,
    PixelArt,
    BitmapCoverage,
    Sdf,
    Msdf,
    Mtsdf,
    RenderTarget,
};

enum class SamplerPolicy : std::uint8_t {
    Nearest,
    Linear,
    LinearMipmapped,
    SdfLinear,
    MsdfLinear,
};

[[nodiscard]] constexpr SamplerPolicy SamplerPolicyFor(TextureKind kind) noexcept {
    switch (kind) {
    case TextureKind::PixelArt:       return SamplerPolicy::Nearest;
    case TextureKind::Color:          return SamplerPolicy::LinearMipmapped;
    case TextureKind::BitmapCoverage: return SamplerPolicy::Linear;
    case TextureKind::Sdf:            return SamplerPolicy::SdfLinear;
    case TextureKind::Msdf:
    case TextureKind::Mtsdf:          return SamplerPolicy::MsdfLinear;
    case TextureKind::RenderTarget:   return SamplerPolicy::Linear;
    }
    return SamplerPolicy::Linear;
}

} // namespace ToolEngine::SpriteUi::Text
