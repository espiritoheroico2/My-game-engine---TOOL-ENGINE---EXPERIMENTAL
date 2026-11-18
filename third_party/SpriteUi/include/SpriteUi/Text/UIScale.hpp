#pragma once

// Escala de interface (UI scale) como fator de primeira classe do texto.
//
// Uma escala so define tudo: o tamanho fisico do texto e logico x escala. A partir dele saem o bucket do atlas
// (raster), o grid de pixel do layout (devicePixelScale) e a chave do cache de atlas. Assim o texto de 1.0x, 1.25x
// e 2.0x sai nitido, porque o atlas tem resolucao suficiente para o tamanho fisico, e nao e uma imagem esticada.
//
//   tamanho fisico = tamanhoLogico x escala
//   raster         = menor bucket >= ceil(tamanho fisico x oversampling)      (BitmapRasterPolicy)
//
// Padrao: 1.25 (125 por cento). Exemplo de jogo: um rotulo de 16 px logicos em tela de 1.25x ocupa 20 px fisicos;
// com oversampling 1.25 pede 25 px e usa o bucket 32. Em 2.0x, 16 px logicos pedem 40 px e usam o bucket 48.
//
// Filtro e gama: o filtro de amostragem (linear) e do backend, nao do core. A gama de cobertura e o campo
// TruetypeAtlasConfig::coverageGamma; este modulo deixa o valor padrao (1.0) e o host escolhe.

#include "SpriteUi/Text/BitmapRasterPolicy.hpp"
#include "SpriteUi/Text/FontAtlasCache.hpp"
#include "SpriteUi/Text/TextLayout.hpp"
#include "SpriteUi/Text/TruetypeAtlas.hpp"

#include <cstdint>

namespace ToolEngine::SpriteUi::Text {

inline constexpr float kDefaultUIScale = 1.25f; // 125 por cento
inline constexpr float kMinUIScale = 0.5f;
inline constexpr float kMaxUIScale = 4.0f;

// Escala valida no intervalo [kMinUIScale, kMaxUIScale]. Valor nao finito ou nao positivo volta ao padrao.
[[nodiscard]] float SanitizeUIScale(float uiScale) noexcept;

// Tamanho do em de rasterizacao (bucket) para um texto logico nesta escala.
[[nodiscard]] std::uint32_t BitmapRasterSizeFor(float logicalFontSize, float uiScale,
                                                const BitmapRasterPolicy& policy = {}) noexcept;

// Configuracao de layout: fontSize logico e grid de pixel igual a escala (glifos caem no pixel fisico).
[[nodiscard]] TextLayoutConfig MakeScaledTextConfig(float logicalFontSize, float uiScale) noexcept;

// Configuracao do atlas bitmap para este tamanho e escala: raster no bucket e a textura cresce (512 a 4096) quando o
// bucket e grande demais para o padrao de 512 px. A gama fica no padrao (1.0).
[[nodiscard]] TruetypeAtlasConfig MakeBitmapAtlasConfig(float logicalFontSize, float uiScale,
                                                        const BitmapRasterPolicy& policy = {}) noexcept;

// Chave do cache de atlas para este tamanho e escala: a base (fonte, estilo) com o bucket preenchido. Dois tamanhos
// no mesmo bucket compartilham o atlas.
[[nodiscard]] FontAtlasKey MakeBitmapAtlasKey(FontAtlasKey base, float logicalFontSize, float uiScale,
                                              const BitmapRasterPolicy& policy = {}) noexcept;

} // namespace ToolEngine::SpriteUi::Text
