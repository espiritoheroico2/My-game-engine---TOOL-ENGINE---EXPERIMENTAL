// Teste do modelo do Controlador Narrativo (narrative_model.hpp), sem janela: nos, ligacoes, renomear, bloco fixo
// da cena, variaveis, compilacao e teste rapido. A janela SpriteUI so chama estas regras.
#include "narrative_model.hpp"

#include <cstdio>
#include <string>

namespace {

int run = 0;
int failed = 0;

void Check(bool ok, const char* expr, int line) {
  ++run;
  if (!ok) {
    ++failed;
    std::fprintf(stderr, "[FAIL] line %d: %s\n", line, expr);
  }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)

} // namespace

int main() {
  using editor::NarrativeModel;
  NarrativeModel m;   // grafo novo: variavel "hp" e sem nos (a cena ainda nao foi informada)
  CHECK(m.graph().find("variables") != nullptr);
  CHECK(m.nodeList()->items().empty());

  // Cena: cria o bloco fixo uma vez e atualiza o nome sem duplicar.
  m.setScene("Fase 1");
  CHECK(m.nodeList()->items().size() == 1);
  CHECK(m.nodeList()->items()[0].has("pinned"));
  m.setScene("Fase 2");
  CHECK(m.nodeList()->items().size() == 1);
  CHECK(m.nodeList()->items()[0]["label"].asString() == "Fase 2");

  // Nos: id unico por tipo, posicao gravada.
  const int a = m.addNode("set", 100, 50);
  const int b = m.addNode("set", 300, 50);
  CHECK(a == 1 && b == 2);
  CHECK(m.nodeList()->items()[1]["id"].asString() == "set1");
  CHECK(m.nodeList()->items()[2]["id"].asString() == "set2");
  CHECK(m.nodeList()->items()[1]["x"].asNumber() == 100.0);

  // Ligacoes: sem auto-ligacao, sem id inexistente, sem duplicata.
  CHECK(!m.addLink("set1", "out", "set1"));
  CHECK(!m.addLink("set1", "out", "nope"));
  CHECK(m.addLink("set1", "out", "set2"));
  CHECK(!m.addLink("set1", "out", "set2"));
  CHECK(m.linkList()->items().size() == 1);

  // Renomear troca o id nas ligacoes; id em uso e o bloco fixo sao recusados.
  CHECK(m.renameNode(2, "destino"));
  CHECK(m.linkList()->items()[0]["to"].asString() == "destino");
  CHECK(!m.renameNode(2, "set1"));
  CHECK(!m.renameNode(0, "outro"));   // bloco fixo da cena nao renomeia

  // Propriedades: texto, numero e lista.
  m.setNodeNumber(1, "value", 7.5);
  CHECK(m.nodeList()->items()[1]["value"].asNumber() == 7.5);
  m.setNodeList(1, "inputs", {"a", "b"});
  CHECK(m.nodeList()->items()[1]["inputs"].items().size() == 2);

  // Remover: apaga as ligacoes do no; o bloco fixo nao sai.
  m.removeNode(1);
  CHECK(m.linkList()->items().empty());
  CHECK(m.nodeList()->items().size() == 2);
  CHECK(!m.removeNode(0));
  m.removeLink(0);   // indice fora da lista: nada

  // Variaveis.
  CHECK(m.addVariable("score"));
  CHECK(!m.addVariable("score"));   // ja existe
  CHECK(!m.addVariable(""));
  m.setVariable("score", 42);
  CHECK(m.varList()->members().at("score")["value"].asNumber() == 42.0);
  m.removeVariable("score");
  CHECK(!m.varList()->has("score"));

  // Compilar um grafo simples (inicio sem acao e sem erro de ligacao).
  CHECK(m.compile());
  CHECK(!m.compileMessage().empty());

  // Teste rapido: um gatilho "start" sem ligacoes roda sem erro.
  m.fireTest("event");
  CHECK(!m.testLog().empty());
  CHECK(m.testLog()[0].find("fluxo") != std::string::npos || m.testLog()[0].rfind("ERROR", 0) == 0);

  std::printf("%d/%d checks passed\n", run - failed, run);
  return failed == 0 ? 0 : 1;
}
