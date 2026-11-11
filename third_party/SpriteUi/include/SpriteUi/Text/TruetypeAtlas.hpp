#pragma once

// Atlas de glifos com stb_truetype (opcional: target SpriteUi::TextStb, ativado por SPRITEUI_WITH_TEXT_STB).
//
// Limites deste primeiro passo, de proposito:
//  - Sem shaping complexo: um glifo por codepoint, sem kerning, ligaduras ou marcas combinantes.
//  - Atlas de rasterizacao (alfa 8 bits), nao SDF/MSDF. Renderizar muito menor ou maior que
//    rasterPixelSize fica serrilhado. SDF fica para a proxima etapa.
//  - Um atlas, uma pagina. Sem atlas dinamico.
//
// Convencoes iguais as do DebugShaper: xAdvance e planeBounds em unidades de fonte com
// metrics.unitsPerEm = 1000; planeBounds = [x0, y0, x1, y1] com y para cima a partir da linha de base;
// uvRect = [u0, v0, u1, v1] normalizado no atlas.

#include "SpriteUi/Text/FontMetrics.hpp"
#include "SpriteUi/Text/GlyphInstance.hpp"

#include <cstdint>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ToolEngine::SpriteUi::Text {

struct TruetypeAtlasConfig {
    float rasterPixelSize = 48.0f;                    // Tamanho em pixels do em usado na rasterizacao.
    std::uint32_t atlasWidth = 512;
    std::uint32_t atlasHeight = 512;
    std::uint32_t firstCodepoint = 0x20;              // ASCII imprimivel por padrao.
    std::uint32_t lastCodepoint = 0x7E;
    std::uint32_t padding = 1;                        // Pixels vazios entre glifos.
};

struct TruetypeGlyph {
    std::uint32_t glyphId = 0;
    float advance = 0.0f;                             // Em unidades de fonte (unitsPerEm = 1000).
    float planeBounds[4]{};
    float uvRect[4]{};
};

struct TruetypeAtlas {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> pixels;                 // Alfa 8 bits, width x height, linha por linha.
    FontMetrics metrics;                              // unitsPerEm = 1000; ascent/descent/lineGap nessa escala.
    std::unordered_map<std::uint32_t, TruetypeGlyph> glyphs;   // Chave: codepoint Unicode.
    std::uint32_t fallbackCodepoint = '?';            // Usado para codepoints fora do atlas.

    [[nodiscard]] bool IsValid() const noexcept;
};

// Monta o atlas a partir dos bytes de um TTF/OTF. Retorna false se a fonte for invalida, a configuracao
// for absurda ou os glifos nao couberem no atlas (nesse caso out fica vazio).
[[nodiscard]] bool BuildTruetypeAtlas(
    std::span<const std::uint8_t> fontBytes,
    const TruetypeAtlasConfig& config,
    TruetypeAtlas& out);

// Converte UTF-8 em um GlyphInstance por codepoint, na ordem do texto. clusterByteOffset e o offset
// do byte inicial do codepoint. Bytes UTF-8 invalidos e codepoints fora do atlas viram fallbackCodepoint.
// Tab avanca 4 espacos; controles (como '\n') ocupam um glifo sem area e sem avanco, como no DebugShaper.
[[nodiscard]] std::vector<GlyphInstance> ShapeTruetype(
    std::string_view utf8,
    const TruetypeAtlas& atlas);

} // namespace ToolEngine::SpriteUi::Text
