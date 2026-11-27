#pragma once

// Quebra de linha de texto do Canvas, sem janela e sem fonte: quem chama passa a funcao que mede o texto.
// A regra e: quebra nos espacos (palavras inteiras); "\n" no texto forca uma linha nova; uma palavra maior que a
// largura e partida em pedacos que cabem (sem cortar um caractere UTF-8 no meio).
//
// Exemplo: largura 50 e medida de 10 por letra: "aaa bbb ccc" vira {"aaa", "bbb", "ccc"} (cada linha com 3 letras
// cabe, mas "aaa bbb" tem 7 letras = 70 > 50). Com largura 80 vira {"aaa bbb", "ccc"}.

#include <string>
#include <vector>

namespace editor::canvas_text {

namespace detail {
// Tamanho em bytes do caractere UTF-8 que comeca em s[i] (continuacao solta conta 1).
inline std::size_t charLen(const std::string& s, std::size_t i) {
  const unsigned char c = static_cast<unsigned char>(s[i]);
  std::size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
  while (n > 1 && i + n > s.size()) --n;
  return n;
}
}  // namespace detail

// Quebra `text` em linhas que cabem em `maxWidth` segundo `measure` (float(const std::string&)). maxWidth <= 0: uma
// linha por "\n", sem quebra por largura. Sempre devolve ao menos uma linha (vazia para texto vazio).
template <class Measure>
std::vector<std::string> wrapLines(const std::string& text, float maxWidth, Measure measure) {
  std::vector<std::string> lines;
  std::size_t start = 0;
  while (true) {   // um paragrafo por trecho entre "\n"
    const std::size_t nl = text.find('\n', start);
    const std::string para = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
    if (maxWidth <= 0.f) {
      lines.push_back(para);
    } else {
      std::string cur;
      std::size_t i = 0;
      while (i < para.size()) {
        std::size_t j = para.find(' ', i);
        if (j == std::string::npos) j = para.size();
        const std::string word = para.substr(i, j - i);
        i = j + 1;
        if (word.empty()) continue;   // espacos repetidos colapsam
        const std::string joined = cur.empty() ? word : cur + " " + word;
        if (measure(joined) <= maxWidth) { cur = joined; continue; }
        if (!cur.empty()) { lines.push_back(cur); cur.clear(); }
        if (measure(word) <= maxWidth) { cur = word; continue; }
        // Palavra maior que a linha: parte em pedacos que cabem (ao menos um caractere por pedaco).
        std::string piece;
        for (std::size_t p = 0; p < word.size();) {
          const std::size_t n = detail::charLen(word, p);
          const std::string next = piece + word.substr(p, n);
          if (!piece.empty() && measure(next) > maxWidth) { lines.push_back(piece); piece.clear(); continue; }
          piece = next;
          p += n;
        }
        cur = piece;
      }
      lines.push_back(cur);
    }
    if (nl == std::string::npos) break;
    start = nl + 1;
  }
  return lines;
}

}  // namespace editor::canvas_text
