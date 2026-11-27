// Teste do modelo da janela Node (node_model.hpp), sem janela: coleta da subarvore (objeto + filhos, recursivo) e o
// filtro de isolamento (so a subarvore fica visivel). Cena de teste com ids inteiros; 0 nao e usado como objeto.
#include "node_model.hpp"

#include <cstdio>
#include <map>
#include <optional>
#include <vector>

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

namespace nm = editor::node_model;
using L = nm::Link<int>;

}  // namespace

int main() {
  // Cena:   1 (raiz da cena)        10 (irmao da raiz isolada, com filho 11)     20 (solto)
  //          +- 2 (isolado)
  //               +- 3 +- 5
  //               |    +- 6 -- 7
  //               +- 4
  const std::vector<L> all = {{1, std::nullopt}, {2, 1}, {3, 2}, {4, 2}, {5, 3}, {6, 3}, {7, 6},
                              {10, 1}, {11, 10}, {20, std::nullopt}};
  std::map<int, std::optional<int>> parents;
  for (const L& l : all) parents[l.id] = l.parent;
  auto parentOf = [&](int id) -> std::optional<int> {
    const auto it = parents.find(id);
    return it == parents.end() ? std::nullopt : it->second;
  };

  // Coleta: o objeto e todos os descendentes, em ordem de arvore, com a profundidade.
  const std::vector<nm::Row<int>> rows = nm::CollectSubtree(2, all);
  const std::vector<int> expectedIds = {2, 3, 5, 6, 7, 4};
  const std::vector<int> expectedDepth = {0, 1, 2, 2, 3, 1};
  CHECK(rows.size() == expectedIds.size());
  for (std::size_t i = 0; i < rows.size() && i < expectedIds.size(); ++i) {
    CHECK(rows[i].id == expectedIds[i]);
    CHECK(rows[i].depth == expectedDepth[i]);
  }
  // Fora da coleta: o pai, os irmaos e o resto da cena.
  for (const int out : {1, 10, 11, 20}) {
    bool found = false;
    for (const nm::Row<int>& r : rows) found = found || r.id == out;
    CHECK(!found);
  }

  // Folha: so ela mesma. Raiz da cena: a cena toda ligada a ela.
  CHECK(nm::CollectSubtree(4, all).size() == 1);
  CHECK(nm::CollectSubtree(1, all).size() == 9);   // tudo menos o 20 (solto)
  CHECK(nm::CollectSubtree(20, all).size() == 1);

  // Ciclo de pais corrompido (a <-> b): termina e nao repete linhas.
  const std::vector<L> cyc = {{1, 2}, {2, 1}};
  CHECK(nm::CollectSubtree(1, cyc).size() <= 3);

  // InSubtree: a propria raiz e os descendentes entram; pai, irmaos e solto nao.
  for (const int in : {2, 3, 4, 5, 6, 7}) CHECK(nm::InSubtree(2, in, parentOf));
  for (const int out : {1, 10, 11, 20}) CHECK(!nm::InSubtree(2, out, parentOf));

  // Filtro de isolamento: sem raiz tudo aparece; com raiz so a subarvore.
  for (const int id : {1, 2, 7, 11, 20}) CHECK(nm::VisibleInIsolation(false, 2, id, parentOf));
  CHECK(nm::VisibleInIsolation(true, 2, 7, parentOf));
  CHECK(nm::VisibleInIsolation(true, 2, 2, parentOf));
  CHECK(!nm::VisibleInIsolation(true, 2, 1, parentOf));
  CHECK(!nm::VisibleInIsolation(true, 2, 10, parentOf));
  CHECK(!nm::VisibleInIsolation(true, 2, 11, parentOf));
  CHECK(!nm::VisibleInIsolation(true, 2, 20, parentOf));

  // Isolar um neto: so ele e o filho dele.
  CHECK(nm::VisibleInIsolation(true, 6, 7, parentOf));
  CHECK(!nm::VisibleInIsolation(true, 6, 3, parentOf));
  CHECK(!nm::VisibleInIsolation(true, 6, 5, parentOf));

  // Ciclo de pais: InSubtree termina (limite de profundidade) em vez de travar.
  auto cycParent = [&](int id) -> std::optional<int> { return id == 1 ? 2 : 1; };
  CHECK(!nm::InSubtree(99, 1, cycParent));

  std::printf("node model: %d checks, %d failed\n", run, failed);
  return failed == 0 ? 0 : 1;
}
