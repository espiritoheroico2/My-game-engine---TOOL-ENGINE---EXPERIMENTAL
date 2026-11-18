#include "SpriteUi/Text/UIScale.hpp"

#include <algorithm>
#include <cmath>

namespace ToolEngine::SpriteUi::Text {

namespace {

// Tamanho de textura (lado) para caber os glifos ASCII no bucket. Estimativa grosseira: avanco medio de 0,6 em,
// altura de linha de 1,2 em, mais uma folga de 30 por cento. Bucket 256 pede 4096; bucket 48 cabe em 512.
std::uint32_t AtlasSideFor(std::uint32_t raster) noexcept {
    constexpr double kGlyphCount = 95.0;   // ASCII imprimivel (0x20 a 0x7E)
    const double r = static_cast<double>(raster);
    const double needed = kGlyphCount * (0.6 * r + 1.0) * (1.2 * r + 1.0) * 1.3;
    std::uint32_t side = 512;
    while (side < 4096 && static_cast<double>(side) * static_cast<double>(side) < needed) side *= 2;
    return side;
}

} // namespace

float SanitizeUIScale(float uiScale) noexcept {
    if (!std::isfinite(uiScale) || uiScale <= 0.0f) return kDefaultUIScale;
    return std::clamp(uiScale, kMinUIScale, kMaxUIScale);
}

std::uint32_t BitmapRasterSizeFor(float logicalFontSize, float uiScale, const BitmapRasterPolicy& policy) noexcept {
    return ChooseBitmapRasterPixelSize(logicalFontSize, SanitizeUIScale(uiScale), policy);
}

TextLayoutConfig MakeScaledTextConfig(float logicalFontSize, float uiScale) noexcept {
    TextLayoutConfig config;
    config.fontSize = (std::isfinite(logicalFontSize) && logicalFontSize > 0.0f) ? logicalFontSize : 16.0f;
    config.wrap = TextWrap::None;
    config.devicePixelScale = SanitizeUIScale(uiScale);
    return config;
}

TruetypeAtlasConfig MakeBitmapAtlasConfig(float logicalFontSize, float uiScale,
                                          const BitmapRasterPolicy& policy) noexcept {
    TruetypeAtlasConfig config;
    const std::uint32_t raster = BitmapRasterSizeFor(logicalFontSize, uiScale, policy);
    config.rasterPixelSize = static_cast<float>(raster);
    config.atlasWidth = AtlasSideFor(raster);
    config.atlasHeight = config.atlasWidth;
    return config;
}

FontAtlasKey MakeBitmapAtlasKey(FontAtlasKey base, float logicalFontSize, float uiScale,
                                const BitmapRasterPolicy& policy) noexcept {
    base.mode = TextRenderMode::BitmapCoverage;
    base.rasterBucket = static_cast<std::uint16_t>(BitmapRasterSizeFor(logicalFontSize, uiScale, policy));
    return base;
}

} // namespace ToolEngine::SpriteUi::Text
