#pragma once

// Politica de resolucao do atlas bitmap (texto de cobertura, FlagBitmapText).
//
// O stb_truetype rasteriza o contorno da fonte uma unica vez, em P pixels por em. Depois disso o shader so
// amostra esses pixels. Se o texto final ocupar mais pixels do que P, a borda perde definicao. Esta politica
// escolhe P a partir do tamanho fisico desejado:
//
//     P_fisico  = tamanhoLogico * dpiScale          (quantos pixels de tela o texto ocupa)
//     P_raster  = ceil(P_fisico * O)                (O = sobreamostragem, padrao 1.25)
//     P         = menor bucket >= P_raster          (16, 24, 32, 48, 64, 96, 128, 192, 256)
//
// Buckets fixos evitam criar um atlas para cada tamanho exato. Acima de 256 px o bucket satura: texto que
// precisa de mais resolucao que isso pede MSDF (fase 3, ainda nao implementado).
//
// Exemplo de jogo: um rotulo de 16 pt em tela 4K com escala 2.0 ocupa 32 px fisicos; com O = 1.25 o pedido e
// ceil(40) = 40 px, e o bucket escolhido e 48. O atlas e gerado uma vez e reutilizado enquanto a fonte, o
// DPI e o bucket nao mudarem; nao reconstrua a cada quadro.

#include <array>
#include <cmath>
#include <cstdint>

namespace ToolEngine::SpriteUi::Text {

struct BitmapRasterPolicy {
    // Sobreamostragem O. 1.0 economiza memoria; 1.25 e o equilibrio padrao; 2.0 para texto que sera
    // ampliado com frequencia (zoom de editor). Valores nao finitos ou <= 0 voltam ao padrao.
    float oversampling = 1.25f;
};

// Buckets permitidos para o tamanho de rasterizacao em pixels. Ordem crescente.
inline constexpr std::array<std::uint32_t, 9> kBitmapRasterBuckets{16, 24, 32, 48, 64, 96, 128, 192, 256};

// Escolhe o tamanho de rasterizacao (em pixels) para um texto de bitmap.
// Entradas invalidas voltam a valores seguros: tamanho logico <= 0 ou nao finito vira 16, DPI invalido vira 1.
// Acima do maior bucket, a funcao devolve 256; quem precisa de mais deve usar MSDF.
[[nodiscard]] inline std::uint32_t ChooseBitmapRasterPixelSize(
    float logicalFontSize, float dpiScale, const BitmapRasterPolicy& policy = {}) noexcept {
    constexpr float kDefaultLogicalSize = 16.0f;
    constexpr float kDefaultOversampling = 1.25f;

    const float logical = (std::isfinite(logicalFontSize) && logicalFontSize > 0.0f) ? logicalFontSize
                                                                                     : kDefaultLogicalSize;
    const float dpi = (std::isfinite(dpiScale) && dpiScale > 0.0f) ? dpiScale : 1.0f;
    const float oversampling = (std::isfinite(policy.oversampling) && policy.oversampling > 0.0f)
                                   ? policy.oversampling
                                   : kDefaultOversampling;

    const float wanted = std::ceil(logical * dpi * oversampling);
    for (const std::uint32_t bucket : kBitmapRasterBuckets) {
        if (wanted <= static_cast<float>(bucket)) return bucket;
    }
    return kBitmapRasterBuckets.back();
}

} // namespace ToolEngine::SpriteUi::Text
