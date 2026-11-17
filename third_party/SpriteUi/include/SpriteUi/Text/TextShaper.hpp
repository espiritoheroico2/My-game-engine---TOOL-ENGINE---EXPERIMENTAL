#pragma once

// Interface de shaping: transforma UTF-8 em glifos (GlyphInstance) que o layout consome.
//
// Por que existe: LayoutText recebe glifos prontos e nao sabe de onde vieram. Quem decide quais glifos saem do
// texto (um por codepoint, ou com kerning, ligaduras e marcas, como faz o HarfBuzz) e o shaper. Trocar o shaper
// nao muda quebra de linha, alinhamento, caret nem os itens de desenho.
//
// Exemplo de jogo: um jogo em arabe ou hindi precisa de shaping real para que as letras se liguem. Ao plugar um
// shaper baseado em HarfBuzz, o HUD continua usando o mesmo LayoutText e o mesmo GenerateTextRenderItems.
//
// Limitacoes do shaper padrao (CodepointShaper): um glifo por codepoint, sem kerning, ligaduras, marcas
// combinantes ou reordenacao bidi. Escritas como arabe, hebraico, devanagari e tailandes nao saem corretas com ele.
// Para elas, implemente ITextShaper com HarfBuzz (fase 2).

#include "SpriteUi/Text/GlyphInstance.hpp"
#include "SpriteUi/Text/TruetypeAtlas.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace ToolEngine::SpriteUi::Text {

class ITextShaper {
public:
    virtual ~ITextShaper() = default;

    // Glifos na ordem do texto. Cada um traz clusterByteOffset (byte UTF-8 de origem) e glyphId. O glyphId deve
    // ser o indice de glifo da fonte, resolvido no atlas: nunca o codepoint Unicode usado como indice.
    [[nodiscard]] virtual std::vector<GlyphInstance> Shape(std::string_view utf8,
                                                           const TruetypeAtlas& atlas) const = 0;
};

// Shaper padrao: um glifo por codepoint, a mesma regra de ShapeTruetype. Use para texto latino, cirílico e grego
// simples, em que essa regra basta. Ver as limitacoes no topo do arquivo.
class CodepointShaper final : public ITextShaper {
public:
    [[nodiscard]] std::vector<GlyphInstance> Shape(std::string_view utf8, const TruetypeAtlas& atlas) const override;
};

} // namespace ToolEngine::SpriteUi::Text
