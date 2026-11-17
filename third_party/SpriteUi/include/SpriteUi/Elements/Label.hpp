#pragma once

// Rotulo: um texto de uma linha alinhado dentro da area util (content rect) do elemento.
//
// Dados e apresentacao sao separados. Label guarda o texto e o alinhamento. O layout do texto (shaping e
// medida) e feito pelo chamador com uma fonte real (ver MakeLabelLayoutConfig); BuildLabelItems so posiciona
// esse layout e gera os itens de desenho. Assim o rotulo nao conhece atlas nem arquivo de fonte.
//
// Recorte: o texto que nao cabe na area util e recortado pelo clip informado; nunca empurra o layout.

#include "SpriteUi/Core/Math.hpp"
#include "SpriteUi/Elements/Element.hpp"
#include "SpriteUi/Rendering/DrawList.hpp"
#include "SpriteUi/Text/TextLayout.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace ToolEngine::SpriteUi::Elements {

enum class HorizontalAlign : std::uint8_t { Left, Center, Right };
enum class VerticalAlign : std::uint8_t { Top, Middle, Bottom };

// Onde e como os glifos sao gerados. O chamador escolhe a fonte (slice do atlas) e os flags.
struct TextTarget {
    std::uint32_t textureSlice = Rendering::WhiteTextureSlice;
    std::uint32_t renderFlags = 0u; // 0 = atlas de alfa.
    float zOrder = 0.5f;
    std::uint64_t paintOrder = 0;   // Base; cada glifo sobe um.
};

class Label final : public Element {
  public:
    std::string text;
    Vec4 color{1.0f, 1.0f, 1.0f, 1.0f}; // RGBA linear premultiplicado.
    float fontSize = 16.0f;             // Usado pelo chamador ao montar o layout do texto.
    HorizontalAlign horizontal = HorizontalAlign::Left;
    VerticalAlign vertical = VerticalAlign::Middle;
};

// Configuracao de layout do rotulo: uma linha, sem quebra, no tamanho do rotulo.
[[nodiscard]] Text::TextLayoutConfig MakeLabelLayoutConfig(const Label& label) noexcept;

// Itens de desenho do texto, alinhados dentro de label.ContentRect(). Se o texto nao cabe, sai recortado pelo
// clipIndex (que o chamador cria para a area util). Sem layout valido ou area nao finita, nao ha itens.
[[nodiscard]] std::vector<Rendering::UIRenderItem> BuildLabelItems(const Label& label,
                                                                   const Text::TextLayoutResult& layout,
                                                                   Rect content, const TextTarget& target,
                                                                   std::uint32_t clipIndex);

} // namespace ToolEngine::SpriteUi::Elements
