// ToolEngine editor - politica de remocao do cache de malhas de chunks (F-08).
// Funcoes puras, sem ImGui, GPU ou cena: podem ser testadas isoladamente.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace editor::chunkcache {

// Quantas entradas remover quando o cache tem `size` entradas e o teto e `max`.
// Acima do teto, desce ate max - max/8 (histerese de ~12,5%): assim uma remocao libera um lote
// e nao uma entrada a cada quadro. max == 0 nao retem nada.
inline size_t evictionCount(size_t size, size_t max) {
  if (max == 0) return size;
  if (size <= max) return 0;
  const size_t target = max - max / 8;
  return size - target;
}

// Chaves das `count` entradas menos usadas recentemente (menor lastUsed).
// Empates: a ordem entre entradas de mesmo lastUsed nao e garantida.
template <class K>
std::vector<K> leastRecent(std::vector<std::pair<K, uint64_t>> entries, size_t count) {
  std::vector<K> out;
  if (count == 0 || entries.empty()) return out;
  if (count >= entries.size()) count = entries.size();
  if (count < entries.size()) {
    std::nth_element(entries.begin(), entries.begin() + static_cast<std::ptrdiff_t>(count), entries.end(),
                     [](const auto& a, const auto& b) { return a.second < b.second; });
  }
  out.reserve(count);
  for (size_t i = 0; i < count; ++i) out.push_back(entries[i].first);
  return out;
}

} // namespace editor::chunkcache
