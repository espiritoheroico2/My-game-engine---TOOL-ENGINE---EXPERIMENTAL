// Modelo do Console feito com SpriteUI: o que aparece no log, filtros, contagens, historico de comandos e
// modo de rolagem. Nao usa SDL, ImGui nem SpriteUI. O painel (console_panel.hpp) so desenha e repassa teclas.
//
// Dados: o Editor guarda as linhas em um std::deque<pair<LogLevel, string>> (ed.console). O modelo le esse
// deque e nao o copia. Os filtros, o campo de texto e o historico ficam aqui.
//
// Regras que o modelo garante (testadas em tests/test_console_model.cpp):
//  - Niveis: Trace e Info viram o grupo "Info"; Warn vira "Warnings"; Error vira "Errors".
//  - Uma linha aparece se o seu grupo esta ligado E o texto contem o filtro (sem diferenca de maiusculas).
//  - Contagens por grupo sao sobre TODAS as linhas (filtro nao muda a contagem).
//  - Copiar leva so as linhas visiveis, uma por linha, na ordem do log.
//  - Campo de linha (filtro e comando) edita por codepoint UTF-8: apagar nunca corta um caractere no meio.
//  - Historico de comandos: Cima/Baixo percorrem do mais novo ao mais antigo; comando igual ao ultimo nao repete.
//  - Rolagem: "seguir o fim" fica ligado ate o usuario subir; voltar ao fim liga de novo.
#pragma once

#include "tool/core/log.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace editor::console_model {

constexpr std::size_t kMaxEntries = 1000;     // linhas guardadas (o Editor descarta as mais antigas)
constexpr std::size_t kMaxHistory = 100;      // comandos lembrados para Cima/Baixo
constexpr float kTailSlack = 1.f;             // px: abaixo disso do fim, conta como "no fim"

using LogEntry = std::pair<tool::LogLevel, std::string>;
using LogEntries = std::deque<LogEntry>;

enum class Group : std::uint8_t { Info = 0, Warn = 1, Error = 2 };
constexpr int kGroupCount = 3;

inline Group groupOf(tool::LogLevel level) {
  if (level == tool::LogLevel::Error) return Group::Error;
  if (level == tool::LogLevel::Warn) return Group::Warn;
  return Group::Info;
}

struct Counts {
  std::size_t byGroup[kGroupCount] = {0, 0, 0};
  [[nodiscard]] std::size_t of(Group g) const { return byGroup[static_cast<int>(g)]; }
  [[nodiscard]] std::size_t total() const { return byGroup[0] + byGroup[1] + byGroup[2]; }
};

// Procura `needle` em `hay` sem diferenca de maiusculas (ASCII). Vazio casa com tudo.
inline bool containsIgnoreCase(std::string_view hay, std::string_view needle) {
  if (needle.empty()) return true;
  auto low = [](unsigned char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c); };
  if (needle.size() > hay.size()) return false;
  for (std::size_t i = 0; i + needle.size() <= hay.size(); ++i) {
    std::size_t k = 0;
    while (k < needle.size() && low(static_cast<unsigned char>(hay[i + k])) == low(static_cast<unsigned char>(needle[k]))) ++k;
    if (k == needle.size()) return true;
  }
  return false;
}

// Nome entre o primeiro '[' e o ']' seguinte ("[Cubo] carregou" -> "Cubo"). Vazio se nao houver.
inline std::string bracketName(std::string_view msg) {
  const auto lb = msg.find('[');
  if (lb == std::string_view::npos) return {};
  const auto rb = msg.find(']', lb + 1);
  if (rb == std::string_view::npos || rb == lb + 1) return {};
  return std::string(msg.substr(lb + 1, rb - lb - 1));
}

// Linha de texto editavel (filtro e comando). Caret em bytes, sempre em fronteira de codepoint UTF-8.
class LineEdit {
 public:
  [[nodiscard]] const std::string& text() const { return text_; }
  [[nodiscard]] std::size_t caret() const { return caret_; }
  [[nodiscard]] bool empty() const { return text_.empty(); }

  void setText(std::string s) {
    text_ = std::move(s);
    caret_ = text_.size();
  }
  void clear() { text_.clear(); caret_ = 0; }

  void insert(std::string_view s) {
    text_.insert(caret_, s.data(), s.size());
    caret_ += s.size();
  }

  bool backspace() {
    if (caret_ == 0) return false;
    const std::size_t p = prev(caret_);
    text_.erase(p, caret_ - p);
    caret_ = p;
    return true;
  }

  bool del() {
    if (caret_ >= text_.size()) return false;
    text_.erase(caret_, next(caret_) - caret_);
    return true;
  }

  void left() { caret_ = prev(caret_); }
  void right() { caret_ = next(caret_); }
  void home() { caret_ = 0; }
  void end() { caret_ = text_.size(); }

 private:
  static bool isCont(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }
  [[nodiscard]] std::size_t prev(std::size_t p) const {
    if (p == 0) return 0;
    --p;
    while (p > 0 && isCont(text_[p])) --p;
    return p;
  }
  [[nodiscard]] std::size_t next(std::size_t p) const {
    if (p >= text_.size()) return text_.size();
    ++p;
    while (p < text_.size() && isCont(text_[p])) ++p;
    return p;
  }

  std::string text_;
  std::size_t caret_ = 0;
};

class ConsoleModel {
 public:
  // ---- filtros por nivel (Info / Warnings / Errors), todos ligados por padrao
  [[nodiscard]] bool enabled(Group g) const { return on_[static_cast<int>(g)]; }
  void toggle(Group g) { on_[static_cast<int>(g)] = !on_[static_cast<int>(g)]; }

  // ---- campos de texto
  [[nodiscard]] LineEdit& filter() { return filter_; }
  [[nodiscard]] const LineEdit& filter() const { return filter_; }
  [[nodiscard]] LineEdit& command() { return command_; }
  [[nodiscard]] const LineEdit& command() const { return command_; }

  // Uma linha passa se o grupo esta ligado e o texto do filtro esta contido nela.
  [[nodiscard]] bool visible(const LogEntry& e) const {
    return enabled(groupOf(e.first)) && containsIgnoreCase(e.second, filter_.text());
  }

  // Contagens por grupo sobre todas as linhas (nao depende do filtro).
  [[nodiscard]] static Counts countAll(const LogEntries& entries) {
    Counts c;
    for (const auto& e : entries) ++c.byGroup[static_cast<int>(groupOf(e.first))];
    return c;
  }

  // Indices (no deque) das linhas visiveis, em ordem.
  [[nodiscard]] std::vector<std::size_t> visibleIndices(const LogEntries& entries) const {
    std::vector<std::size_t> out;
    out.reserve(entries.size());
    for (std::size_t i = 0; i < entries.size(); ++i)
      if (visible(entries[i])) out.push_back(i);
    return out;
  }

  // Texto das linhas visiveis (para Copiar), uma por linha.
  [[nodiscard]] std::string copyText(const LogEntries& entries) const {
    std::string out;
    for (const auto& e : entries) {
      if (!visible(e)) continue;
      out += e.second;
      out += '\n';
    }
    return out;
  }

  // ---- historico de comandos
  // Envia o que esta no campo: devolve o comando (vazio se nada) e limpa o campo.
  std::string submit() {
    std::string cmd = command_.text();
    if (cmd.empty()) return {};
    if (history_.empty() || history_.back() != cmd) history_.push_back(cmd);
    if (history_.size() > kMaxHistory) history_.erase(history_.begin());
    histPos_ = -1;
    command_.clear();
    return cmd;
  }

  // Cima: comando anterior (o primeiro toque vai ao mais novo). Sem historico, nada.
  void historyUp() {
    if (history_.empty()) return;
    if (histPos_ == -1) histPos_ = static_cast<int>(history_.size()) - 1;
    else if (histPos_ > 0) --histPos_;
    command_.setText(history_[static_cast<std::size_t>(histPos_)]);
  }

  // Baixo: comando seguinte; depois do ultimo, o campo volta a vazio.
  void historyDown() {
    if (histPos_ == -1) return;
    if (++histPos_ >= static_cast<int>(history_.size())) {
      histPos_ = -1;
      command_.clear();
    } else {
      command_.setText(history_[static_cast<std::size_t>(histPos_)]);
    }
  }

  [[nodiscard]] const std::vector<std::string>& history() const { return history_; }

  // ---- rolagem: seguir o fim
  [[nodiscard]] bool follow() const { return follow_; }
  void setFollow(bool on) { follow_ = on; }
  // Chamar com a rolagem atual e a maxima: subir tira o "seguir"; chegar ao fim o devolve.
  void onScrolled(float offset, float maxOffset) { follow_ = offset >= maxOffset - kTailSlack; }

 private:
  bool on_[kGroupCount] = {true, true, true};
  LineEdit filter_;
  LineEdit command_;
  std::vector<std::string> history_;
  int histPos_ = -1;
  bool follow_ = true;
};

}  // namespace editor::console_model
