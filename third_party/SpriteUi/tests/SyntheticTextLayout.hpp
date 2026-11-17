#pragma once

// Layout de texto sintetico para os testes de widgets de texto: um glifo por byte, avanco fixo e caixa de
// glifo com area (sem ela o gerador de itens descarta o glifo). Nao depende de fonte nem de stb.

#include "SpriteUi/Text/TextLayout.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace SyntheticText {

namespace Text = ToolEngine::SpriteUi::Text;

// Avanco de 500 unidades: com fontSize 20 cada caractere ocupa 10 px. Com as metricas padrao, a linha mede
// 0,4 px nesse tamanho; lineHeightMultiplier aumenta o espacamento entre linhas (50 da 20 px).
inline Text::TextLayoutResult Layout(const std::string& text, float fontSize = 20.0f, float maxWidth = 0.0f,
                                     Text::TextWrap wrap = Text::TextWrap::None,
                                     float lineHeightMultiplier = 1.0f) {
    std::vector<Text::GlyphInstance> glyphs;
    glyphs.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        Text::GlyphInstance g;
        g.glyphId = static_cast<std::uint32_t>(static_cast<unsigned char>(text[i]));
        g.clusterByteOffset = static_cast<std::uint32_t>(i);
        g.xAdvance = 500.0f;
        g.planeBounds[0] = 50.0f;
        g.planeBounds[1] = -200.0f;
        g.planeBounds[2] = 450.0f;
        g.planeBounds[3] = 800.0f;
        g.uvRect[2] = 0.1f;
        g.uvRect[3] = 0.1f;
        glyphs.push_back(g);
    }
    Text::FontMetrics metrics;
    Text::TextLayoutConfig cfg;
    cfg.fontSize = fontSize;
    cfg.maxWidth = maxWidth;
    cfg.wrap = wrap;
    cfg.lineHeightMultiplier = lineHeightMultiplier;
    return Text::LayoutText(text, glyphs, metrics, cfg);
}

} // namespace SyntheticText
