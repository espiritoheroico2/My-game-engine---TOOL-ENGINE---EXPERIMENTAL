#pragma once

// Imagem (Image) e botao de imagem (ImageButton): um retangulo com uma regiao de textura, um modo de ajuste e,
// opcionalmente, nine-slice. O botao de imagem e uma imagem com a maquina de estados do botao.
//
// Partes: a fonte (ImageSource: slice da textura, regiao UV, tamanho em pixels, modo de ajuste e nine-slice) e o
// desenho dentro da area de conteudo. Se a textura nao estiver carregada (loaded = false), o elemento desenha um
// marcador de erro (fundo magenta com um X), para o usuario saber que algo falta.
//
// Regiao UV: u, v, largura e altura em [0, 1]. Valores fora disso sao limitados: a regiao nunca sai da textura.
//
// Modos de ajuste (ImageFit):
//  - Esticar (Stretch): a imagem ocupa toda a area; pode distorcer.
//  - Conter (Contain): a proporcao e preservada e a imagem cabe inteira, centrada; pode sobrar faixa.
//  - Preencher (Fill): a imagem cobre a area sem distorcer; o que sobra e cortado (a regiao UV e reduzida).
// Com nine-slice ligado, a imagem e esticada pelas bordas (os cantos mantem o tamanho) e o modo de ajuste nao
// se aplica. Tamanho da textura zero significa desconhecido: a imagem e esticada.
//
// Botao de imagem: a cor da imagem muda com o estado (pressionado escurece, desabilitado fica translucido) e um
// contorno aparece com foco. Click segue a regra do botao (Down e Up dentro da area). Enter ou Espaco, com foco,
// ativam (KeyActivate).
//
// Exemplo:
//   auto id = tree.Create<Image>(parent);
//   auto* img = tree.GetAs<Image>(id);
//   img->source.textureSlice = atlasSlice;
//   img->source.textureSize = {600.0f, 600.0f};
//   img->source.fit = ImageFit::Contain;

#include "SpriteUi/Core/Math.hpp"
#include "SpriteUi/Elements/Element.hpp"
#include "SpriteUi/Elements/PointerEvent.hpp"
#include "SpriteUi/Elements/PressGesture.hpp"
#include "SpriteUi/Rendering/DrawList.hpp"
#include "SpriteUi/Rendering/NineSlice.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace ToolEngine::SpriteUi::Elements {

enum class ImageFit : std::uint8_t { Stretch, Contain, Fill };

struct ImageSource {
    std::uint32_t textureSlice = Rendering::WhiteTextureSlice;
    float uv[4]{0.0f, 0.0f, 1.0f, 1.0f};  // u, v, largura, altura da regiao na textura.
    Vec2 textureSize{0.0f, 0.0f};         // Tamanho da textura em pixels; zero = desconhecido.
    bool loaded = true;                   // false: textura ausente, desenha o marcador de erro.
    ImageFit fit = ImageFit::Stretch;
    bool nineSlice = false;
    Rendering::NineSliceBorders borders{};  // Em texels da textura; usado com nineSlice.
};

struct ImageStyle {
    Vec4 missingFill{0.8f, 0.1f, 0.8f, 1.0f};
    Vec4 missingMark{0.05f, 0.05f, 0.05f, 1.0f};
    Vec4 focusRing{0.95f, 0.8f, 0.3f, 1.0f};
    float markThickness = 2.0f;
    float focusRingWidth = 2.0f;
    float pressedTint = 0.7f;    // Multiplicador da cor com o botao pressionado.
    float disabledTint = 0.4f;   // Multiplicador da cor com o botao desabilitado.
};

class Image final : public Element {
  public:
    ImageSource source;
    Vec4 tint{1.0f, 1.0f, 1.0f, 1.0f};  // Cor multiplicadora (premultiplicada, linear).
};

class ImageButton final : public Element {
  public:
    ImageSource source;
    Vec4 tint{1.0f, 1.0f, 1.0f, 1.0f};

    // Trata o ponteiro. Retorna true quando o clique completa sobre o botao.
    [[nodiscard]] bool OnPointer(const PointerEvent& event) noexcept;

    // Enter ou Espaco com foco: true se o botao deve ativar (habilitado e com foco).
    [[nodiscard]] bool KeyActivate() const noexcept;

  private:
    PressGesture gesture_;
};

// Retangulo onde a imagem aparece dentro de content, pelo modo de ajuste. Stretch e Fill usam content inteiro.
[[nodiscard]] Rect ImageDrawRect(Rect content, Vec2 textureSize, ImageFit fit) noexcept;

// Regiao UV efetiva: limitada a [0, 1]; no modo Fill, reduzida para a proporcao da area (corte centrado).
[[nodiscard]] std::array<float, 4> ImageVisibleUv(Rect content, const ImageSource& source) noexcept;

// Itens de uma fonte de imagem dentro de content, com cor tint. Textura ausente vira marcador de erro.
[[nodiscard]] std::vector<Rendering::UIRenderItem> BuildImageSourceItems(UIHandle owner, Rect content,
                                                                         const ImageSource& source, Vec4 tint,
                                                                         const ImageStyle& style,
                                                                         std::uint32_t clipIndex, float zOrder,
                                                                         std::uint64_t paintOrder);

// Itens da imagem (area de conteudo, cor e fonte do elemento).
[[nodiscard]] std::vector<Rendering::UIRenderItem> BuildImageItems(const Image& image, const ImageStyle& style,
                                                                   std::uint32_t clipIndex, float zOrder,
                                                                   std::uint64_t paintOrder);

// Itens do botao de imagem: a cor muda com o estado (pressionado, desabilitado) e ganha contorno com foco.
[[nodiscard]] std::vector<Rendering::UIRenderItem> BuildImageButtonItems(const ImageButton& button,
                                                                         const ImageStyle& style,
                                                                         std::uint32_t clipIndex, float zOrder,
                                                                         std::uint64_t paintOrder);

} // namespace ToolEngine::SpriteUi::Elements
