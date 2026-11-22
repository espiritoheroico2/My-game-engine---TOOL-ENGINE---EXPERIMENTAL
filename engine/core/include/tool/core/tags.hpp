// ToolEngine - tags internadas (string -> inteiro) e mascara de 128 bits.
#pragma once
#include "tool/core/types.hpp"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace tool {

using TagId = u32;
inline constexpr u32 kMaxTags = 128;

// Mascara de bits de 128 posicoes, uma por tag: marca quais tags um objeto possui.
struct TagMask {
  u64 lo = 0, hi = 0;
  // Liga o bit da tag t (valida limites < 128).
  void set(TagId t) { if (t < kMaxTags) (t < 64 ? lo : hi) |= 1ull << (t & 63); }
  // Desliga o bit da tag t (valida limites < 128).
  void clear(TagId t) { if (t < kMaxTags) (t < 64 ? lo : hi) &= ~(1ull << (t & 63)); }
  // True se o bit da tag t esta ligado.
  bool test(TagId t) const { return t < kMaxTags ? (((t < 64 ? lo : hi) >> (t & 63)) & 1ull) : false; }
  // True se todas as tags de 'o' tambem estao presentes nesta mascara.
  bool containsAll(const TagMask& o) const { return (lo & o.lo) == o.lo && (hi & o.hi) == o.hi; }
  // True se pelo menos uma tag esta ligada.
  bool any() const { return lo || hi; }
  // Compara as duas mascaras bit a bit.
  friend bool operator==(const TagMask& a, const TagMask& b) { return a.lo == b.lo && a.hi == b.hi; }
};

// Registro global de nomes de tags: converte string em TagId estavel e vice-versa.
class TagRegistry {
public:
  // Retorna o id da tag, criando-a se necessario. kInvalidIndex se passar de 128.
  TagId intern(std::string_view name) {
    if (auto it = ids_.find(std::string(name)); it != ids_.end()) return it->second;
    if (names_.size() >= kMaxTags) return kInvalidIndex;
    const TagId id = static_cast<TagId>(names_.size());
    names_.emplace_back(name);
    ids_.emplace(names_.back(), id);
    return id;
  }
  // Nome da tag id, ou string vazia se o id nao existir.
  std::string_view name(TagId id) const { return id < names_.size() ? std::string_view(names_[id]) : std::string_view(); }
  // Quantidade de tags registradas.
  size_t size() const { return names_.size(); }
  // Procura sem criar (kInvalidIndex se nao existir).
  TagId find(std::string_view name) const { auto it = ids_.find(std::string(name)); return it == ids_.end() ? kInvalidIndex : it->second; }
  // Renomeia uma tag EM TODOS os objetos (eles guardam o id, nao o nome). Recusa nome vazio, longo ou ja existente.
  bool rename(TagId id, std::string_view newName) {
    if (id >= names_.size() || newName.empty() || newName.size() > 40) return false;
    const std::string n(newName);
    if (ids_.count(n)) return false;
    ids_.erase(names_[id]); names_[id] = n; ids_.emplace(n, id);
    return true;
  }

private:
  std::unordered_map<std::string, TagId> ids_;
  std::vector<std::string> names_;
};

} // namespace tool
