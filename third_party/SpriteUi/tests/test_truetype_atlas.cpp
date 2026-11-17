// Testes do atlas stb_truetype. Executavel proprio (nao usa o harness de test_main.cpp).
// Compilado apenas com SPRITEUI_WITH_TEXT_STB=ON.

#include "SpriteUi/Text/TextLayout.hpp"
#include "SpriteUi/Text/TextShaper.hpp"
#include "SpriteUi/Text/TruetypeAtlas.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

#ifndef SPRITEUI_TEST_FONT_PATH
#error "SPRITEUI_TEST_FONT_PATH must be defined by CMake"
#endif

namespace {

using namespace ToolEngine::SpriteUi;
using namespace ToolEngine::SpriteUi::Text;

int g_run = 0;
int g_failed = 0;
const char* g_current = "";

void Check(bool condition, const char* expression) {
    ++g_run;
    if (!condition) {
        ++g_failed;
        std::cerr << "[FAIL] " << g_current << ": " << expression << '\n';
    }
}

#define CHECK(expr) Check((expr), #expr)

bool Near(float a, float b, float epsilon = 1.0e-4f) {
    return std::fabs(a - b) <= epsilon;
}

std::vector<std::uint8_t> LoadFont() {
    std::ifstream file(SPRITEUI_TEST_FONT_PATH, std::ios::binary);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

bool BuildDefault(TruetypeAtlas& atlas) {
    static const std::vector<std::uint8_t> font = LoadFont();
    return BuildTruetypeAtlas(font, TruetypeAtlasConfig{}, atlas);
}

void TestAtlasBuildsAndHasCoverage() {
    g_current = "TestAtlasBuildsAndHasCoverage";
    TruetypeAtlas atlas;
    CHECK(BuildDefault(atlas));
    CHECK(atlas.IsValid());
    CHECK(atlas.glyphs.size() == 0x7E - 0x20 + 1);

    std::size_t lit = 0;
    for (std::uint8_t p : atlas.pixels) lit += p != 0 ? 1 : 0;
    CHECK(lit > 1000);
}

void TestLetterGeometry() {
    g_current = "TestLetterGeometry";
    TruetypeAtlas atlas;
    CHECK(BuildDefault(atlas));

    const auto a = atlas.glyphs.find('A');
    CHECK(a != atlas.glyphs.end());
    if (a == atlas.glyphs.end()) return;
    const TruetypeGlyph& g = a->second;
    CHECK(g.advance > 0.0f);
    CHECK(g.planeBounds[2] > g.planeBounds[0]);          // Largura positiva.
    CHECK(g.planeBounds[3] > g.planeBounds[1]);          // Altura positiva, y para cima.
    CHECK(g.planeBounds[1] > -60.0f);                    // 'A' encosta na linha de base (tolerancia de arredondamento de pixel).
    CHECK(g.uvRect[0] >= 0.0f && g.uvRect[2] <= 1.0f);
    CHECK(g.uvRect[1] >= 0.0f && g.uvRect[3] <= 1.0f);
    CHECK(g.uvRect[2] > g.uvRect[0] && g.uvRect[3] > g.uvRect[1]);

    const auto g_it = atlas.glyphs.find('g');
    CHECK(g_it != atlas.glyphs.end());
    if (g_it != atlas.glyphs.end()) {
        CHECK(g_it->second.planeBounds[1] < 0.0f);       // Descendente abaixo da linha de base.
    }
}

void TestSpaceHasAdvanceButNoArea() {
    g_current = "TestSpaceHasAdvanceButNoArea";
    TruetypeAtlas atlas;
    CHECK(BuildDefault(atlas));

    const auto space = atlas.glyphs.find(' ');
    CHECK(space != atlas.glyphs.end());
    if (space == atlas.glyphs.end()) return;
    CHECK(space->second.advance > 0.0f);
    CHECK(space->second.planeBounds[0] == space->second.planeBounds[2]);
    CHECK(space->second.planeBounds[1] == space->second.planeBounds[3]);
}

void TestShapeAsciiClusters() {
    g_current = "TestShapeAsciiClusters";
    TruetypeAtlas atlas;
    CHECK(BuildDefault(atlas));

    const auto glyphs = ShapeTruetype("Hi!", atlas);
    CHECK(glyphs.size() == 3);
    if (glyphs.size() != 3) return;
    CHECK(glyphs[0].clusterByteOffset == 0);
    CHECK(glyphs[1].clusterByteOffset == 1);
    CHECK(glyphs[2].clusterByteOffset == 2);
    CHECK(glyphs[0].xAdvance > 0.0f && glyphs[1].xAdvance > 0.0f);
    CHECK(glyphs[0].glyphId == atlas.glyphs.at('H').glyphId);
}

void TestMultibyteFallbackKeepsClusters() {
    g_current = "TestMultibyteFallbackKeepsClusters";
    TruetypeAtlas atlas;
    CHECK(BuildDefault(atlas));

    // 'é' (C3 A9) esta fora do intervalo ASCII do atlas: vira '?' e ocupa o byte 0 como um unico cluster.
    const auto glyphs = ShapeTruetype("\xC3\xA9" "a", atlas);
    CHECK(glyphs.size() == 2);
    if (glyphs.size() != 2) return;
    CHECK(glyphs[0].glyphId == atlas.glyphs.at('?').glyphId);
    CHECK(glyphs[0].clusterByteOffset == 0);
    CHECK(glyphs[1].clusterByteOffset == 2);
}

void TestInvalidUtf8BecomesReplacement() {
    g_current = "TestInvalidUtf8BecomesReplacement";
    TruetypeAtlas atlas;
    CHECK(BuildDefault(atlas));

    // 0xFF nunca e um byte UTF-8 valido; 0xC3 sem continuacao tambem nao.
    const auto glyphs = ShapeTruetype("\xFF\xC3z", atlas);
    CHECK(glyphs.size() == 3);
    if (glyphs.size() != 3) return;
    CHECK(glyphs[0].clusterByteOffset == 0);
    CHECK(glyphs[1].clusterByteOffset == 1);
    CHECK(glyphs[2].clusterByteOffset == 2);
    CHECK(glyphs[2].glyphId == atlas.glyphs.at('z').glyphId);
}

void TestTabAndControls() {
    g_current = "TestTabAndControls";
    TruetypeAtlas atlas;
    CHECK(BuildDefault(atlas));

    const float space = atlas.glyphs.at(' ').advance;
    const auto tab = ShapeTruetype("\t", atlas);
    CHECK(tab.size() == 1);
    if (tab.size() == 1) CHECK(std::abs(tab[0].xAdvance - 4.0f * space) < 0.01f);

    const auto newline = ShapeTruetype("\n", atlas);
    CHECK(newline.size() == 1);
    if (newline.size() == 1) {
        CHECK(newline[0].xAdvance == 0.0f);
        CHECK(newline[0].planeBounds[0] == newline[0].planeBounds[2]);
    }
}

void TestRejectsBadInput() {
    g_current = "TestRejectsBadInput";
    const std::vector<std::uint8_t> font = LoadFont();
    TruetypeAtlas atlas;

    CHECK(!BuildTruetypeAtlas({}, TruetypeAtlasConfig{}, atlas));
    CHECK(!BuildTruetypeAtlas(std::vector<std::uint8_t>{1, 2, 3, 4}, TruetypeAtlasConfig{}, atlas));

    TruetypeAtlasConfig zeroSize;
    zeroSize.rasterPixelSize = 0.0f;
    CHECK(!BuildTruetypeAtlas(font, zeroSize, atlas));

    TruetypeAtlasConfig reversed;
    reversed.firstCodepoint = 0x7E;
    reversed.lastCodepoint = 0x20;
    CHECK(!BuildTruetypeAtlas(font, reversed, atlas));
    CHECK(atlas.glyphs.empty());
}

void TestOverflowReturnsFalseAndEmptiesOutput() {
    g_current = "TestOverflowReturnsFalseAndEmptiesOutput";
    const std::vector<std::uint8_t> font = LoadFont();
    TruetypeAtlasConfig tiny;
    tiny.atlasWidth = 16;
    tiny.atlasHeight = 16;                 // 48 px de em nao cabe num atlas 16x16.
    TruetypeAtlas atlas;
    CHECK(!BuildTruetypeAtlas(font, tiny, atlas));
    CHECK(atlas.glyphs.empty());
    CHECK(atlas.pixels.empty());
}

void TestWordWrapUsesAtlasMetrics() {
    g_current = "TestWordWrapUsesAtlasMetrics";
    TruetypeAtlas atlas;
    CHECK(BuildDefault(atlas));

    const std::string text = "Hello wide world again";
    const auto glyphs = ShapeTruetype(text, atlas);

    // Tamanho 1000 com unitsPerEm 1000 faz uma unidade de fonte valer um pixel.
    TextLayoutConfig config;
    config.fontSize = 1000.0f;
    config.maxWidth = 1500.0f;
    config.wrap = TextWrap::Word;
    const TextLayoutResult result = LayoutText(text, glyphs, atlas.metrics, config);
    CHECK(result.lineCount >= 2);
    CHECK(result.size.x <= config.maxWidth + 0.01f);
}

// Regressao: o item de desenho recebe [u, v, largura, altura]. O atlas guarda [u0, v0, u1, v1].
void TestTextRenderItemUsesWidthHeightUv() {
    g_current = "TestTextRenderItemUsesWidthHeightUv";
    TruetypeAtlas atlas;
    CHECK(BuildDefault(atlas));
    const auto shaped = ShapeTruetype("M", atlas);
    CHECK(shaped.size() == 1);
    const auto& g = atlas.glyphs.at('M');
    TextLayoutConfig cfg;
    cfg.fontSize = 15.0f;
    const auto layout = LayoutText(std::string_view("M"), shaped, atlas.metrics, cfg);
    CHECK(!layout.quads.empty());
    UILayoutRect box;
    box.resolvedSize = {100.0f, 20.0f};
    const auto items = GenerateTextRenderItems(layout, box, {1.0f, 1.0f, 1.0f, 1.0f}, 1u);
    CHECK(items.size() == 1);
    if (items.size() != 1) return;
    const auto& uv = items[0].uvRect;
    CHECK(std::fabs(uv[0] - g.uvRect[0]) < 1e-6f);
    CHECK(std::fabs(uv[1] - g.uvRect[1]) < 1e-6f);
    CHECK(std::fabs(uv[2] - (g.uvRect[2] - g.uvRect[0])) < 1e-6f);   // largura, nao u1
    CHECK(std::fabs(uv[3] - (g.uvRect[3] - g.uvRect[1])) < 1e-6f);   // altura, nao v1
}


// Gama de cobertura: com 0.5, cada alfa vira a raiz quadrada do alfa original (+-1 por arredondamento).
void TestCoverageGammaMapsAlpha() {
    g_current = "TestCoverageGammaMapsAlpha";
    const std::vector<std::uint8_t> font = LoadFont();
    TruetypeAtlas plain, boosted;
    CHECK(BuildTruetypeAtlas(font, TruetypeAtlasConfig{}, plain));
    TruetypeAtlasConfig gammaConfig;
    gammaConfig.coverageGamma = 0.5f;
    CHECK(BuildTruetypeAtlas(font, gammaConfig, boosted));
    CHECK(plain.pixels.size() == boosted.pixels.size());

    int worst = 0;
    for (std::size_t i = 0; i < plain.pixels.size() && i < boosted.pixels.size(); ++i) {
        const double expect = std::round(std::sqrt(plain.pixels[i] / 255.0) * 255.0);
        worst = std::max(worst, static_cast<int>(std::abs(expect - boosted.pixels[i])));
    }
    CHECK(worst <= 1);
    CHECK(boosted.pixels[0] == 0);                         // Pixels vazios continuam vazios.
}

void TestRejectsBadCoverageGamma() {
    g_current = "TestRejectsBadCoverageGamma";
    const std::vector<std::uint8_t> font = LoadFont();
    TruetypeAtlas atlas;
    TruetypeAtlasConfig zero;
    zero.coverageGamma = 0.0f;
    CHECK(!BuildTruetypeAtlas(font, zero, atlas));
    TruetypeAtlasConfig nan;
    nan.coverageGamma = std::numeric_limits<float>::quiet_NaN();
    CHECK(!BuildTruetypeAtlas(font, nan, atlas));
}

// Geometria: a largura de uma linha bate com a soma dos avancos dos glifos (tolerancia de 1 px), em 12, 16 e 24 px
// e com escala de tela 1.0 e 1.5. O alinhamento a pixel nao pode acumular erro no avanco.
void TestLineWidthMatchesGlyphAdvances() {
    g_current = "TestLineWidthMatchesGlyphAdvances";
    TruetypeAtlas atlas;
    CHECK(BuildDefault(atlas));
    const std::string text = "Configuracoes Ajuda Hierarquia";
    const auto glyphs = ShapeTruetype(text, atlas);
    for (float size : {12.0f, 16.0f, 24.0f}) {
        for (float scale : {1.0f, 1.5f}) {
            float sum = 0.0f;
            for (const GlyphInstance& glyph : glyphs) sum += glyph.xAdvance * size / atlas.metrics.unitsPerEm;
            TextLayoutConfig config;
            config.fontSize = size;
            config.devicePixelScale = scale;
            const TextLayoutResult layout = LayoutText(text, glyphs, atlas.metrics, config);
            CHECK(layout.lines.size() == 1);
            if (layout.lines.size() == 1) CHECK(std::abs(layout.lines[0].width - sum) <= 1.0f);
        }
    }
}

// Alinhamento: com escala 1.5, a origem de cada glifo cai em um pixel de tela inteiro (x * 1.5 e inteiro).
void TestGlyphOriginsSnapToDevicePixels() {
    g_current = "TestGlyphOriginsSnapToDevicePixels";
    TruetypeAtlas atlas;
    CHECK(BuildDefault(atlas));
    const std::string text = "Ab";
    const auto glyphs = ShapeTruetype(text, atlas);
    TextLayoutConfig config;
    config.fontSize = 16.0f;
    config.devicePixelScale = 1.5f;
    const TextLayoutResult layout = LayoutText(text, glyphs, atlas.metrics, config);
    UILayoutRect box;
    box.resolvedPosition = {10.3f, 7.7f};
    box.resolvedSize = {100.0f, 20.0f};
    const auto items = GenerateTextRenderItems(layout, box, {1.0f, 1.0f, 1.0f, 1.0f}, 1u);
    CHECK(items.size() == 2);
    for (const auto& item : items) {
        const float dx = item.layout.resolvedPosition.x * 1.5f;
        const float dy = item.layout.resolvedPosition.y * 1.5f;
        CHECK(std::fabs(dx - std::round(dx)) < 1e-3f);
        CHECK(std::fabs(dy - std::round(dy)) < 1e-3f);
    }
}

// Sem alinhamento (escala 0), a posicao segue fracionaria, como antes.
void TestSnapOffKeepsFractionalOrigin() {
    g_current = "TestSnapOffKeepsFractionalOrigin";
    TruetypeAtlas atlas;
    CHECK(BuildDefault(atlas));
    const std::string text = "A";
    const auto glyphs = ShapeTruetype(text, atlas);
    TextLayoutConfig config;
    config.fontSize = 16.0f;
    config.devicePixelScale = 0.0f;
    const TextLayoutResult layout = LayoutText(text, glyphs, atlas.metrics, config);
    UILayoutRect box;
    box.resolvedPosition = {10.3f, 7.7f};
    box.resolvedSize = {100.0f, 20.0f};
    const auto items = GenerateTextRenderItems(layout, box, {1.0f, 1.0f, 1.0f, 1.0f}, 1u);
    CHECK(items.size() == 1);
    if (items.size() == 1) {
        CHECK(std::fabs(items[0].layout.resolvedPosition.x - (10.3f + layout.quads[0].bounds.position.x)) < 1e-4f);
    }
}

void WriteBigEndian32(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint32_t value) {
    bytes[at] = static_cast<std::uint8_t>(value >> 24);
    bytes[at + 1] = static_cast<std::uint8_t>(value >> 16);
    bytes[at + 2] = static_cast<std::uint8_t>(value >> 8);
    bytes[at + 3] = static_cast<std::uint8_t>(value);
}

// Validacao estrutural do arquivo: truncado, cabecalho invalido, tabela fora dos limites (com overflow de 32 bits),
// diretorio que nao cabe, TTC sem fonte, arquivo acima do limite e politica de origem.
void TestFontFileValidation() {
    g_current = "TestFontFileValidation";
    const std::vector<std::uint8_t> font = LoadFont();
    const TruetypeAtlasConfig config;
    TruetypeAtlas atlas;
    CHECK(BuildTruetypeAtlas(font, config, atlas));   // Controle: a fonte integra passa.

    // Truncada: so o cabecalho sobra, e o diretorio de 20 tabelas nao cabe.
    const std::vector<std::uint8_t> onlyHeader(font.begin(), font.begin() + 12);
    CHECK(!BuildTruetypeAtlas(onlyHeader, config, atlas));
    // Truncada no meio: as tabelas cobrem o arquivo inteiro, entao alguma sai do fim.
    const std::vector<std::uint8_t> halfFile(font.begin(), font.begin() + static_cast<std::ptrdiff_t>(font.size() / 2));
    CHECK(!BuildTruetypeAtlas(halfFile, config, atlas));
    CHECK(atlas.glyphs.empty());

    // Cabecalho que nao e de fonte.
    std::vector<std::uint8_t> badHeader = font;
    badHeader[0] = 'A';
    badHeader[1] = 'B';
    badHeader[2] = 'C';
    badHeader[3] = 'D';
    CHECK(!BuildTruetypeAtlas(badHeader, config, atlas));

    // Primeira tabela com offset 0xFFFFFF00 e comprimento 0x200: a soma em 32 bits daria 0x100, e passaria.
    std::vector<std::uint8_t> wrapped = font;
    WriteBigEndian32(wrapped, 20, 0xFFFFFF00u);   // offset da entrada 0 (cabecalho 12 + 8)
    WriteBigEndian32(wrapped, 24, 0x00000200u);   // comprimento da entrada 0 (cabecalho 12 + 12)
    CHECK(!BuildTruetypeAtlas(wrapped, config, atlas));

    // Comprimento de tabela absurdo, com offset zero.
    std::vector<std::uint8_t> hugeLength = font;
    WriteBigEndian32(hugeLength, 24, 0xFFFFFFFFu);
    CHECK(!BuildTruetypeAtlas(hugeLength, config, atlas));

    // Diretorio que declara 65535 tabelas num arquivo de 144 KB.
    std::vector<std::uint8_t> manyTables = font;
    manyTables[4] = 0xFF;
    manyTables[5] = 0xFF;
    CHECK(!BuildTruetypeAtlas(manyTables, config, atlas));

    // Colecao TTC com zero fontes.
    std::vector<std::uint8_t> emptyCollection = font;
    emptyCollection[0] = 't';
    emptyCollection[1] = 't';
    emptyCollection[2] = 'c';
    emptyCollection[3] = 'f';
    WriteBigEndian32(emptyCollection, 8, 0u);
    CHECK(!BuildTruetypeAtlas(emptyCollection, config, atlas));

    // Limite de tamanho: um byte a menos recusa; exatamente o limite aceita.
    FontLoadPolicy tight;
    tight.maxFontFileBytes = font.size() - 1;
    CHECK(!BuildTruetypeAtlas(font, config, atlas, tight));
    CHECK(atlas.pixels.empty());
    FontLoadPolicy exact;
    exact.maxFontFileBytes = font.size();
    CHECK(BuildTruetypeAtlas(font, config, atlas, exact));

    // Origem: com allowUntrustedFonts = false, so o caminho Trusted carrega. O padrao aceita as duas.
    FontLoadPolicy strict;
    strict.allowUntrustedFonts = false;
    CHECK(!BuildTruetypeAtlas(font, config, atlas, strict, FontOrigin::Untrusted));
    CHECK(BuildTruetypeAtlas(font, config, atlas, strict, FontOrigin::Trusted));
    CHECK(BuildTruetypeAtlas(font, config, atlas));
}

// O shaper padrao produz exatamente o que ShapeTruetype produz: a interface nao muda o comportamento atual.
void TestCodepointShaperMatchesShapeTruetype() {
    g_current = "TestCodepointShaperMatchesShapeTruetype";
    TruetypeAtlas atlas;
    CHECK(BuildDefault(atlas));
    const std::string text = "Vida 80\t\xC3\xA9";   // inclui tab e 'e' acentuado (2 bytes)
    const auto direct = ShapeTruetype(text, atlas);
    const auto viaShaper = CodepointShaper{}.Shape(text, atlas);
    CHECK(direct.size() == viaShaper.size());
    if (direct.size() != viaShaper.size()) return;
    for (std::size_t i = 0; i < direct.size(); ++i) {
        CHECK(direct[i].glyphId == viaShaper[i].glyphId);
        CHECK(direct[i].clusterByteOffset == viaShaper[i].clusterByteOffset);
        CHECK(Near(direct[i].xAdvance, viaShaper[i].xAdvance));
    }
}

// Um shaper alternativo alimenta o layout sem que o layout saiba quem o produziu. Aqui o avanco dobra; a largura
// da linha dobra junto, e o layout continua o mesmo.
class DoubledAdvanceShaper final : public ITextShaper {
public:
    std::vector<GlyphInstance> Shape(std::string_view utf8, const TruetypeAtlas& atlas) const override {
        std::vector<GlyphInstance> glyphs = CodepointShaper{}.Shape(utf8, atlas);
        for (GlyphInstance& glyph : glyphs) glyph.xAdvance *= 2.0f;
        return glyphs;
    }
};

void TestCustomShaperFeedsLayoutUnchanged() {
    g_current = "TestCustomShaperFeedsLayoutUnchanged";
    TruetypeAtlas atlas;
    CHECK(BuildDefault(atlas));
    const std::string text = "abc";
    const TextLayoutConfig config;
    const auto plain = LayoutText(text, CodepointShaper{}.Shape(text, atlas), atlas.metrics, config);
    const auto doubled = LayoutText(text, DoubledAdvanceShaper{}.Shape(text, atlas), atlas.metrics, config);
    CHECK(plain.quads.size() == doubled.quads.size());
    CHECK(plain.size.x > 0.0f);
    CHECK(Near(doubled.size.x, 2.0f * plain.size.x, 1.0e-2f));
}

// Limite de quantidade de glifos: rejeita antes de alocar, aceita dentro do limite e no teto absoluto.
void TestGlyphCountLimits() {
    g_current = "TestGlyphCountLimits";
    const std::vector<std::uint8_t> font = LoadFont();
    TruetypeAtlas atlas;

    TruetypeAtlasConfig none;
    none.maxGlyphCount = 0;                       // Zero nao e um limite valido.
    CHECK(!BuildTruetypeAtlas(font, none, atlas));

    TruetypeAtlasConfig overAbsolute;
    overAbsolute.maxGlyphCount = kTruetypeMaxGlyphCountLimit + 1;
    CHECK(!BuildTruetypeAtlas(font, overAbsolute, atlas));

    // ASCII imprimivel tem 0x7E - 0x20 + 1 = 95 codepoints.
    TruetypeAtlasConfig tooFew;
    tooFew.maxGlyphCount = 94;
    CHECK(!BuildTruetypeAtlas(font, tooFew, atlas));
    CHECK(atlas.glyphs.empty());
    CHECK(atlas.pixels.empty());

    TruetypeAtlasConfig exact;
    exact.maxGlyphCount = 95;
    CHECK(BuildTruetypeAtlas(font, exact, atlas));

    // Intervalo enorme com limite padrao: a rejeicao acontece antes de qualquer iteracao sobre 1.1 milhao de codepoints.
    TruetypeAtlasConfig huge;
    huge.firstCodepoint = 0;
    huge.lastCodepoint = 0x10FFFF;
    CHECK(!BuildTruetypeAtlas(font, huge, atlas));
    CHECK(atlas.pixels.empty());
}

// Tamanho de em e padding: limites superiores rejeitados, valores nos limites aceitos.
// O atlas gerado pelo stb e de cobertura: o metadado diz bitmap e a flag derivada e FlagBitmapText.
void TestAtlasMetadataIsBitmapCoverage() {
    g_current = "TestAtlasMetadataIsBitmapCoverage";
    TruetypeAtlas atlas;
    CHECK(BuildDefault(atlas));
    CHECK(atlas.renderMetadata.mode == TextRenderMode::BitmapCoverage);
    CHECK(atlas.renderMetadata.rasterPixelSize == 48u);
    const auto flag = TextShaderFlagFor(atlas.renderMetadata);
    CHECK(flag.has_value());
    CHECK(flag.value_or(0u) == Rendering::FlagBitmapText);
    CHECK(flag.value_or(0u) != Rendering::FlagMsdfText);
    // Os itens gerados com a flag do atlas carregam FlagBitmapText.
    const auto layout = LayoutText("ab", ShapeTruetype("ab", atlas), atlas.metrics, TextLayoutConfig{});
    const auto items = GenerateTextRenderItems(layout, UILayoutRect{}, {1.0f, 1.0f, 1.0f, 1.0f}, 1u,
                                               Rendering::NoClipIndex, 0.0f, 0, UIHandle{1, 1}, flag.value_or(0u));
    CHECK(!items.empty());
    for (const auto& item : items) CHECK(item.flags == Rendering::FlagBitmapText);
}

void TestRasterAndPaddingLimits() {
    g_current = "TestRasterAndPaddingLimits";
    const std::vector<std::uint8_t> font = LoadFont();
    TruetypeAtlas atlas;

    TruetypeAtlasConfig bigRaster;
    bigRaster.rasterPixelSize = kTruetypeMaxRasterPixelSize + 1.0f;
    CHECK(!BuildTruetypeAtlas(font, bigRaster, atlas));

    // Uma so letra em 512 px cabe num atlas de 1024 px: prova que o limite e inclusivo.
    TruetypeAtlasConfig edgeRaster;
    edgeRaster.rasterPixelSize = kTruetypeMaxRasterPixelSize;
    edgeRaster.atlasWidth = 1024;
    edgeRaster.atlasHeight = 1024;
    edgeRaster.firstCodepoint = 'A';
    edgeRaster.lastCodepoint = 'A';
    CHECK(BuildTruetypeAtlas(font, edgeRaster, atlas));

    TruetypeAtlasConfig bigPadding;
    bigPadding.padding = kTruetypeMaxPadding + 1;
    CHECK(!BuildTruetypeAtlas(font, bigPadding, atlas));

    // Com 64 px de borda, 95 glifos precisam de um atlas maior que o padrao de 512 px.
    TruetypeAtlasConfig edgePadding;
    edgePadding.padding = kTruetypeMaxPadding;
    edgePadding.atlasWidth = 2048;
    edgePadding.atlasHeight = 2048;
    CHECK(BuildTruetypeAtlas(font, edgePadding, atlas));
}

} // namespace

int main() {
    TestAtlasBuildsAndHasCoverage();
    TestLetterGeometry();
    TestSpaceHasAdvanceButNoArea();
    TestShapeAsciiClusters();
    TestMultibyteFallbackKeepsClusters();
    TestInvalidUtf8BecomesReplacement();
    TestTabAndControls();
    TestRejectsBadInput();
    TestTextRenderItemUsesWidthHeightUv();
    TestOverflowReturnsFalseAndEmptiesOutput();
    TestWordWrapUsesAtlasMetrics();
    TestCoverageGammaMapsAlpha();
    TestRejectsBadCoverageGamma();
    TestLineWidthMatchesGlyphAdvances();
    TestGlyphOriginsSnapToDevicePixels();
    TestSnapOffKeepsFractionalOrigin();
    TestGlyphCountLimits();
    TestRasterAndPaddingLimits();
    TestAtlasMetadataIsBitmapCoverage();
    TestFontFileValidation();
    TestCodepointShaperMatchesShapeTruetype();
    TestCustomShaperFeedsLayoutUnchanged();

    std::cout << "truetype atlas: " << (g_run - g_failed) << "/" << g_run << " checks passed\n";
    return g_failed == 0 ? 0 : 1;
}
