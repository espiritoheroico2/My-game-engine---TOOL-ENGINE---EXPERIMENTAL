// Indice do HierarchyModel::Sync: id -> posicao precisa continuar certo quando a lista muda de forma
// (mesmos ids, ids reordenados, item removido ou novo, dado alterado). Sem janela.
#include "hierarchy_model.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace editor::hierarchy_model;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FALHOU: %s (linha %d)\n", #c, __LINE__); ++fails; } } while (0)

static Item make(Id id, Id parent, const char* label) {
    Item it; it.id = id; it.parent = parent; it.label = label; return it;
}

// Confere que Find devolve o item certo para cada id da lista.
static bool allFound(const HierarchyModel& m, const std::vector<Item>& expect) {
    for (const Item& e : expect) {
        const Item* got = m.Find(e.id);
        if (!got || got->id != e.id || got->label != e.label || got->hidden != e.hidden) return false;
    }
    return true;
}

int main() {
    HierarchyModel m;
    std::vector<Item> a = {make(1, NoId, "Raiz"), make(2, 1, "Filho A"), make(3, 1, "Filho B"), make(4, NoId, "Outra")};
    m.Sync(a);
    CHECK(allFound(m, a));
    CHECK(m.RowCount() == 4);

    // 1) Mesma estrutura, dado mudou (oculto): o indice nao muda, Find deve refletir o dado novo.
    a[1].hidden = true;
    m.Sync(a);
    CHECK(allFound(m, a));
    CHECK(m.Find(2) && m.Find(2)->hidden);

    // 2) Mesmos ids, ordem trocada: o indice deve ser refeito.
    std::vector<Item> b = {a[3], a[0], a[2], a[1]};
    m.Sync(b);
    CHECK(allFound(m, b));
    CHECK(m.Find(4)->label == "Outra");

    // 3) Item removido: Find do removido vira nullptr e os outros continuam certos.
    std::vector<Item> c = {b[0], b[1], b[3]};   // sem o id 3
    m.Sync(c);
    CHECK(m.Find(3) == nullptr);
    CHECK(allFound(m, c));

    // 4) Item novo no fim.
    std::vector<Item> d = c;
    d.push_back(make(9, NoId, "Novo"));
    m.Sync(d);
    CHECK(allFound(m, d));
    CHECK(m.Find(9) && m.Find(9)->label == "Novo");

    // 5) Mesmos ids com rotulo novo (muda a assinatura e reconstroi a arvore).
    d[1].label = "Raiz renomeada";   // d[1] e o id 1
    m.Sync(d);
    CHECK(allFound(m, d));
    CHECK(m.Find(1)->label == "Raiz renomeada");

    if (fails == 0) std::printf("hierarchy index: todos os testes passaram\n");
    return fails == 0 ? 0 : 1;
}
