// Arvore de nos (TreeView.hpp): expansao, linhas visiveis, teclado e selecao escondida. Sem janela.
#include "SpriteUi/Widgets/TreeView.hpp"

#include <cstdio>
#include <vector>

namespace {

using namespace ToolEngine::SpriteUi::Widgets;

int run = 0;
int failed = 0;

#define CHECK(expr)                                                                                          \
    do {                                                                                                     \
        ++run;                                                                                               \
        if (!(expr)) {                                                                                       \
            ++failed;                                                                                        \
            std::fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #expr);                           \
        }                                                                                                    \
    } while (0)

// Arvore de teste:  A (a1, a2 (a2x)), B
struct SampleTree {
    TreeControl tree;
    std::size_t a = 0, a1 = 0, a2 = 0, a2x = 0, b = 0;

    SampleTree() {
        a = tree.AddNode("A");
        a1 = tree.AddNode("a1", a);
        a2 = tree.AddNode("a2", a);
        a2x = tree.AddNode("a2x", a2);
        b = tree.AddNode("B");
    }
};

std::vector<std::size_t> VisibleNodes(const TreeControl& tree) {
    std::vector<std::size_t> out;
    for (const TreeRow& row : tree.VisibleRows()) out.push_back(row.node);
    return out;
}

void TestInitialRowsAreRootsOnly() {
    SampleTree s;
    const auto rows = s.tree.VisibleRows();
    CHECK(rows.size() == 2);
    CHECK(rows[0].node == s.a && rows[0].depth == 0);
    CHECK(rows[1].node == s.b && rows[1].depth == 0);
    CHECK(!s.tree.IsExpanded(s.a));
    CHECK(s.tree.HasChildren(s.a));
    CHECK(!s.tree.HasChildren(s.b));
}

void TestExpandShowsChildrenWithDepth() {
    SampleTree s;
    CHECK(s.tree.SetExpanded(s.a, true));
    CHECK(!s.tree.SetExpanded(s.a, true)); // ja expandido: nada muda
    CHECK(!s.tree.SetExpanded(s.b, true)); // folha nao expande
    CHECK(!s.tree.SetExpanded(999, true)); // indice invalido

    const auto rows = s.tree.VisibleRows();
    CHECK(rows.size() == 4);
    CHECK(rows[1].node == s.a1 && rows[1].depth == 1);
    CHECK(rows[2].node == s.a2 && rows[2].depth == 1);
    CHECK(rows[3].node == s.b && rows[3].depth == 0);
}

void TestNestedExpansionNeedsEveryAncestor() {
    SampleTree s;
    CHECK(s.tree.SetExpanded(s.a2, true)); // a2 expandido, mas A recolhido: a2x continua escondido
    CHECK(VisibleNodes(s.tree).size() == 2);
    CHECK(s.tree.SetExpanded(s.a, true));
    const auto nodes = VisibleNodes(s.tree);
    CHECK(nodes.size() == 5);
    CHECK(nodes[3] == s.a2x);
}

void TestToggleRespectsDisabled() {
    SampleTree s;
    CHECK(s.tree.Toggle(s.a));
    CHECK(s.tree.IsExpanded(s.a));
    s.tree.disabled = true;
    CHECK(!s.tree.Toggle(s.a));
    CHECK(s.tree.IsExpanded(s.a));
    // Chamada de programa continua valendo mesmo desabilitado.
    CHECK(s.tree.SetExpanded(s.a, false));
}

void TestArrowKeysMoveAndStopAtEnds() {
    SampleTree s;
    CHECK(s.tree.Key(TreeKey::Down));
    CHECK(s.tree.Selected() == s.a);
    CHECK(!s.tree.Key(TreeKey::Up)); // ja no topo
    CHECK(s.tree.Key(TreeKey::Down));
    CHECK(s.tree.Selected() == s.b);
    CHECK(!s.tree.Key(TreeKey::Down)); // ja na ultima linha
}

void TestRightExpandsThenEntersFirstChild() {
    SampleTree s;
    CHECK(s.tree.Select(s.a));
    CHECK(s.tree.Key(TreeKey::Right));
    CHECK(s.tree.IsExpanded(s.a));
    CHECK(s.tree.Selected() == s.a);
    CHECK(s.tree.Key(TreeKey::Right));
    CHECK(s.tree.Selected() == s.a1);
    CHECK(!s.tree.Key(TreeKey::Right)); // folha
}

void TestLeftCollapsesThenClimbsToParent() {
    SampleTree s;
    s.tree.SetExpanded(s.a, true);
    CHECK(s.tree.Select(s.a1));
    CHECK(s.tree.Key(TreeKey::Left)); // a1 e folha: sobe para o pai
    CHECK(s.tree.Selected() == s.a);
    CHECK(s.tree.Key(TreeKey::Left)); // A expandido: recolhe
    CHECK(!s.tree.IsExpanded(s.a));
    CHECK(!s.tree.Key(TreeKey::Left)); // raiz recolhida: nada a fazer
}

void TestEnterTogglesBranchesOnly() {
    SampleTree s;
    CHECK(s.tree.Select(s.a));
    CHECK(s.tree.Key(TreeKey::Enter));
    CHECK(s.tree.IsExpanded(s.a));
    CHECK(s.tree.Key(TreeKey::Enter));
    CHECK(!s.tree.IsExpanded(s.a));
    CHECK(s.tree.Select(s.b));
    CHECK(!s.tree.Key(TreeKey::Enter)); // folha
}

void TestCollapseMovesHiddenSelectionToAncestor() {
    SampleTree s;
    s.tree.SetExpanded(s.a, true);
    s.tree.SetExpanded(s.a2, true);
    CHECK(s.tree.Select(s.a2x));
    CHECK(s.tree.SetExpanded(s.a, false));
    CHECK(s.tree.Selected() == s.a); // a2x sumiu: a selecao sobe para A
}

void TestExpandAndCollapseDoNotRebuildRows() {
    SampleTree s;
    const std::size_t rebuildsBefore = s.tree.RebuildCount();
    CHECK(s.tree.SetExpanded(s.a, true));
    CHECK(s.tree.SetExpanded(s.a2, true));
    CHECK(s.tree.SetExpanded(s.a, false));
    CHECK(s.tree.RebuildCount() == rebuildsBefore); // so mexe nas linhas do no
    CHECK(s.tree.VisibleRows().size() == 2);
}

void TestSelectRejectsHiddenAndDisabled() {
    SampleTree s;
    CHECK(!s.tree.Select(s.a2x)); // escondido
    CHECK(!s.tree.Select(999));
    CHECK(s.tree.Select(s.b));
    CHECK(!s.tree.Select(s.b)); // ja selecionado
    s.tree.disabled = true;
    CHECK(!s.tree.Key(TreeKey::Up));
}

void TestAddNodeAfterExpansionShowsRow() {
    SampleTree s;
    s.tree.SetExpanded(s.a, true);
    const std::size_t extra = s.tree.AddNode("a3", s.a);
    CHECK(s.tree.Parent(extra) == s.a);
    CHECK(VisibleNodes(s.tree).size() == 5);
    // Pai invalido vira raiz em vez de sumir da arvore.
    const std::size_t orphan = s.tree.AddNode("orphan", 12345);
    CHECK(s.tree.Parent(orphan) == NoControlIndex);
    CHECK(VisibleNodes(s.tree).back() == orphan);
}

} // namespace

int main() {
    TestInitialRowsAreRootsOnly();
    TestExpandShowsChildrenWithDepth();
    TestNestedExpansionNeedsEveryAncestor();
    TestToggleRespectsDisabled();
    TestArrowKeysMoveAndStopAtEnds();
    TestRightExpandsThenEntersFirstChild();
    TestLeftCollapsesThenClimbsToParent();
    TestEnterTogglesBranchesOnly();
    TestCollapseMovesHiddenSelectionToAncestor();
    TestSelectRejectsHiddenAndDisabled();
    TestAddNodeAfterExpansionShowsRow();
    TestExpandAndCollapseDoNotRebuildRows();

    std::printf("tree view: %d checks, %d failed\n", run, failed);
    return failed == 0 ? 0 : 1;
}
