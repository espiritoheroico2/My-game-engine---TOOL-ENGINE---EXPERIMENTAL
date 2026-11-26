// Testes sem janela dos ids dos elementos de UI (ui_ids.hpp). Roda no ctest como Editor.UiIds.
#include "ui_ids.hpp"

#include <cstdio>
#include <string>

namespace {

int gFailures = 0;

void Check(bool ok, const char* what) {
  std::printf("%s: %s\n", ok ? "ok" : "FALHOU", what);
  if (!ok) ++gFailures;
}

}  // namespace

int main() {
  namespace ui = editor::ui_ids;

  Check(ui::make(0) == "e0", "make(0) = e0");
  Check(ui::make(35) == "ez", "make(35) = ez (ultimo digito base 36)");
  Check(ui::make(36) == "e10", "make(36) = e10");
  Check(ui::parse("e10") == 36, "parse(e10) = 36");
  Check(ui::parse(ui::make(123456789)) == 123456789, "parse(make(n)) = n");
  Check(ui::parse("") == 0, "parse vazio = 0");
  Check(ui::parse("x1") == 0, "parse sem prefixo e = 0");
  Check(ui::parse("eA") == 0, "parse com maiuscula = 0 (formato so minusculo)");
  Check(ui::parse("e-1") == 0, "parse com simbolo = 0");

  ui::gNext = 1;
  const std::string a = ui::next(), b = ui::next(), c = ui::next();
  Check(a != b && b != c && a != c, "ids da sessao sao distintos");

  ui::gNext = 1;
  ui::observe("e50");
  Check(ui::parse("e50") == 180 && ui::gNext == 181, "observe(e50) empurra o contador para 181 (e50 = 180 em base 36)");
  Check(ui::next() != "e50", "novo id nao repete um id carregado");

  ui::gNext = 1;
  ui::observe("nao-id");
  Check(ui::gNext == 1, "observe de id invalido nao muda o contador");

  std::printf("%d falha(s)\n", gFailures);
  return gFailures == 0 ? 0 : 1;
}
