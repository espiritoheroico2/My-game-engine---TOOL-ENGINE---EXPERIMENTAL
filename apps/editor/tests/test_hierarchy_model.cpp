// Modelo da janela Hierarchy (hierarchy_model.hpp): arvore, selecao por id, teclas, reparentar e faixa visivel.
// Sem janela. Roda no ctest como Editor.HierarchyModel.
#include "hierarchy_model.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace {

using namespace editor::hierarchy_model;
namespace Wd = ToolEngine::SpriteUi::Widgets;

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

Item Obj(Id id, Id parent, const std::string& label, Kind kind = Kind::Object) {
    Item item;
    item.id = id;
    item.parent = parent;
    item.label = label;
    item.kind = kind;
    return item;
}

// Cena pequena: Player (1) com Arma (2) como filho, Arma com Ponta (3); Luz (4) solta.
std::vector<Item> SmallScene() {
    return {Obj(1, NoId, "Player"), Obj(2, 1, "Arma"), Obj(3, 2, "Ponta"), Obj(4, NoId, "Luz", Kind::Light)};
}

void TestRowsFollowTheTreeOrderAndDepth() {
    HierarchyModel m;
    CHECK(m.Sync(SmallScene()));
    CHECK(m.RowCount() == 4);
    CHECK(m.IdOfRow(0) == 1 && m.DepthOfRow(0) == 0);
    CHECK(m.IdOfRow(1) == 2 && m.DepthOfRow(1) == 1);
    CHECK(m.IdOfRow(2) == 3 && m.DepthOfRow(2) == 2);
    CHECK(m.IdOfRow(3) == 4 && m.DepthOfRow(3) == 0);
    CHECK(m.RowHasChildren(0) && !m.RowHasChildren(2));
}

void TestSameStructureDoesNotRebuild() {
    HierarchyModel m;
    CHECK(m.Sync(SmallScene()));
    CHECK(!m.Sync(SmallScene()));   // mesma estrutura: so guarda os dados
    std::vector<Item> renamed = SmallScene();
    renamed[3].label = "Sol";
    CHECK(m.Sync(renamed));          // rotulo mudou: refaz
}

void TestSelectionFollowsIdAfterInsert() {
    HierarchyModel m;
    m.Sync(SmallScene());
    CHECK(m.Select(3).selectionChanged);
    CHECK(m.Selected() == 3);
    std::vector<Item> more = SmallScene();
    more.insert(more.begin(), Obj(9, NoId, "Novo"));   // a Ponta mudou de posicao na lista
    CHECK(m.Sync(more));
    CHECK(m.Selected() == 3);
}

void TestItemsMissingParentBecomeRoots() {
    HierarchyModel m;
    m.Sync({Obj(5, 99, "Orfao"), Obj(6, 5, "Filho")});
    CHECK(m.RowCount() == 2);
    CHECK(m.DepthOfRow(0) == 0 && m.DepthOfRow(1) == 1);
}

void TestCycleDoesNotHideObjects() {
    HierarchyModel m;
    m.Sync({Obj(1, 2, "A"), Obj(2, 1, "B"), Obj(3, NoId, "Livre")});
    CHECK(m.RowCount() == 3);   // nenhum objeto some
}

void TestCollapseMovesSelectionToTheCollapsedParent() {
    HierarchyModel m;
    m.Sync(SmallScene());
    m.Select(3);
    const Outcome out = m.Toggle(1);   // recolhe o Player, que contem a Ponta
    CHECK(out.selectionChanged);
    CHECK(m.Selected() == 1);
    CHECK(m.RowCount() == 2);          // Player e Luz
}

void TestCollapsedStateSurvivesRebuild() {
    HierarchyModel m;
    m.Sync(SmallScene());
    m.Toggle(2);                       // recolhe a Arma
    CHECK(m.RowCount() == 3);
    std::vector<Item> more = SmallScene();
    more.push_back(Obj(7, NoId, "Camera", Kind::Camera));
    CHECK(m.Sync(more));
    CHECK(m.RowCount() == 4);          // Player, Arma (recolhida), Luz, Camera
    CHECK(!m.RowExpanded(1));
}

void TestArrowKeysMoveAndExpand() {
    HierarchyModel m;
    m.Sync(SmallScene());
    m.Toggle(1);                                             // Player recolhido
    CHECK(m.Select(1).selectionChanged);
    CHECK(m.Key(Wd::TreeKey::Down).selectionChanged);        // vai para a Luz
    CHECK(m.Selected() == 4);
    CHECK(m.Key(Wd::TreeKey::Up).selectionChanged);          // volta para o Player
    CHECK(m.Selected() == 1);
    CHECK(m.Key(Wd::TreeKey::Right).selectionChanged == false);   // Right num no recolhido expande, nao seleciona
    CHECK(m.RowCount() == 4);
    CHECK(m.Key(Wd::TreeKey::Left).selectionChanged == false);   // Left num no aberto recolhe
    CHECK(m.RowCount() == 2);
}

void TestLeftGoesToParentFromLeaf() {
    HierarchyModel m;
    m.Sync(SmallScene());
    m.Select(3);
    CHECK(m.Key(Wd::TreeKey::Left).selectionChanged);        // Ponta (folha) sobe para a Arma
    CHECK(m.Selected() == 2);
}

void TestEnterOnLeafRequestsActivate() {
    HierarchyModel m;
    m.Sync(SmallScene());
    m.Select(3);
    const Outcome out = m.Key(Wd::TreeKey::Enter);
    CHECK(out.activate == 3);
    CHECK(!out.selectionChanged);
}

void TestEnterOnBranchTogglesInsteadOfActivate() {
    HierarchyModel m;
    m.Sync(SmallScene());
    m.Select(1);
    const Outcome out = m.Key(Wd::TreeKey::Enter);
    CHECK(out.activate == NoId);
    CHECK(m.RowCount() == 2);                                 // recolheu o Player
}

void TestReparentRefusesCyclesAndSelf() {
    HierarchyModel m;
    m.Sync(SmallScene());
    CHECK(m.CanReparent(4, 1));        // Luz vira filha do Player
    CHECK(m.CanReparent(3, 1));        // Ponta vira filha do Player
    CHECK(!m.CanReparent(1, 3));       // Player nao pode virar filho da propria Ponta (ciclo)
    CHECK(!m.CanReparent(2, 2));       // nem de si mesmo
    CHECK(!m.CanReparent(4, 99));      // pai que nao existe
}

void TestLargeTreeHasOnlyVisibleRowsInTheWindow() {
    HierarchyModel m;
    std::vector<Item> items;
    items.reserve(10000);
    for (Id i = 1; i <= 10000; ++i) {
        const Id parent = (i % 100 == 1) ? NoId : ((i - 1) / 100) * 100 + 1;   // 100 raizes, 99 filhos cada
        items.push_back(Obj(i, parent, "No " + std::to_string(i)));
    }
    CHECK(m.Sync(items));
    CHECK(m.RowCount() == 10000);
    const float rowH = 22.f;
    const RowRange window = VisibleRange(m.RowCount(), 5000.f * rowH, rowH, 400.f);
    CHECK(window.count > 0 && window.count <= 22);   // 400 px de janela + linhas parciais
    CHECK(window.first == 5000);
    CHECK(m.IdOfRow(window.first) == 5001);
    CHECK(!m.Sync(items));                            // mesma estrutura: nada e refeito
}

void TestVisibleRangeEdges() {
    CHECK(VisibleRange(0, 0.f, 22.f, 400.f).count == 0);
    CHECK(VisibleRange(10, 1000.f, 22.f, 400.f).count == 0);   // rolou para fora
    CHECK(VisibleRange(10, -50.f, 22.f, 400.f).first == 0);    // rolagem negativa vira zero
    const RowRange last = VisibleRange(10, 0.f, 22.f, 400.f);
    CHECK(last.first == 0 && last.count == 10);                // mais linhas que a janela cabem todas
}

}  // namespace

int main() {
    TestRowsFollowTheTreeOrderAndDepth();
    TestSameStructureDoesNotRebuild();
    TestSelectionFollowsIdAfterInsert();
    TestItemsMissingParentBecomeRoots();
    TestCycleDoesNotHideObjects();
    TestCollapseMovesSelectionToTheCollapsedParent();
    TestCollapsedStateSurvivesRebuild();
    TestArrowKeysMoveAndExpand();
    TestLeftGoesToParentFromLeaf();
    TestEnterOnLeafRequestsActivate();
    TestEnterOnBranchTogglesInsteadOfActivate();
    TestReparentRefusesCyclesAndSelf();
    TestLargeTreeHasOnlyVisibleRowsInTheWindow();
    TestVisibleRangeEdges();

    std::printf("hierarchy model: %d checks, %d failed\n", run, failed);
    return failed == 0 ? 0 : 1;
}
