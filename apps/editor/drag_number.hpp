// Widget numerico de arraste (F2): logica pura, sem SDL e sem desenho, reutilizada por todo campo numerico do editor.
//
// Uso: o usuario aperta o mouse DENTRO do campo e arrasta para a direita (valor sobe) ou para a esquerda (valor
// desce). Um clique sem arraste (movimento menor que kStartPx) nao muda nada: o chamador abre o campo como texto.
// O passo por pixel vem das casas decimais do campo (2 casas = 0.01 por pixel); Shift divide por 10 (fino) e Ctrl
// multiplica por 10 (grosso). O valor sempre respeita [min, max] do campo.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace editor::drag_number {

// Movimento minimo (em pixels) para o clique virar arraste.
inline constexpr float kStartPx = 3.f;

// Passo base por pixel: 10^-casas (0 casas = 1, 1 casa = 0.1, 2 casas = 0.01...).
[[nodiscard]] inline float StepFor(int decimals) noexcept {
  return std::pow(10.f, -static_cast<float>(std::clamp(decimals, 0, 6)));
}

// Multiplicador do passo: Shift = fino (x0.1), Ctrl = grosso (x10), os dois juntos se anulam.
[[nodiscard]] inline float StepMultiplier(bool shift, bool ctrl) noexcept {
  if (shift && !ctrl) return 0.1f;
  if (ctrl && !shift) return 10.f;
  return 1.f;
}

// Arredonda para `digits` casas (evita ruido de ponto flutuante como 0.30000001 no valor gravado).
[[nodiscard]] inline float Quantize(float v, int digits) noexcept {
  const float k = std::pow(10.f, static_cast<float>(std::clamp(digits, 0, 6)));
  return std::round(v * k) / k;
}

// Estado de um arraste em curso (um campo por vez).
class DragNumber {
 public:
  // Mouse apertado sobre o campo `key` (x em pixels) com o valor atual do campo.
  void Press(std::uint32_t key, float x, float value) noexcept {
    key_ = key;
    startX_ = lastX_ = x;
    acc_ = value;
    pending_ = true;
    dragging_ = false;
  }

  // Mouse movido. Devolve true quando o arraste produziu um valor novo em `out` (ja prendido em [min, max]).
  // Antes de passar de kStartPx continua sendo "so um clique" e nao devolve nada.
  [[nodiscard]] bool Move(float x, bool shift, bool ctrl, int decimals, float min, float max, float& out) noexcept {
    if (!pending_) return false;
    if (!dragging_) {
      if (std::fabs(x - startX_) < kStartPx) return false;
      dragging_ = true;
      lastX_ = startX_;   // o primeiro movimento conta desde o ponto de apoio
    }
    const float dx = x - lastX_;
    lastX_ = x;
    if (dx == 0.f) return false;
    acc_ = std::clamp(acc_ + dx * StepFor(decimals) * StepMultiplier(shift, ctrl), min, max);
    out = std::clamp(Quantize(acc_, decimals + 1), min, max);
    return true;
  }

  // Mouse solto. Devolve true se foi um clique comum (sem arraste): o campo deve abrir como texto.
  bool Release() noexcept {
    const bool wasClick = pending_ && !dragging_;
    pending_ = dragging_ = false;
    key_ = 0;
    return wasClick;
  }

  // Cancela sem efeito (perda de foco, troca de objeto).
  void Cancel() noexcept { pending_ = dragging_ = false; key_ = 0; }

  [[nodiscard]] bool Pending() const noexcept { return pending_; }
  [[nodiscard]] bool Dragging() const noexcept { return dragging_; }
  [[nodiscard]] std::uint32_t Key() const noexcept { return key_; }
  // x do Press (para posicionar o caret quando o clique vira edicao de texto).
  [[nodiscard]] float StartX() const noexcept { return startX_; }

 private:
  std::uint32_t key_ = 0;
  float startX_ = 0.f, lastX_ = 0.f, acc_ = 0.f;
  bool pending_ = false, dragging_ = false;
};

}  // namespace editor::drag_number
