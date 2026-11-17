#pragma once

// Presets de estilo com nome fixo: Classic, Modern e Effects. Cada preset define a forma (raio de canto,
// borda, sombra, duracao e easing da animacao, flags de cabecalho com gradiente e icones) e a paleta (cores
// principais).
//
// Forma e paleta sao separadas de proposito. ApplyPalette troca so as cores e mantem a forma: um tema novo
// nao muda os cantos nem a sombra. Esta unidade nao escreve nada em disco e nao desenha; so entrega valores.
//
// Cores: RGBA linear, mesmo formato de Style.hpp.
// Tipo invalido de StyleKind (valor fora do enum) cai no preset Modern. GetPreset devolve o kind efetivo.

#include "SpriteUi/Core/Math.hpp"

#include <cstdint>

namespace ToolEngine::SpriteUi::Widgets {

enum class StyleKind : std::uint8_t { Classic, Modern, Effects };

enum class AnimationEasing : std::uint8_t {
    Linear,  // Progresso constante.
    EaseOut, // Desacelera no fim (saida suave).
};

// Sombra. Cor com alfa zero e desfoque zero significam "sem sombra".
struct ShadowStyle {
    Vec2 offset{0.0f, 0.0f}; // Deslocamento em pixels (y positivo desce).
    float blur = 0.0f;       // Raio do desfoque em pixels; zero = sombra dura ou ausente.
    Vec4 color{0.0f, 0.0f, 0.0f, 0.0f};
};

// Cores principais. Trocadas por ApplyPalette.
struct StylePalette {
    Vec4 background{0.12f, 0.12f, 0.14f, 1.0f}; // Fundo do painel.
    Vec4 header{0.18f, 0.18f, 0.22f, 1.0f};     // Cabecalho / barra de titulo.
    Vec4 text{0.92f, 0.92f, 0.94f, 1.0f};       // Texto.
    Vec4 border{0.35f, 0.35f, 0.38f, 1.0f};     // Borda.
};

struct StylePreset {
    StyleKind kind = StyleKind::Modern;

    // Forma.
    float cornerRadius = 0.0f; // Raio dos cantos em pixels; zero = retos.
    float borderWidth = 1.0f;  // Largura da borda em pixels.
    ShadowStyle shadow{};
    float animationMs = 0.0f; // Duracao da animacao de hover e de botao; zero = sem animacao.
    AnimationEasing easing = AnimationEasing::Linear;
    bool headerGradient = false; // Gradiente/brilho no cabecalho.
    bool icons = false;          // Icones habilitados. A implementacao dos icones nao esta nesta unidade.

    // Cores.
    StylePalette palette{};
};

// Nome fixo do estilo ("Classic", "Modern", "Effects"). Valor invalido devolve "Modern".
[[nodiscard]] const char* StyleKindName(StyleKind kind) noexcept;

// Preset do estilo com a paleta padrao. Valor invalido devolve o preset Modern.
[[nodiscard]] StylePreset GetPreset(StyleKind kind) noexcept;

// Copia o preset trocando so a paleta. Forma, tipo e flags nao mudam.
[[nodiscard]] StylePreset ApplyPalette(const StylePreset& preset, const StylePalette& palette) noexcept;

} // namespace ToolEngine::SpriteUi::Widgets
