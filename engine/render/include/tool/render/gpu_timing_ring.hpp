#pragma once
// Anel de tempos de GPU por quadro (docs/planos/02_RENDER_DRAW_CALLS.md 5.4 e 5.5). Puro: o backend grava o tempo (em ms) quando as queries
// resolvem. Um quadro so e lido depois de `latency` quadros, para nao bloquear a CPU. Tempo invalido nao entra.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace tool::render {

// Anel de tempos de GPU: guarda o tempo de cada quadro ate ele poder ser lido sem bloquear a CPU.
class GpuTimingRing {
 public:
  // latency = quantos quadros esperar antes de ler um tempo (3 por padrao; 0 vira 1).
  explicit GpuTimingRing(std::size_t latency = 3) : slots_(latency == 0 ? 1 : latency) {}

  // Guarda o tempo do quadro. NaN, infinito ou negativo sao ignorados.
  void record(std::uint64_t frameId, float ms) {
    if (!std::isfinite(ms) || ms < 0.f) return;
    Slot& s = slots_[frameId % slots_.size()];
    s.frame = frameId;
    s.ms = ms;
    s.valid = true;
  }

  // Devolve true e o tempo quando o quadro ja foi gravado e ainda nao foi sobrescrito por um quadro mais novo.
  // `current` e o quadro em curso na CPU; o quadro so fica disponivel depois de `latency` quadros.
  [[nodiscard]] bool query(std::uint64_t frameId, std::uint64_t current, float& outMs) const {
    if (current < frameId + slots_.size()) return false;
    const Slot& s = slots_[frameId % slots_.size()];
    if (!s.valid || s.frame != frameId) return false;
    outMs = s.ms;
    return true;
  }

 private:
  // Um quadro guardado: id, tempo em ms e se o dado e valido.
  struct Slot {
    std::uint64_t frame = 0;
    float ms = 0.f;
    bool valid = false;
  };
  // Anel de slots, indexado por frameId % tamanho.
  std::vector<Slot> slots_;
};

}  // namespace tool::render
