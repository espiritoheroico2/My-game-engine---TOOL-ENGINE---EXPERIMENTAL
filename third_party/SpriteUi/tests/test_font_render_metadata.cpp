// Derivacao da flag do shader a partir do metadado do atlas (Text/FontRenderMetadata.hpp).
// Funcao pura: roda sem fonte, sem stb e sem GPU.
#include "SpriteUi/Rendering/RenderFlags.hpp"
#include "SpriteUi/Text/FontRenderMetadata.hpp"

#include <cstdio>

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

// Atlas de cobertura vira FlagBitmapText e nunca FlagMsdfText.
void TestBitmapCoverageMapsToBitmapFlag() {
    current = "TestBitmapCoverageMapsToBitmapFlag";
    const auto flag = TextShaderFlagFor(TextRenderMode::BitmapCoverage);
    CHECK(flag.has_value());
    CHECK(flag.value_or(0u) == Rendering::FlagBitmapText);
    CHECK(flag.value_or(0u) != Rendering::FlagMsdfText);
}

// O padrao de FontRenderMetadata e bitmap: um atlas sem metadado explicito nao vira MSDF.
void TestDefaultMetadataIsBitmap() {
    current = "TestDefaultMetadataIsBitmap";
    const FontRenderMetadata defaults;
    CHECK(defaults.mode == TextRenderMode::BitmapCoverage);
    CHECK(TextShaderFlagFor(defaults).value_or(0u) == Rendering::FlagBitmapText);
}

// MSDF so vale quando o metadado diz MSDF (o gerador ainda nao existe: a flag so descreve o formato).
void TestMsdfMapsToMsdfFlag() {
    current = "TestMsdfMapsToMsdfFlag";
    FontRenderMetadata msdf;
    msdf.mode = TextRenderMode::Msdf;
    msdf.pxRange = 4.0f;
    CHECK(TextShaderFlagFor(msdf).value_or(0u) == Rendering::FlagMsdfText);
}

// Modos sem caminho de shader nesta versao nao caem em outra flag em silencio.
void TestUnsupportedModesHaveNoFlag() {
    current = "TestUnsupportedModesHaveNoFlag";
    CHECK(!TextShaderFlagFor(TextRenderMode::Sdf).has_value());
    CHECK(!TextShaderFlagFor(TextRenderMode::Mtsdf).has_value());
    CHECK(!TextShaderFlagFor(TextRenderMode::Color).has_value());
}

} // namespace

int main() {
    TestBitmapCoverageMapsToBitmapFlag();
    TestDefaultMetadataIsBitmap();
    TestMsdfMapsToMsdfFlag();
    TestUnsupportedModesHaveNoFlag();
    std::printf("font render metadata: %d/%d checks passed\n", run - failed, run);
    return failed == 0 ? 0 : 1;
}
