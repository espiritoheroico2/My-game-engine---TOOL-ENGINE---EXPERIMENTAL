// Escala de interface (UI scale) e texto nitido: bucket por escala, altura coerente do glifo e snap a pixel fisico.
// Usa a fonte do projeto (Xmind). Executavel proprio, sem o harness principal.

#include "SpriteUi/Core/Handle.hpp"
#include "SpriteUi/Text/TextLayout.hpp"
#include "SpriteUi/Text/TextShaper.hpp"
#include "SpriteUi/Text/TruetypeAtlas.hpp"
#include "SpriteUi/Text/UIScale.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

#ifndef SPRITEUI_UI_SCALE_TEST_FONT
#define SPRITEUI_UI_SCALE_TEST_FONT "Fonts/Xmind/static/Xmind-Regular.ttf"
#endif

namespace {

using namespace ToolEngine::SpriteUi;
using namespace ToolEngine::SpriteUi::Text;

int run = 0;
int failed = 0;
const char* current = "";

void Check(bool ok, const char* expr) {
    ++run;
    if (!ok) {
        ++failed;
        std::fprintf(stderr, "[FAIL] %s: %s\n", current, expr);
    }
}
#define CHECK(expr) Check((expr), #expr)

std::vector<std::uint8_t> LoadFont() {
    std::ifstream file(SPRITEUI_UI_SCALE_TEST_FONT, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void TestSanitizeAndDefaults() {
    current = "TestSanitizeAndDefaults";
    CHECK(kDefaultUIScale == 1.25f);
    CHECK(SanitizeUIScale(1.25f) == 1.25f);
    CHECK(SanitizeUIScale(0.0f) == kDefaultUIScale);
    CHECK(SanitizeUIScale(-2.0f) == kDefaultUIScale);
    CHECK(SanitizeUIScale(std::nanf("")) == kDefaultUIScale);
    CHECK(SanitizeUIScale(99.0f) == kMaxUIScale);
    CHECK(SanitizeUIScale(0.1f) == kMinUIScale);
    CHECK(MakeScaledTextConfig(16.0f, 1.5f).devicePixelScale == 1.5f);
}

void TestRasterBucketFollowsPhysicalSize() {
    current = "TestRasterBucketFollowsPhysicalSize";
    // 16 px logicos: 1.0x -> 20 -> 24; 1.25x -> 25 -> 32; 2.0x -> 40 -> 48.
    CHECK(BitmapRasterSizeFor(16.0f, 1.0f) == 24);
    CHECK(BitmapRasterSizeFor(16.0f, 1.25f) == 32);
    CHECK(BitmapRasterSizeFor(16.0f, 2.0f) == 48);
    // Escala maior nunca pede raster menor.
    CHECK(BitmapRasterSizeFor(16.0f, 2.0f) >= BitmapRasterSizeFor(16.0f, 1.25f));
    // Escala invalida cai no padrao 1.25.
    CHECK(BitmapRasterSizeFor(16.0f, std::nanf("")) == BitmapRasterSizeFor(16.0f, kDefaultUIScale));
    // Chave do cache leva o bucket: dois tamanhos no mesmo bucket compartilham o atlas.
    FontAtlasKey base{};
    base.file = 42;
    CHECK(MakeBitmapAtlasKey(base, 16.0f, 1.25f).rasterBucket == 32);
    // 16 e 17 px a 1.25x pedem 25 e 26,6 px: os dois caem no bucket 32 e compartilham o atlas.
    CHECK(MakeBitmapAtlasKey(base, 16.0f, 1.25f) == MakeBitmapAtlasKey(base, 17.0f, 1.25f));
    CHECK(MakeBitmapAtlasKey(base, 16.0f, 2.0f) != MakeBitmapAtlasKey(base, 16.0f, 1.0f));
}

// Altura do glifo 'H' em pixels do atlas, a partir dos limites do glifo (em unidades de fonte, em = 1000).
float GlyphHeightPixels(const TruetypeAtlas& atlas, std::uint32_t codepoint) {
    const auto it = atlas.glyphs.find(codepoint);
    if (it == atlas.glyphs.end()) return -1.0f;
    const float* b = it->second.planeBounds;
    return (b[3] - b[1]) / 1000.0f * static_cast<float>(atlas.renderMetadata.rasterPixelSize);
}

// Texto "Hello" em 1.0x, 1.25x e 2.0x: cada atlas tem o bucket escolhido e a altura do glifo acompanha o raster.
void TestHelloHasCoherentGlyphHeightAtEachScale() {
    current = "TestHelloHasCoherentGlyphHeightAtEachScale";
    const std::vector<std::uint8_t> font = LoadFont();
    CHECK(!font.empty());
    if (font.empty()) return;

    const float scales[] = {1.0f, 1.25f, 2.0f};
    float ratios[3] = {0, 0, 0};
    for (int i = 0; i < 3; ++i) {
        const float s = scales[i];
        const TruetypeAtlasConfig config = MakeBitmapAtlasConfig(16.0f, s);
        CHECK(config.rasterPixelSize == static_cast<float>(BitmapRasterSizeFor(16.0f, s)));
        TruetypeAtlas atlas;
        CHECK(BuildTruetypeAtlas(font, config, atlas));
        if (!atlas.IsValid()) continue;
        CHECK(atlas.renderMetadata.rasterPixelSize == static_cast<std::uint32_t>(config.rasterPixelSize));

        // Altura do H coerente com o raster: uma fracao estavel do em (altura de capitular, ~0,7 em).
        const float h = GlyphHeightPixels(atlas, 'H');
        ratios[i] = h / config.rasterPixelSize;
        CHECK(ratios[i] > 0.55f && ratios[i] < 0.80f);
        // Pixels suficientes para o tamanho fisico: o raster nao e menor que logico x escala.
        CHECK(config.rasterPixelSize >= 16.0f * s);
        // O texto "Hello" tem um glifo por letra, e cada uma existe no atlas.
        const auto glyphs = ShapeTruetype("Hello", atlas);
        CHECK(glyphs.size() == 5);
    }
    // A proporcao e a mesma nas tres escalas (o atlas nao esta esticado): diferenca pequena.
    CHECK(std::fabs(ratios[0] - ratios[1]) < 0.06f);
    CHECK(std::fabs(ratios[1] - ratios[2]) < 0.06f);
    // A altura em pixels de tela cresce com a escala: 2.0x tem o H mais alto que 1.0x.
    CHECK(ratios[2] * 48.0f > ratios[0] * 24.0f);
}

// Snap a pixel fisico: o desenho de cada glifo cai num pixel de tela inteiro (posicao x 1.25 e inteira). O layout e
// o caret continuam em coordenadas logicas, sem snap, para nao depender do DPI.
void TestScaledLayoutSnapsGlyphOriginsToPhysicalPixels() {
    current = "TestScaledLayoutSnapsGlyphOriginsToPhysicalPixels";
    const std::vector<std::uint8_t> font = LoadFont();
    if (font.empty()) {
        CHECK(!font.empty());
        return;
    }
    TruetypeAtlas atlas;
    if (!BuildTruetypeAtlas(font, MakeBitmapAtlasConfig(16.0f, 1.25f), atlas)) {
        CHECK(false);
        return;
    }
    const auto glyphs = ShapeTruetype("Hello world", atlas);
    const TextLayoutResult layout = LayoutText("Hello world", glyphs, atlas.metrics, MakeScaledTextConfig(16.0f, 1.25f));
    CHECK(!layout.quads.empty());
    UILayoutRect box;
    box.resolvedPosition = {0.0f, 0.0f};
    box.resolvedSize = layout.size;
    const auto items = GenerateTextRenderItems(layout, box, {1.0f, 1.0f, 1.0f, 1.0f}, 1u, Rendering::NoClipIndex,
                                               0.0f, 0, UIHandle{1, 1});
    CHECK(!items.empty() && items.size() <= layout.quads.size());   // espacos nao geram item
    bool snapped = !items.empty();
    for (const Rendering::UIRenderItem& item : items) {
        const float px = item.layout.resolvedPosition.x * 1.25f;
        const float py = item.layout.resolvedPosition.y * 1.25f;
        snapped = snapped && std::fabs(px - std::round(px)) < 1.0e-3f && std::fabs(py - std::round(py)) < 1.0e-3f;
    }
    CHECK(snapped);
}

} // namespace

int main() {
    TestSanitizeAndDefaults();
    TestRasterBucketFollowsPhysicalSize();
    TestHelloHasCoherentGlyphHeightAtEachScale();
    TestScaledLayoutSnapsGlyphOriginsToPhysicalPixels();
    std::printf("ui scale: %d/%d checks passed\n", run - failed, run);
    return failed == 0 ? 0 : 1;
}
