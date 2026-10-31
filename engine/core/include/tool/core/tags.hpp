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

struct TagMask {
  u64 lo = 0, hi = 0;
  void set(TagId t) { (t < 64 ? lo : hi) |= 1ull << (t & 63); }
  void clear(TagId t) { (t < 64 ? lo : hi) &= ~(1ull << (t & 63)); }
  bool test(TagId t) const { return ((t < 64 ? lo : hi) >> (t & 63)) & 1ull; }
  bool containsAll(const TagMask& o) const { return (lo & o.lo) == o.lo && (hi & o.hi) == o.hi; }
  bool any() const { return lo || hi; }
  friend bool operator==(const TagMask& a, const TagMask& b) { return a.lo == b.lo && a.hi == b.hi; }
};

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
  std::string_view name(TagId id) const { return id < names_.size() ? std::string_view(names_[id]) : std::string_view(); }
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
