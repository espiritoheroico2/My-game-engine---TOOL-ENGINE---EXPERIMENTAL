// Modelo do Console (SpriteUI): niveis, filtro de texto, contagens, copia, campo de linha, historico e rolagem.
// Nao usa SDL nem SpriteUI. Roda no ctest como Editor.ConsoleModel.
#include "console_model.hpp"

#include <cstdio>
#include <string>

namespace {

namespace cm = editor::console_model;
using tool::LogLevel;

int run = 0;
int failed = 0;
const char* current = "";

void Check(bool ok, const char* expr) {
  ++run;
  if (!ok) {
    ++failed;
    std::fprintf(stderr, "[FAIL] %s: %s\n", current, expr);
  }
}
#define CHECK(expr) Check((expr), #expr)

cm::LogEntries Sample() {
  return cm::LogEntries{
      {LogLevel::Info, "Engine started."},
      {LogLevel::Trace, "[Cubo] posicao 1"},
      {LogLevel::Warn, "TOOL-S007 aviso de script"},
      {LogLevel::Error, "[Cubo] falhou ao carregar"},
      {LogLevel::Info, "[Luz] ligada"},
  };
}

void TestGroups() {
  current = "groups";
  CHECK(cm::groupOf(LogLevel::Trace) == cm::Group::Info);
  CHECK(cm::groupOf(LogLevel::Info) == cm::Group::Info);
  CHECK(cm::groupOf(LogLevel::Warn) == cm::Group::Warn);
  CHECK(cm::groupOf(LogLevel::Error) == cm::Group::Error);
  const auto c = cm::ConsoleModel::countAll(Sample());
  CHECK(c.of(cm::Group::Info) == 3);   // Info + Trace + Info
  CHECK(c.of(cm::Group::Warn) == 1);
  CHECK(c.of(cm::Group::Error) == 1);
  CHECK(c.total() == 5);
}

void TestLevelFilter() {
  current = "level filter";
  cm::ConsoleModel m;
  const auto e = Sample();
  CHECK(m.visibleIndices(e).size() == 5);
  m.toggle(cm::Group::Info);
  CHECK(!m.enabled(cm::Group::Info));
  CHECK(m.visibleIndices(e).size() == 2);   // so Warn e Error
  // Contagem nao depende do filtro.
  CHECK(cm::ConsoleModel::countAll(e).total() == 5);
  m.toggle(cm::Group::Info);
  m.toggle(cm::Group::Error);
  CHECK(m.visibleIndices(e).size() == 4);   // Info (3) + Warn (1)
}

void TestTextFilter() {
  current = "text filter";
  cm::ConsoleModel m;
  const auto e = Sample();
  m.filter().insert("cubo");   // sem diferenca de maiusculas
  const auto idx = m.visibleIndices(e);
  CHECK(idx.size() == 2);
  CHECK(idx.size() == 2 && idx[0] == 1 && idx[1] == 3);
  m.filter().setText("TOOL-s007");
  CHECK(m.visibleIndices(e).size() == 1);
  m.filter().setText("nada disso");
  CHECK(m.visibleIndices(e).empty());
  m.filter().clear();
  CHECK(m.visibleIndices(e).size() == 5);
  CHECK(cm::containsIgnoreCase("abc", ""));
  CHECK(!cm::containsIgnoreCase("ab", "abc"));
}

void TestCopy() {
  current = "copy";
  cm::ConsoleModel m;
  const auto e = Sample();
  m.toggle(cm::Group::Info);
  m.filter().insert("cubo");
  CHECK(m.copyText(e) == "[Cubo] falhou ao carregar\n");
  m.filter().clear();
  m.toggle(cm::Group::Info);
  CHECK(m.copyText(cm::LogEntries{}).empty());
}

void TestBracketName() {
  current = "bracket name";
  CHECK(cm::bracketName("[Cubo] posicao") == "Cubo");
  CHECK(cm::bracketName("sem colchetes").empty());
  CHECK(cm::bracketName("[] vazio").empty());
  CHECK(cm::bracketName("[aberto sem fim").empty());
  CHECK(cm::bracketName("x [Luz] y") == "Luz");
}

void TestLineEdit() {
  current = "line edit";
  cm::LineEdit l;
  l.insert("ab");
  CHECK(l.text() == "ab" && l.caret() == 2);
  l.left();
  l.insert("X");
  CHECK(l.text() == "aXb" && l.caret() == 2);
  CHECK(l.backspace() && l.text() == "ab" && l.caret() == 1);
  CHECK(l.del() && l.text() == "a");
  CHECK(!l.del());
  l.home();
  CHECK(l.caret() == 0 && !(l.backspace()));
  l.end();
  CHECK(l.caret() == 1);
  // UTF-8: "ç" tem dois bytes. Apagar remove o caractere inteiro.
  l.clear();
  l.insert("a\xC3\xA7");   // "aç"
  CHECK(l.caret() == 3);
  CHECK(l.backspace() && l.text() == "a" && l.caret() == 1);
  l.insert("\xC3\xA7\xC3\xA7");   // "açç"
  l.home();
  l.right();
  CHECK(l.caret() == 1);                          // 'a' tem 1 byte
  l.right();
  CHECK(l.caret() == 3);                          // passou o primeiro 'ç' inteiro
  l.left();
  CHECK(l.caret() == 1);
  l.right();
  CHECK(l.del() && l.text() == "aç");

  l.clear();
  l.insert("hello");
  l.moveCaretTo(1);
  l.moveCaretTo(4, true);
  const auto selectedRange = l.selectionRange();
  CHECK(l.hasSelection() && selectedRange.first == 1 && selectedRange.second == 4);
  CHECK(l.selectedText() == "ell");
  l.insert("i");
  CHECK(l.text() == "hio" && l.caret() == 2 && !l.hasSelection());
  l.moveCaretTo(1);
  l.moveCaretTo(2, true);
  CHECK(l.backspace() && l.text() == "ho" && l.caret() == 1);

  l.clear();
  l.insert("a\xC3\xA7" "b");   // "açb"
  l.moveCaretTo(2);              // continuation byte must snap to a codepoint boundary
  CHECK(l.caret() == 1);
  l.moveCaretTo(3, true);
  CHECK(l.selectedText() == "\xC3\xA7");
}

void TestHistory() {
  current = "history";
  cm::ConsoleModel m;
  m.historyUp();                                   // sem historico: nada
  CHECK(m.command().empty());
  m.command().insert("help");
  CHECK(m.submit() == "help" && m.command().empty());
  m.command().insert("help");
  CHECK(m.submit() == "help");                     // repetido: nao duplica
  CHECK(m.history().size() == 1);
  m.command().insert("test ECS: 10");
  CHECK(m.submit() == "test ECS: 10");
  CHECK(m.submit().empty());                       // campo vazio: nada e nao entra no historico
  CHECK(m.history().size() == 2);

  m.historyUp();
  CHECK(m.command().text() == "test ECS: 10");     // o mais novo primeiro
  m.historyUp();
  CHECK(m.command().text() == "help");
  m.historyUp();
  CHECK(m.command().text() == "help");             // no inicio, para
  m.historyDown();
  CHECK(m.command().text() == "test ECS: 10");
  m.historyDown();
  CHECK(m.command().empty());                      // passou do ultimo: campo limpo
  m.historyDown();
  CHECK(m.command().empty());
}

void TestHistoryLimit() {
  current = "history limit";
  cm::ConsoleModel m;
  for (std::size_t i = 0; i < cm::kMaxHistory + 5; ++i) {
    m.command().setText("cmd" + std::to_string(i));
    m.submit();
  }
  CHECK(m.history().size() == cm::kMaxHistory);
  CHECK(m.history().front() == "cmd5");
  CHECK(m.history().back() == "cmd" + std::to_string(cm::kMaxHistory + 4));
}

void TestFollow() {
  current = "follow";
  cm::ConsoleModel m;
  CHECK(m.follow());                               // comeca seguindo o fim
  m.onScrolled(100.f, 400.f);                      // subiu
  CHECK(!m.follow());
  m.onScrolled(399.5f, 400.f);                     // dentro da folga: esta no fim
  CHECK(m.follow());
  m.onScrolled(0.f, 0.f);                          // lista curta: sempre no fim
  CHECK(m.follow());
  m.setFollow(false);
  CHECK(!m.follow());
}

}  // namespace

int main() {
  TestGroups();
  TestLevelFilter();
  TestTextFilter();
  TestCopy();
  TestBracketName();
  TestLineEdit();
  TestHistory();
  TestHistoryLimit();
  TestFollow();
  std::printf("%d/%d checks passed\n", run - failed, run);
  return failed == 0 ? 0 : 1;
}
