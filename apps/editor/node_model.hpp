#pragma once

// Modelo puro da janela "Node" (edicao individual de um objeto): quais objetos fazem parte do isolamento.
// Nao depende do SDL nem do Editor: o tipo de id e um parametro, entao o teste usa inteiros.
//  - InSubtree: o objeto e a raiz isolada ou descende dela (filtro de desenho e de clique da Scene).
//  - CollectSubtree: a raiz e seus filhos, recursivo, em ordem de arvore, com a profundidade de cada um.

#include <cstddef>
#include <optional>
#include <vector>

namespace editor::node_model {

// Limite de profundidade: protege contra ciclo de pais em dados corrompidos.
inline constexpr int kMaxDepth = 256;

// Uma linha da arvore do Node: o objeto e a profundidade (0 = a raiz isolada).
template <class Id>
struct Row {
  Id id{};
  int depth = 0;
};

// Um objeto da cena com o seu pai (nullopt = sem pai). A lista inteira e a entrada de CollectSubtree.
template <class Id>
struct Link {
  Id id{};
  std::optional<Id> parent;
};

// true se `e` e a raiz ou descende dela. parentOf(id) devolve std::optional<Id> (nullopt = sem pai).
template <class Id, class ParentFn>
[[nodiscard]] bool InSubtree(const Id& root, const Id& e, ParentFn&& parentOf) {
  Id cur = e;
  for (int i = 0; i < kMaxDepth; ++i) {
    if (cur == root) return true;
    const std::optional<Id> p = parentOf(cur);
    if (!p) return false;
    cur = *p;
  }
  return false;
}

// Filtro de isolamento: sem raiz (hasRoot = false) tudo e visivel; com raiz so a subarvore dela e.
template <class Id, class ParentFn>
[[nodiscard]] bool VisibleInIsolation(bool hasRoot, const Id& root, const Id& e, ParentFn&& parentOf) {
  return !hasRoot || InSubtree(root, e, parentOf);
}

// A raiz e todos os descendentes (filhos, netos...), em ordem de arvore (pai antes dos filhos, irmaos na ordem da lista).
// Irmaos da raiz e o resto da cena ficam de fora. `all` = todos os objetos da cena com o respectivo pai.
template <class Id>
[[nodiscard]] std::vector<Row<Id>> CollectSubtree(const Id& root, const std::vector<Link<Id>>& all) {
  std::vector<Row<Id>> out;
  // Pilha explicita: percorre em profundidade mantendo a ordem da lista.
  std::vector<Row<Id>> stack{{root, 0}};
  const std::size_t limit = all.size() + 1;   // cada objeto entra no maximo uma vez (ciclos nao travam)
  while (!stack.empty() && out.size() < limit) {
    const Row<Id> cur = stack.back();
    stack.pop_back();
    out.push_back(cur);
    // Filhos empilhados ao contrario: o primeiro da lista sai primeiro.
    for (std::size_t i = all.size(); i-- > 0;) {
      const Link<Id>& l = all[i];
      if (l.parent && *l.parent == cur.id && !(l.id == root)) stack.push_back({l.id, cur.depth + 1});
    }
  }
  return out;
}

}  // namespace editor::node_model
