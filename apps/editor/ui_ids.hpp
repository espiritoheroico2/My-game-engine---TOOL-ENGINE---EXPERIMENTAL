#pragma once

// Ids dos elementos de UI: cada elemento recebe um id unico quando e criado. O id vale para o jogo inteiro (nao so
// para o canvas), tem letras e numeros e nunca e reaproveitado durante a sessao.
//
// Formato: "e" + numero em base 36 (0-9 e a-z). Exemplos: "e1", "e2", "ea", "e10".
//
// Como funciona:
//   next:     devolve o proximo id livre e avanca o contador.
//   parse:    numero de um id gerado, ou 0 quando o texto nao segue o formato.
//   observe:  depois de carregar uma cena, garante que os novos ids nao repetem os que ja existem.

#include <cstdint>
#include <limits>
#include <string>

namespace editor::ui_ids {

inline std::uint64_t gNext = 1;   // proximo numero a usar; cresce sempre, nunca volta

[[nodiscard]] inline std::string make(std::uint64_t n) {
  static const char kDigits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
  std::string s;
  do {
    s.insert(s.begin(), kDigits[n % 36]);
    n /= 36;
  } while (n != 0);
  return "e" + s;
}

// Numero de um id no formato "e<base36>", ou 0 quando nao segue o formato (vazio, sem "e", caractere invalido, estouro).
[[nodiscard]] inline std::uint64_t parse(const std::string& id) {
  if (id.size() < 2 || id[0] != 'e') return 0;
  std::uint64_t v = 0;
  for (std::size_t i = 1; i < id.size(); ++i) {
    const char c = id[i];
    std::uint64_t d = 0;
    if (c >= '0' && c <= '9') d = static_cast<std::uint64_t>(c - '0');
    else if (c >= 'a' && c <= 'z') d = static_cast<std::uint64_t>(c - 'a' + 10);
    else return 0;
    if (v > (std::numeric_limits<std::uint64_t>::max() - d) / 36) return 0;
    v = v * 36 + d;
  }
  return v;
}

[[nodiscard]] inline std::string next() { return make(gNext++); }

inline void observe(const std::string& id) {
  const std::uint64_t n = parse(id);
  if (n >= gNext) gNext = n + 1;
}

}  // namespace editor::ui_ids
