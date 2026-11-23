#pragma once

// Metadado de renderizacao de um atlas de fonte: em que formato o atlas foi gerado.
//
// Regra: a flag do shader vem do formato REAL do recurso, nunca do tipo abstrato "fonte". Um atlas de cobertura
// (BitmapCoverage) nunca vira FlagMsdfText: o shader MSDF le RGB e a mediana dos canais, e um atlas de cobertura
// so tem o canal R. Exemplo de jogo: a fonte de HUD e um bitmap de 48 px; o menu de titulo usa um MSDF gerado
// offline. Cada um carrega o proprio metadado, e o chamador pede a flag a partir dele.

#include "SpriteUi/Rendering/RenderFlags.hpp"
#include "SpriteUi/Text/TextureSamplingPolicy.hpp"

#include <cstdint>
#include <optional>

namespace ToolEngine::SpriteUi::Text {

enum class TextRenderMode : std::uint8_t {
    BitmapCoverage, // Cobertura 8 bits por pixel (BuildTruetypeAtlas). Implementado: FlagBitmapText.
    Sdf,            // Distancia assinada de um canal. Sem caminho de shader nesta versao (fase 3).
    Msdf,           // Distancia multicanal (RGB). O shader existe; o gerador MSDF ainda nao (fase 3).
    Mtsdf,          // MSDF com distancia no alfa. Sem caminho de shader nesta versao (fase 3).
    Color,          // Glifos coloridos (emoji). Fora do escopo atual.
};

struct FontRenderMetadata {
    TextRenderMode mode = TextRenderMode::BitmapCoverage;
    float pxRange = 0.0f;                   // Largura da faixa de distancia em pixels (SDF/MSDF). Zero no bitmap.
    std::uint32_t rasterPixelSize = 0;      // Tamanho do em usado ao gerar o atlas (0 = desconhecido).
    SamplerPolicy sampler = SamplerPolicyFor(TextureKind::BitmapCoverage);
};

// Flag do shader para um modo de atlas. Sem valor (nullopt) quando o modo nao tem caminho de shader nesta versao:
// o chamador deve tratar isso como erro de configuracao, nao cair em outra flag em silencio.
[[nodiscard]] constexpr std::optional<std::uint32_t> TextShaderFlagFor(TextRenderMode mode) noexcept {
    switch (mode) {
    case TextRenderMode::BitmapCoverage: return Rendering::FlagBitmapText;
    case TextRenderMode::Msdf:           return Rendering::FlagMsdfText;
    case TextRenderMode::Sdf:
    case TextRenderMode::Mtsdf:
    case TextRenderMode::Color:          return std::nullopt;
    }
    return std::nullopt;
}

[[nodiscard]] constexpr std::optional<std::uint32_t> TextShaderFlagFor(const FontRenderMetadata& metadata) noexcept {
    return TextShaderFlagFor(metadata.mode);
}

} // namespace ToolEngine::SpriteUi::Text
