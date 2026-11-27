// Classificador work-debug do Console: TOOL-W301 e TOOL-W302 (medicao de desempenho) ficam em grupo proprio,
// fora da contagem de Warnings e escondidos por padrao. Sem janela. Roda no ctest como Editor.ConsoleWorkDebug.
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
      {LogLevel::Warn, "TOOL-W301 trabalho da CPU oscila muito (desvio 5.00 ms)."},
      {LogLevel::Warn, "TOOL-W302 zona 'Update' teve pico de 12.00 ms (media 1.00 ms)."},
      {LogLevel::Warn, "TOOL-S007 aviso de script"},
      {LogLevel::Warn, "TOOL-W307 memoria do processo em 3000.00 MB."},
      {LogLevel::Info, "Engine started."},
  };
}

void TestClassifier() {
  current = "classifier";
  CHECK(cm::isWorkDebugText("TOOL-W301 trabalho da CPU oscila"));
  CHECK(cm::isWorkDebugText("TOOL-W302 zona 'Update' teve pico"));
  CHECK(cm::isWorkDebugText("TOOL-W302.3 zona"));          // chave de zona com indice ainda e work-debug
  CHECK(cm::isWorkDebugText("TOOL-W302"));
  CHECK(!cm::isWorkDebugText("TOOL-S007 aviso de script"));
  CHECK(!cm::isWorkDebugText("TOOL-W307 memoria alta"));   // memoria continua aviso normal
  CHECK(!cm::isWorkDebugText("TOOL-W3010 nao e o codigo"));
  CHECK(!cm::isWorkDebugText("sem codigo TOOL-W301 no meio"));
  CHECK(!cm::isWorkDebugText(""));
}

void TestGroupOfEntry() {
  current = "group of entry";
  CHECK(cm::groupOf(Sample()[0]) == cm::Group::WorkDebug);
  CHECK(cm::groupOf(Sample()[1]) == cm::Group::WorkDebug);
  CHECK(cm::groupOf(Sample()[2]) == cm::Group::Warn);
  CHECK(cm::groupOf(Sample()[3]) == cm::Group::Warn);
  CHECK(cm::groupOf(Sample()[4]) == cm::Group::Info);
}

void TestCounts() {
  current = "counts";
  const auto c = cm::ConsoleModel::countAll(Sample());
  CHECK(c.of(cm::Group::WorkDebug) == 2);
  CHECK(c.of(cm::Group::Warn) == 2);           // so os avisos reais
  CHECK(c.of(cm::Group::Info) == 1);
  CHECK(c.total() == 5);
}

void TestDefaultHidden() {
  current = "default hidden";
  cm::ConsoleModel m;
  CHECK(!m.enabled(cm::Group::WorkDebug));
  CHECK(m.visibleIndices(Sample()).size() == 3);   // W301 e W302 escondidos
  CHECK(m.copyText(Sample()).find("TOOL-W301") == std::string::npos);
}

void TestToggleShows() {
  current = "toggle shows";
  cm::ConsoleModel m;
  m.toggle(cm::Group::WorkDebug);
  CHECK(m.enabled(cm::Group::WorkDebug));
  CHECK(m.visibleIndices(Sample()).size() == 5);
  m.toggle(cm::Group::WorkDebug);
  CHECK(m.visibleIndices(Sample()).size() == 3);
}

}  // namespace

int main() {
  TestClassifier();
  TestGroupOfEntry();
  TestCounts();
  TestDefaultHidden();
  TestToggleShows();
  std::printf("%d/%d checks passed\n", run - failed, run);
  return failed == 0 ? 0 : 1;
}
