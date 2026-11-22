#pragma once
// Cache de estado de bind por slot (udc.md 3.8 e 9.2). Puro, sem RHI: o backend decide o que fazer com a resposta.
// Regras: mesmo id no mesmo slot = sem bind repetido; id novo = bind; slot fora do cache = sempre bind (nunca erra);
// invalidar (troca de contexto, operacao externa, reset do passe) forca o proximo bind de cada slot.
#include <cstddef>
#include <cstdint>
#include <vector>

namespace tool::render {

class BindStateCache {
 public:
  static constexpr std::uint64_t kUnknown = ~0ull;

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

  void invalidateSlot(std::size_t slot) {
    if (slot < bound_.size()) bound_[slot] = kUnknown;
  }

  void invalidateAll() {
    for (std::uint64_t& v : bound_) v = kUnknown;
  }

  [[nodiscard]] std::uint64_t binds() const { return binds_; }
  [[nodiscard]] std::uint64_t skipped() const { return skipped_; }
  void resetCounters() {
    binds_ = 0;
    skipped_ = 0;
  }

 private:
  std::vector<std::uint64_t> bound_;
  std::uint64_t binds_ = 0;
  std::uint64_t skipped_ = 0;
};

}  // namespace tool::render
