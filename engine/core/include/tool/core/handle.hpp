// ToolEngine - handles com geracao (id + generation).
// Um handle antigo NUNCA aponta para um objeto novo que reaproveitou o slot.
#pragma once
#include "tool/core/types.hpp"

#include <vector>

namespace tool {

// Referencia segura a um objeto: indice do slot + geracao. Use kNullHandle para 'sem objeto'.
struct Handle {
  u32 index = kInvalidIndex;
  u32 generation = 0;

  // True se o handle e o nulo (indice invalido).
  constexpr bool isNull() const { return index == kInvalidIndex; }
  // Dois handles sao iguais se apontam para o mesmo slot na mesma geracao.
  friend constexpr bool operator==(Handle a, Handle b) { return a.index == b.index && a.generation == b.generation; }
  // Negacao de operator==.
  friend constexpr bool operator!=(Handle a, Handle b) { return !(a == b); }
};

// Handle nulo padrao (indice kInvalidIndex, geracao 0).
inline constexpr Handle kNullHandle{};

// Aloca indices reutilizaveis; cada destruicao incrementa a geracao do slot.
class HandleAllocator {
public:
  // Cria um objeto: reaproveita um slot livre (com a geracao atual) ou cresce o vetor.
  Handle create() {
    if (!free_.empty()) {
      const u32 i = free_.back();
      free_.pop_back();
      alive_[i] = true;
      ++liveCount_;
      return {i, gen_[i]};
    }
    gen_.push_back(0);
    alive_.push_back(true);
    ++liveCount_;
    return {static_cast<u32>(gen_.size() - 1), 0};
  }

  // True se o handle ainda e valido: slot vivo e geracao igual a do handle.
  bool alive(Handle h) const { return h.index < gen_.size() && alive_[h.index] && gen_[h.index] == h.generation; }

  // Libera o objeto do handle e incrementa a geracao do slot. False se o handle ja nao estava vivo.
  bool destroy(Handle h) {
    if (!alive(h)) return false;
    alive_[h.index] = false;
    ++gen_[h.index];
    free_.push_back(h.index);
    --liveCount_;
    return true;
  }

  // Invalida handles antigos mantendo o slot vivo (usado pelo DeleteAndReuse).
  Handle renew(Handle h) {
    if (!alive(h)) return kNullHandle;
    ++gen_[h.index];
    return {h.index, gen_[h.index]};
  }

  // True se o slot de indice i esta ocupado (sem checar geracao).
  bool aliveIndex(u32 i) const { return i < gen_.size() && alive_[i]; }
  // Geracao atual do slot i. Pre-condicao: i < capacity().
  u32 generationOf(u32 i) const { return gen_[i]; }

  // Quantidade de objetos vivos.
  size_t liveCount() const { return liveCount_; }
  // Quantidade de slots ja alocados (vivos e livres).
  size_t capacity() const { return gen_.size(); }

private:
  std::vector<u32> gen_;
  std::vector<bool> alive_;
  std::vector<u32> free_;
  size_t liveCount_ = 0;
};

} // namespace tool
