// Testes do atlas stb_truetype. Executavel proprio (nao usa o harness de test_main.cpp).
// Compilado apenas com SPRITEUI_WITH_TEXT_STB=ON.

#include "SpriteUi/Text/TextLayout.hpp"
#include "SpriteUi/Text/TruetypeAtlas.hpp"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
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
    TestOverflowReturnsFalseAndEmptiesOutput();
    TestWordWrapUsesAtlasMetrics();

    std::cout << "truetype atlas: " << (g_run - g_failed) << "/" << g_run << " checks passed\n";
    return g_failed == 0 ? 0 : 1;
}
