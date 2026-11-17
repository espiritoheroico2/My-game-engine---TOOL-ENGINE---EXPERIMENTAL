#pragma once

// Seletor de cor RGBA. A parte de modelo (ColorPickerControl) guarda a cor e aplica as regras de edicao; ela nao
// desenha e nao conhece texto de plataforma. A parte de desenho (LayoutColorPicker e BuildColorPickerItems) monta
// as areas do seletor e os itens de render. O texto dos campos HEX e RGB nao sai daqui: o chamador desenha o
// rotulo de cada campo (BuildLabelItems) dentro do retangulo que o layout devolve.
//
// Itens do seletor, de cima para baixo: area 2D de saturacao e valor (com marcador), faixa de matiz (com marcador),
// faixa de alfa sobre fundo quadriculado (com marcador), linha de amostra (original e atual, lado a lado), campo
// HEX e os quatro campos R, G, B e A. Os gradientes sao grades de celulas de cor solida: a biblioteca nao tem
// shader de gradiente.
//
// Regras do modelo:
//  - Espaco de edicao e HSV: matiz, saturacao e valor sao floats, e o alfa e float em [0, 1]. Arrastar a area
//    2D ou a faixa de matiz muda a cor sem arredondar. O arredondamento para 8 bits so acontece na exibicao
//    (Channel, ChannelText, HexText).
//  - Campos guardam inteiros 0 a 255, como o usuario digita. Valor fora da faixa e prendido na faixa.
//  - Texto invalido (letras, vazio, so sinal) nao muda o campo. Espacos nas pontas sao ignorados.
//  - HEX aceita #RRGGBB (alfa opaco) e #RRGGBBAA, em maiusculas ou minusculas. HEX invalido e rejeitado e a
//    cor atual fica.
//  - Sample() devolve a cor em 0 a 1, sem premultiplicar. Original() e a cor ao abrir o seletor: a amostra
//    mostra as duas, e Revert() volta a ela (cancelar).
//  - Controle desabilitado ignora a entrada do usuario (campos, passos, HEX e arrastes). SetColor(), SetAlpha()
//    e SetChannel() sao de programa e sempre valem.

#include "SpriteUi/Core/Handle.hpp"
#include "SpriteUi/Core/Math.hpp"
#include "SpriteUi/Rendering/DrawList.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ToolEngine::SpriteUi::Widgets {

enum class ColorChannel : std::uint8_t { Red, Green, Blue, Alpha };

// Cor no espaco de edicao: matiz em [0, 1), saturacao e valor em [0, 1].
struct HsvColor {
    float hue = 0.0f;
    float saturation = 0.0f;
    float value = 1.0f;
    [[nodiscard]] bool operator==(const HsvColor& other) const noexcept = default;
};

class ColorPickerControl {
public:
    ColorPickerControl() = default;
    explicit ColorPickerControl(Vec4 initial) noexcept;

    bool disabled = false;

    [[nodiscard]] std::uint8_t Channel(ColorChannel channel) const noexcept;
    [[nodiscard]] std::string ChannelText(ColorChannel channel) const;
    // "#RRGGBBAA" em maiusculas.
    [[nodiscard]] std::string HexText() const;
    [[nodiscard]] Vec4 Sample() const noexcept;
    [[nodiscard]] Vec4 Original() const noexcept;
    // Matiz em [0, 1) e saturacao/valor em [0, 1], para desenhar a area 2D e a faixa de matiz.
    [[nodiscard]] float Hue() const noexcept { return hsv_.hue; }
    [[nodiscard]] float Saturation() const noexcept { return hsv_.saturation; }
    [[nodiscard]] float Value() const noexcept { return hsv_.value; }
    [[nodiscard]] bool Changed() const noexcept { return current() != original_; }

    // Escreve um canal, prendendo na faixa. Retorna true se o valor mudou.
    [[nodiscard]] bool SetChannel(ColorChannel channel, int value) noexcept;
    // Digitacao no campo. Ver as regras no topo do arquivo.
    [[nodiscard]] bool SetChannelText(ColorChannel channel, std::string_view text) noexcept;
    // Seta do campo: soma delta ao valor atual. Retorna true se mudou.
    [[nodiscard]] bool Step(ColorChannel channel, int delta) noexcept;
    // Digitacao no campo HEX. Ver as regras no topo do arquivo.
    [[nodiscard]] bool SetHex(std::string_view text) noexcept;
    // Arrasto da faixa de matiz (matiz em voltas, qualquer valor finito).
    [[nodiscard]] bool DragHue(float hue) noexcept;
    // Arrasto da area 2D: saturacao e valor em [0, 1].
    [[nodiscard]] bool DragSaturationValue(float saturation, float value) noexcept;
    // Alfa em [0, 1]; fora da faixa e prendido. NaN e recusado.
    [[nodiscard]] bool SetAlpha(float alpha) noexcept;
    // Cor inteira de 0 a 1 (programa). Valores fora da faixa sao prendidos. Retorna true se mudou.
    [[nodiscard]] bool SetColor(Vec4 color) noexcept;
    // Volta a cor original (cancelar). Retorna true se mudou.
    [[nodiscard]] bool Revert() noexcept;

private:
    struct State {
        HsvColor hsv{};
        float alpha = 1.0f;
        [[nodiscard]] bool operator==(const State& other) const noexcept = default;
    };

    [[nodiscard]] State current() const noexcept { return {hsv_, alpha_}; }
    [[nodiscard]] bool SetHsv(HsvColor next) noexcept;

    HsvColor hsv_{};
    float alpha_ = 1.0f;
    State original_{};
};

// Retangulos do seletor, em coordenadas absolutas. Os campos R, G, B e A ficam numa linha; o HEX e a amostra
// (original a esquerda, atual a direita) ficam na linha de cima dos campos.
struct ColorPickerLayout {
    Rect saturationValue;
    Rect hue;
    Rect alpha;
    Rect sampleOriginal;
    Rect sampleCurrent;
    Rect hexField;
    std::array<Rect, 4> channelFields{}; // Ordem: Red, Green, Blue, Alpha.
};

// Cores do estilo sao sRGB sem premultiplicar, como as da amostra. Medidas em pixels logicos.
struct ColorPickerStyle {
    float gap = 8.0f;
    float stripHeight = 14.0f;
    float fieldHeight = 24.0f;
    float sampleWidth = 24.0f;
    float checkerSize = 6.0f;
    float markerSize = 12.0f;
    float markerBorder = 2.0f;
    Vec4 fieldFill{0.14f, 0.14f, 0.16f, 1.0f};
    Vec4 fieldBorder{0.32f, 0.32f, 0.36f, 1.0f};
    Vec4 markerFill{1.0f, 1.0f, 1.0f, 1.0f};
    Vec4 markerEdge{0.0f, 0.0f, 0.0f, 1.0f};
    Vec4 checkerLight{0.85f, 0.85f, 0.85f, 1.0f};
    Vec4 checkerDark{0.55f, 0.55f, 0.55f, 1.0f};
};

// Divide bounds nas areas do seletor. A area 2D ocupa o espaco que sobra acima das faixas e das linhas fixas.
[[nodiscard]] ColorPickerLayout LayoutColorPicker(Rect bounds, const ColorPickerStyle& style = {}) noexcept;

// Centro do marcador na area 2D (saturacao no eixo x, valor de baixo para cima), na faixa de matiz e na faixa de alfa.
[[nodiscard]] Vec2 SaturationValueMarkerCenter(const ColorPickerControl& picker, const ColorPickerLayout& layout) noexcept;
[[nodiscard]] Vec2 HueMarkerCenter(const ColorPickerControl& picker, const ColorPickerLayout& layout) noexcept;
[[nodiscard]] Vec2 AlphaMarkerCenter(const ColorPickerControl& picker, const ColorPickerLayout& layout) noexcept;

// Itens de render do seletor, sem texto. Cada campo HEX e RGB tem so o fundo e a borda; o rotulo vem do chamador.
[[nodiscard]] std::vector<Rendering::UIRenderItem> BuildColorPickerItems(const ColorPickerControl& picker,
                                                                         const ColorPickerLayout& layout,
                                                                         UIHandle owner, const ColorPickerStyle& style,
                                                                         float zOrder, std::uint64_t paintOrder);

} // namespace ToolEngine::SpriteUi::Widgets
