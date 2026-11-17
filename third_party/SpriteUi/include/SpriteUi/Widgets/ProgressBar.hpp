#pragma once

// Barra de progresso como modelo puro: um valor numa faixa, o preenchimento proporcional e o modo
// indeterminado. Nao desenha: o chamador desenha o trilho e o preenchimento (FillRect ou IndeterminateRect)
// com o DrawList.
//
// Regras:
//  - O valor fica sempre dentro de [minimum, maximum]. Valor fora da faixa e prendido nela.
//  - Faixa invalida (nao finita, ou maximum menor ou igual a minimum) e recusada: SetRange retorna false e
//    a faixa anterior continua valendo. Valor nao finito tambem e recusado.
//  - Fraction() vai de 0 a 1. Sem faixa valida, nunca ha fracao: o resultado e 0.
//  - FillRect() nunca passa do fim do trilho.
//  - Modo indeterminado (indeterminate = true) ignora o valor. IndeterminateRect() devolve um trecho de
//    largura fixa que anda pelo trilho; phase em [0, 1) percorre um ciclo completo e repete a cada volta.

#include "SpriteUi/Core/Math.hpp"

namespace ToolEngine::SpriteUi::Widgets {

class ProgressBarControl {
  public:
    bool indeterminate = false;

    // Define a faixa. Retorna false (sem mudar nada) se a faixa for invalida. O valor atual e prendido na
    // nova faixa.
    [[nodiscard]] bool SetRange(float minimumValue, float maximumValue) noexcept;

    // Define o valor, prendendo-o na faixa. NaN vira zero. Retorna true se o valor mudou.
    [[nodiscard]] bool SetValue(float newValue) noexcept;

    [[nodiscard]] float Value() const noexcept { return value_; }
    [[nodiscard]] float Minimum() const noexcept { return minimum_; }
    [[nodiscard]] float Maximum() const noexcept { return maximum_; }

    // Fracao concluida de 0 a 1.
    [[nodiscard]] float Fraction() const noexcept;

    // Preenchimento do modo determinado, dentro do trilho e alinhado a esquerda.
    [[nodiscard]] Rect FillRect(Rect track) const noexcept;

    // Trecho do modo indeterminado. segmentFraction e a largura do trecho em relacao ao trilho (0 a 1).
    [[nodiscard]] Rect IndeterminateRect(Rect track, float phase,
                                         float segmentFraction = 0.25f) const noexcept;

  private:
    float minimum_ = 0.0f;
    float maximum_ = 1.0f;
    float value_ = 0.0f;
};

} // namespace ToolEngine::SpriteUi::Widgets
