#include "SpriteUi/Text/TruetypeAtlas.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

// stb_truetype so e compilado aqui, num unico arquivo, para que o resto da biblioteca nao veja a dependencia.
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

namespace ToolEngine::SpriteUi::Text {

namespace {

constexpr float kUnitsPerEm = 1000.0f;
constexpr std::uint32_t kTabSpaces = 4;

// Decodifica um codepoint UTF-8 em `pos`. Retorna o codepoint e o numero de bytes consumidos.
// Sequencias invalidas viram U+FFFD consumindo 1 byte, para nunca travar nem sair do texto.
struct DecodedCodepoint {
    std::uint32_t codepoint = 0xFFFD;
    std::size_t length = 1;
};

DecodedCodepoint DecodeUtf8(std::string_view text, std::size_t pos) {
    const auto lead = static_cast<std::uint8_t>(text[pos]);
    std::size_t length = 0;
    std::uint32_t value = 0;
    std::uint32_t minValue = 0;
    if (lead < 0x80) return {lead, 1};
    if ((lead & 0xE0) == 0xC0) { length = 2; value = lead & 0x1F; minValue = 0x80; }
    else if ((lead & 0xF0) == 0xE0) { length = 3; value = lead & 0x0F; minValue = 0x800; }
    else if ((lead & 0xF8) == 0xF0) { length = 4; value = lead & 0x07; minValue = 0x10000; }
    else return {0xFFFD, 1};
    if (pos + length > text.size()) return {0xFFFD, 1};
    for (std::size_t i = 1; i < length; ++i) {
        const auto cont = static_cast<std::uint8_t>(text[pos + i]);
        if ((cont & 0xC0) != 0x80) return {0xFFFD, 1};
        value = (value << 6) | (cont & 0x3F);
    }
    if (value < minValue || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) return {0xFFFD, 1};
    return {value, length};
}

bool IsControl(std::uint32_t codepoint) {
    return codepoint < 0x20 || codepoint == 0x7F;
}

} // namespace

bool TruetypeAtlas::IsValid() const noexcept {
    if (!metrics.IsValid() || width == 0 || height == 0) return false;
    if (pixels.size() != static_cast<std::size_t>(width) * height) return false;
    return !glyphs.empty();
}

bool BuildTruetypeAtlas(
    std::span<const std::uint8_t> fontBytes,
    const TruetypeAtlasConfig& config,
    TruetypeAtlas& out) {
    out = TruetypeAtlas{};
    if (fontBytes.empty() || config.rasterPixelSize <= 0.0f || !std::isfinite(config.rasterPixelSize)) return false;
    if (config.firstCodepoint > config.lastCodepoint || config.lastCodepoint > 0x10FFFF) return false;
    if (config.atlasWidth == 0 || config.atlasHeight == 0 || config.atlasWidth > 8192 || config.atlasHeight > 8192) return false;

    // stb_truetype nao valida o tamanho nem o cabecalho: sem estas checagens, bytes que nao sao fonte
    // leem fora do buffer. O cabecalho de tag tem 4 bytes e o offset da tabela de diretorio mais 12.
    constexpr std::size_t kMinFontBytes = 12;
    if (fontBytes.size() < kMinFontBytes) return false;

    stbtt_fontinfo info{};
    const unsigned char* data = fontBytes.data();
    const int fontOffset = stbtt_GetFontOffsetForIndex(data, 0);
    if (fontOffset < 0 || static_cast<std::size_t>(fontOffset) + kMinFontBytes > fontBytes.size()) return false;
    if (!stbtt_InitFont(&info, data, fontOffset)) return false;

    const float px = config.rasterPixelSize;
    const float scale = stbtt_ScaleForMappingEmToPixels(&info, px);
    const float toUnits = kUnitsPerEm / px;     // pixels no atlas -> unidades de fonte (1000 por em)

    int ascent = 0, descent = 0, lineGap = 0;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);

    out.width = config.atlasWidth;
    out.height = config.atlasHeight;
    out.pixels.assign(static_cast<std::size_t>(out.width) * out.height, 0);
    out.metrics.fontSize = kUnitsPerEm;
    out.metrics.unitsPerEm = kUnitsPerEm;
    out.metrics.ascent = ascent * scale * toUnits;
    out.metrics.descent = descent * scale * toUnits;
    out.metrics.lineGap = std::max(0.0f, lineGap * scale * toUnits);
    out.metrics.pxRange = 4.0f;
    out.metrics.atlasWidth = static_cast<float>(out.width);
    out.metrics.atlasHeight = static_cast<float>(out.height);

    const std::uint32_t pad = config.padding;
    std::uint32_t cursorX = pad, cursorY = pad, rowHeight = 0;
    for (std::uint32_t cp = config.firstCodepoint; cp <= config.lastCodepoint; ++cp) {
        const int glyphIndex = stbtt_FindGlyphIndex(&info, static_cast<int>(cp));
        TruetypeGlyph glyph;
        glyph.glyphId = static_cast<std::uint32_t>(glyphIndex);

        int advance = 0, leftBearing = 0;
        stbtt_GetGlyphHMetrics(&info, glyphIndex, &advance, &leftBearing);
        glyph.advance = advance * scale * toUnits;

        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        stbtt_GetGlyphBitmapBox(&info, glyphIndex, scale, scale, &x0, &y0, &x1, &y1);
        const auto w = static_cast<std::uint32_t>(std::max(0, x1 - x0));
        const auto h = static_cast<std::uint32_t>(std::max(0, y1 - y0));

        if (w > 0 && h > 0) {
            if (cursorX + w + pad > out.width) {          // Nova linha de prateleira.
                cursorX = pad;
                cursorY += rowHeight + pad;
                rowHeight = 0;
            }
            if (cursorY + h + pad > out.height) {         // Sem espaco: a configuracao e pequena demais.
                out = TruetypeAtlas{};
                return false;
            }
            stbtt_MakeGlyphBitmap(&info, out.pixels.data() + static_cast<std::size_t>(cursorY) * out.width + cursorX,
                                  static_cast<int>(w), static_cast<int>(h),
                                  static_cast<int>(out.width), scale, scale, glyphIndex);

            // Pixels com y para baixo a partir do topo; planeBounds usa y para cima a partir da linha de base.
            glyph.planeBounds[0] = x0 * toUnits;
            glyph.planeBounds[1] = -y1 * toUnits;
            glyph.planeBounds[2] = x1 * toUnits;
            glyph.planeBounds[3] = -y0 * toUnits;
            glyph.uvRect[0] = static_cast<float>(cursorX) / out.width;
            glyph.uvRect[1] = static_cast<float>(cursorY) / out.height;
            glyph.uvRect[2] = static_cast<float>(cursorX + w) / out.width;
            glyph.uvRect[3] = static_cast<float>(cursorY + h) / out.height;

            cursorX += w + pad;
            rowHeight = std::max(rowHeight, h);
        }
        out.glyphs.emplace(cp, glyph);
    }

    if (out.glyphs.empty()) {
        out = TruetypeAtlas{};
        return false;
    }
    // Garante um fallback: se '?' nao esta no intervalo, usa o primeiro codepoint disponivel.
    if (out.glyphs.find(out.fallbackCodepoint) == out.glyphs.end()) {
        out.fallbackCodepoint = config.firstCodepoint;
    }
    return true;
}

std::vector<GlyphInstance> ShapeTruetype(std::string_view utf8, const TruetypeAtlas& atlas) {
    std::vector<GlyphInstance> glyphs;
    glyphs.reserve(utf8.size());

    const auto fallbackIt = atlas.glyphs.find(atlas.fallbackCodepoint);
    const TruetypeGlyph* fallback = fallbackIt != atlas.glyphs.end() ? &fallbackIt->second : nullptr;

    std::size_t pos = 0;
    while (pos < utf8.size()) {
        const DecodedCodepoint decoded = DecodeUtf8(utf8, pos);
        GlyphInstance instance;
        instance.clusterByteOffset = static_cast<std::uint32_t>(pos);

        if (decoded.codepoint == '\t') {
            const auto spaceIt = atlas.glyphs.find(' ');
            const float spaceAdvance = spaceIt != atlas.glyphs.end() ? spaceIt->second.advance : 0.0f;
            instance.glyphId = spaceIt != atlas.glyphs.end() ? spaceIt->second.glyphId : 0;
            instance.xAdvance = spaceAdvance * kTabSpaces;
        } else if (IsControl(decoded.codepoint)) {
            instance.glyphId = 0;                              // Controle: ocupa um glifo sem area nem avanco.
            instance.xAdvance = 0.0f;
        } else {
            const auto it = atlas.glyphs.find(decoded.codepoint);
            const TruetypeGlyph* glyph = it != atlas.glyphs.end() ? &it->second : fallback;
            if (glyph) {
                instance.glyphId = glyph->glyphId;
                instance.xAdvance = glyph->advance;
                std::memcpy(instance.planeBounds, glyph->planeBounds, sizeof instance.planeBounds);
                std::memcpy(instance.uvRect, glyph->uvRect, sizeof instance.uvRect);
            }
        }
        glyphs.push_back(instance);
        pos += decoded.length;
    }
    return glyphs;
}

} // namespace ToolEngine::SpriteUi::Text
