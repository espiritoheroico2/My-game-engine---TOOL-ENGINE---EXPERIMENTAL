#pragma once
// Cache de estado de bind por slot (udc.md 3.8 e 9.2). Puro, sem RHI: o backend decide o que fazer com a resposta.
// Regras: mesmo id no mesmo slot = sem bind repetido; id novo = bind; slot fora do cache = sempre bind (nunca erra);
// invalidar (troca de contexto, operacao externa, reset do passe) forca o proximo bind de cada slot.
#include <cstddef>
#include <cstdint>
#include <vector>

namespace tool::render {

// Cache de bind por slot: decide se um bind precisa ser emitido. Puro, sem RHI.
class BindStateCache {
 public:
  // Id que significa 'nao sei o que esta ligado': sempre gera bind.
  static constexpr std::uint64_t kUnknown = ~0ull;

  // slots = quantidade de slots controlados (16 por padrao). Slots fora dessa faixa sempre fazem bind.
  explicit BindStateCache(std::size_t slots = 16) : bound_(slots, kUnknown) {}

  // true quando o bind deve ser emitido.
  [[nodiscard]] bool needsBind(std::size_t slot, std::uint64_t id) {
    if (slot >= bound_.size() || id == kUnknown) {
      ++binds_;
      return true;
    }
    if (bound_[slot] == id) {
      ++skipped_;
      return false;
    }
    bound_[slot] = id;
    ++binds_;
    return true;
  }

  // Esquece o id de um slot: o proximo bind desse slot sera emitido.
  void invalidateSlot(std::size_t slot) {
    if (slot < bound_.size()) bound_[slot] = kUnknown;
  }

  // Esquece todos os slots (troca de contexto, operacao externa ou reset do passe).
  void invalidateAll() {
    for (std::uint64_t& v : bound_) v = kUnknown;
  }

  // Quantidade de binds emitidos desde o ultimo resetCounters().
  [[nodiscard]] std::uint64_t binds() const { return binds_; }
  // Quantidade de binds evitados (mesmo id ja ligado no slot).
  [[nodiscard]] std::uint64_t skipped() const { return skipped_; }
  // Zera os contadores de binds e de binds evitados; o estado dos slots nao muda.
  void resetCounters() {
    binds_ = 0;
    skipped_ = 0;
  }

 private:
  // Id ligado em cada slot (kUnknown = nada conhecido).
  std::vector<std::uint64_t> bound_;
  // Contadores de estatistica: binds emitidos e evitados.
  std::uint64_t binds_ = 0;
  std::uint64_t skipped_ = 0;
};

}  // namespace tool::render
