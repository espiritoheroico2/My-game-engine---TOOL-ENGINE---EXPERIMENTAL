#pragma once

// Dica de ferramenta como modelo puro: atraso antes de aparecer, posicao junto ao ponteiro e sempre dentro
// do canvas. Nao desenha e nao le relogio: o chamador informa o tempo em segundos a cada chamada.
//
// Regras:
//  - A dica so aparece depois de showDelay segundos com o ponteiro sobre o mesmo alvo.
//  - Trocar de alvo reinicia o atraso. Sair do alvo esconde na hora.
//  - A posicao e abaixo e a direita do ponteiro. Se nao cabe embaixo, sobe. Em qualquer caso, fica dentro do
//    canvas; uma dica maior que o canvas fica presa ao canto superior esquerdo.
//  - A posicao e fixada quando a dica aparece: mover o ponteiro sobre o mesmo alvo nao a desloca.
//  - Tick() informa so as transicoes de visibilidade, para o chamador redesenhar apenas quando muda.

#include "SpriteUi/Core/Math.hpp"

#include <cstdint>
#include <optional>

namespace ToolEngine::SpriteUi::Widgets {

struct TooltipConfig {
    float showDelay = 0.5f;          // Segundos sobre o alvo ate a dica aparecer.
    Vec2 offset{12.0f, 16.0f};       // Distancia do ponteiro ao canto da dica.
};

// Retangulo da dica para um ponteiro, o tamanho do texto e o canvas. Ver as regras no topo do arquivo.
[[nodiscard]] Rect PlaceTooltip(Vec2 anchor, Vec2 size, Rect canvas, Vec2 offset) noexcept;

class TooltipControl {
public:
    explicit TooltipControl(TooltipConfig config = {}) noexcept : config_{config} {}

    // Ponteiro sobre o alvo. Um alvo novo reinicia o atraso e fixa a ancora no ponteiro. Continuar sobre o
    // mesmo alvo nao muda nada.
    void Enter(std::uint64_t target, Vec2 pointer, float now) noexcept;

    // Ponteiro saiu do alvo, clicou ou pressionou tecla: esconde na hora.
    void Leave() noexcept;

    [[nodiscard]] bool HasTarget() const noexcept { return hasTarget_; }
    [[nodiscard]] std::uint64_t Target() const noexcept { return target_; }
    [[nodiscard]] Vec2 Anchor() const noexcept { return anchor_; }
    [[nodiscard]] bool Visible(float now) const noexcept;

    // Instante (em segundos) em que a dica deve aparecer, para o chamador acordar so nessa hora.
    [[nodiscard]] std::optional<float> ShowAt() const noexcept;

    // Atualiza o estado de visibilidade para `now`. Retorna true so quando a visibilidade mudou desde a
    // ultima chamada (aparecer ou sumir).
    [[nodiscard]] bool Tick(float now) noexcept;

private:
    TooltipConfig config_;
    std::uint64_t target_ = 0;
    Vec2 anchor_{};
    float hoverStart_ = 0.0f;
    bool hasTarget_ = false;
    bool shown_ = false;
};

} // namespace ToolEngine::SpriteUi::Widgets
