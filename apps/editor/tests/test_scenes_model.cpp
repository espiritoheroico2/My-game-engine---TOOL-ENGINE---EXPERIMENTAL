// Modelo da janela Scenes (scenes_model.hpp): ordem e marca da cena atual, acoes validas por selecao, nome de cena
// (valido, repetido, com barra), lista de assets da cena (so modelo ou som, sem repetir) e o campo de nome.
// Sem janela. Roda como Editor.ScenesModel.
#include "scenes_model.hpp"

#include <cstdio>
#include <vector>

namespace {

int gFailures = 0;

void Check(bool ok, const char* what) {
  std::printf("%s: %s\n", ok ? "ok" : "FALHOU", what);
  if (!ok) ++gFailures;
}

}  // namespace

int main() {
  namespace SM = editor::scenes_model;

  // 1. Lista em ordem de nome; so a atual vem marcada.
  const auto rows = SM::Rows({"Zeta", "Main", "Alpha"}, "Main");
  Check(rows.size() == 3 && rows[0].name == "Alpha" && rows[1].name == "Main" && rows[2].name == "Zeta", "lista em ordem de nome");
  Check(!rows[0].current && rows[1].current && !rows[2].current, "so a cena atual vem marcada");
  Check(SM::Rows({}, "Main").empty(), "sem cenas, lista vazia");

  // 2. Acoes: nada selecionado, cena atual, outra cena.
  const auto none = SM::ActionsFor("", "Main");
  Check(!none.open && !none.rename && !none.remove, "sem selecao, nenhuma acao");
  const auto current = SM::ActionsFor("Main", "Main");
  Check(!current.open && current.rename && !current.remove, "cena atual: so renomear (nao abre nem apaga)");
  const auto other = SM::ActionsFor("Alpha", "Main");
  Check(other.open && other.rename && other.remove, "outra cena: abrir, renomear e apagar");

  // 3. Nome de cena: vazio, com barra, repetido e valido.
  const std::vector<std::string> existing = {"Main", "Alpha"};
  Check(!SM::NameProblem("", existing).empty(), "nome vazio e recusado");
  Check(!SM::NameProblem("a/b", existing).empty(), "nome com barra e recusado");
  Check(!SM::NameProblem("Main", existing).empty(), "nome repetido e recusado");
  Check(SM::NameProblem("Level 2", existing).empty(), "nome novo e valido");

  // 4. Assets da cena: so modelo ou som, sem repetir, remover pelo indice.
  std::vector<std::string> assets;
  Check(!SM::Assign(assets, "Assets/Models/a.obj", false), "textura nao pode ser atribuida a cena");
  Check(SM::Assign(assets, "Assets/Models/a.obj", true) && assets.size() == 1, "modelo atribuido");
  Check(!SM::Assign(assets, "Assets/Models/a.obj", true) && assets.size() == 1, "mesmo asset nao entra duas vezes");
  Check(!SM::RemoveAt(assets, 5) && assets.size() == 1, "remover indice fora da lista nao muda nada");
  Check(SM::RemoveAt(assets, 0) && assets.empty(), "remover pelo indice");

  // 5. Campo de nome: criar comeca vazio; renomear preenche com a selecao; apagar tira um caractere UTF-8 inteiro.
  SM::Model m;
  m.selected = "Alpha";
  SM::BeginName(m, SM::NameMode::Rename);
  Check(m.mode == SM::NameMode::Rename && m.name == "Alpha" && m.nameFocus, "renomear preenche com a selecao e foca");
  SM::FinishName(m);
  Check(m.mode == SM::NameMode::None && m.name.empty() && !m.nameFocus, "fechar o campo limpa o nome");
  SM::BeginName(m, SM::NameMode::New);
  Check(m.name.empty(), "criar comeca com o campo vazio");
  m.name = "a\xC3\xA7";   // "ac" com cedilha (2 bytes)
  SM::PopUtf8(m.name);
  Check(m.name == "a", "apagar tira o caractere UTF-8 inteiro");

  std::printf("%s (%d falha(s))\n", gFailures == 0 ? "TODOS OS TESTES PASSARAM" : "HA FALHAS", gFailures);
  return gFailures == 0 ? 0 : 1;
}
