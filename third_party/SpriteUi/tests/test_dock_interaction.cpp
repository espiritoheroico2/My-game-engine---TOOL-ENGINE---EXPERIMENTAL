// Camada L3 (docking, interacao): roteiros de eventos do ponteiro sobre o controlador de arrasto.
// Executavel proprio, sem o harness principal.

#include "SpriteUi/Widgets/DockInteraction.hpp"
#include "SpriteUi/Core/DrawOrder.hpp"
#include "SpriteUi/Widgets/DockGuides.hpp"
#include "SpriteUi/Widgets/DockOps.hpp"

#include <array>
#include <cmath>
#include <cstdio>

namespace {

using namespace ToolEngine::SpriteUi;
using namespace ToolEngine::SpriteUi::Widgets;

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

// Duas folhas lado a lado em uma tela 200x100: A a esquerda, B a direita.
const std::array<DockLeafRect, 2> kLeaves = {
    DockLeafRect{UIHandle{1, 1}, Rect{{0, 0}, {100, 100}}},
    DockLeafRect{UIHandle{2, 1}, Rect{{100, 0}, {100, 100}}},
};
const UIHandle kLeafA = UIHandle{1, 1};
const UIHandle kWindow = UIHandle{9, 1};

void TestDropSlotZones() {
    current = "TestDropSlotZones";
    const Rect leaf{{0, 0}, {100, 100}};
    CHECK(ComputeDropSlot(leaf, {50, 50}, 0.25f) == DockSlot::Center);
    CHECK(ComputeDropSlot(leaf, {5, 50}, 0.25f) == DockSlot::Left);
    CHECK(ComputeDropSlot(leaf, {95, 50}, 0.25f) == DockSlot::Right);
    CHECK(ComputeDropSlot(leaf, {50, 5}, 0.25f) == DockSlot::Top);
    CHECK(ComputeDropSlot(leaf, {50, 95}, 0.25f) == DockSlot::Bottom);
    CHECK(ComputeDropSlot(leaf, {150, 50}, 0.25f) == DockSlot::None);              // fora da folha
    CHECK(ComputeDropSlot(Rect{{0, 0}, {0, 0}}, {0, 0}, 0.25f) == DockSlot::None); // sem area
    CHECK(ComputeDropSlot(leaf, {50, 50}, 0.9f) == DockSlot::None);                // fracao invalida
}

void TestClickWithoutMoveDoesNothing() {
    current = "TestClickWithoutMoveDoesNothing";
    DockDragController c;
    c.BeginPress(kWindow, {20, 20});
    CHECK(c.IsPressed());
    CHECK(!c.IsDragging());
    const DockDropResult r = c.Release({20, 20}, kLeaves);
    CHECK(r.action == DockAction::None);
    CHECK(!c.IsPressed());
}

void TestJitterBelowThresholdIsNotADrag() {
    current = "TestJitterBelowThresholdIsNotADrag";
    DockDragController c;
    c.BeginPress(kWindow, {20, 20});
    CHECK(!c.Move({23, 20})); // 3 px, abaixo do limiar de 6 px
    CHECK(!c.Move({21, 22}));
    CHECK(!c.IsDragging());
    CHECK(c.Release({21, 22}, kLeaves).action == DockAction::None);
}

void TestDragToLeftEdgeDocks() {
    current = "TestDragToLeftEdgeDocks";
    DockDragController c;
    c.BeginPress(kWindow, {50, 50});
    CHECK(c.Move({60, 50})); // 10 px: arrasto ativo
    CHECK(c.IsDragging());
    const DockDropResult r = c.Release({5, 50}, kLeaves);
    CHECK(r.action == DockAction::Dock);
    CHECK(r.slot == DockSlot::Left);
    CHECK(r.targetLeaf == kLeafA);
}

void TestDragToCenterTabs() {
    current = "TestDragToCenterTabs";
    DockDragController c;
    c.BeginPress(kWindow, {150, 50});
    c.Move({140, 50});
    const DockDropResult r = c.Release({150, 50}, kLeaves);
    CHECK(r.action == DockAction::Tab);
    CHECK(r.targetLeaf == UIHandle(2, 1));
}

void TestDragOutsideFloats() {
    current = "TestDragOutsideFloats";
    DockDragController c;
    c.BeginPress(kWindow, {50, 50});
    c.Move({400, 300});
    CHECK(c.Release({400, 300}, kLeaves).action == DockAction::Float);
}

void TestCancelDropsTheGesture() {
    current = "TestCancelDropsTheGesture";
    DockDragController c;
    c.BeginPress(kWindow, {50, 50});
    c.Move({80, 50});
    c.Cancel();
    CHECK(!c.IsPressed());
    CHECK(!c.IsDragging());
    // Soltar depois do cancelamento nao pode produzir acao.
    CHECK(c.Release({5, 50}, kLeaves).action == DockAction::None);
}

void TestNoSpontaneousMovementWithoutInput() {
    current = "TestNoSpontaneousMovementWithoutInput";
    DockDragController c;
    c.BeginPress(kWindow, {50, 50});
    // Mil quadros com o ponteiro parado: o gesto nao vira arrasto sozinho.
    bool started = false;
    for (int frame = 0; frame < 1000; ++frame)
        started = started || c.Move({50, 50});
    CHECK(!started);
    CHECK(!c.IsDragging());
    CHECK(c.Release({50, 50}, kLeaves).action == DockAction::None);
}

void TestGestureResetsAfterRelease() {
    current = "TestGestureResetsAfterRelease";
    DockDragController c;
    c.BeginPress(kWindow, {50, 50});
    c.Move({90, 50});
    (void)c.Release({5, 50}, kLeaves);
    c.BeginPress(kWindow, {50, 50});
    CHECK(!c.IsDragging()); // novo gesto comeca limpo
    CHECK(c.Release({50, 50}, kLeaves).action == DockAction::None);
}

void TestClampKeepsWindowOnScreen() {
    current = "TestClampKeepsWindowOnScreen";
    const Rect screen{{0, 0}, {200, 100}};
    const Rect off = ClampWindowToScreen(Rect{{-50, 150}, {80, 40}}, screen);
    CHECK(off.position.x == 0.0f);
    CHECK(off.position.y == 60.0f); // 100 - 40
    CHECK(off.size.x == 80.0f && off.size.y == 40.0f);

    const Rect big = ClampWindowToScreen(Rect{{10, 10}, {500, 300}}, screen);
    CHECK(big.size.x == 200.0f && big.size.y == 100.0f);
    CHECK(big.position.x == 0.0f && big.position.y == 0.0f);

    const Rect inside = ClampWindowToScreen(Rect{{30, 20}, {80, 40}}, screen);
    CHECK(inside.position.x == 30.0f && inside.position.y == 20.0f);
}

// Arvore de docking: folhas A, B, W e C (ids de conteudo).
const UIHandle kA{1, 1}, kB{2, 1}, kW{3, 1}, kC{4, 1};
const Rect kScreen{{0, 0}, {200, 100}};

void TestDockLeftOfRightLeafPlacesWindowLeft() {
    current = "TestDockLeftOfRightLeafPlacesWindowLeft";
    UIDockNode root(kA);
    CHECK(root.Split(DockSlot::Right, 0.5f, kB));
    DockDropResult drop{DockAction::Dock, DockSlot::Left, kB};
    CHECK(ApplyDropResult(root, kW, drop));
    root.Layout(kScreen);
    const UIDockNode* w = FindLeafContaining(root, kW);
    const UIDockNode* b = FindLeafContaining(root, kB);
    const UIDockNode* a = FindLeafContaining(root, kA);
    CHECK(w && b && a);
    if (!w || !b || !a) return;
    CHECK(w->bounds.position.x < b->bounds.position.x); // W a esquerda de B
    CHECK(a->bounds.size.x > 0.0f);                     // A continua visivel
}

void TestTabJoinsLeafAndActivatesWindow() {
    current = "TestTabJoinsLeafAndActivatesWindow";
    UIDockNode root(kA);
    CHECK(ApplyDropResult(root, kW, DockDropResult{DockAction::Tab, DockSlot::None, kA}));
    CHECK(root.tabs.TabCount() == 2);
    CHECK(FindLeafContaining(root, kA) == FindLeafContaining(root, kW));
    CHECK(root.VisibleContent() == kW); // a aba nova fica ativa
}

void TestFloatRemovesAndCollapses() {
    current = "TestFloatRemovesAndCollapses";
    UIDockNode root(kA);
    CHECK(root.Split(DockSlot::Right, 0.5f, kB));
    CHECK(ApplyDropResult(root, kB, DockDropResult{DockAction::Float, DockSlot::None, {}}));
    CHECK(FindLeafContaining(root, kB) == nullptr);
    CHECK(!root.isSplit); // sobrou so A: o pai colapsou
    CHECK(root.content == kA);
}

void TestRemovingOneOfTwoTabsKeepsOtherAsContent() {
    current = "TestRemovingOneOfTwoTabsKeepsOtherAsContent";
    UIDockNode root(kA);
    CHECK(ApplyDropResult(root, kW, DockDropResult{DockAction::Tab, DockSlot::None, kA}));
    CHECK(RemoveFromDock(root, kW));
    CHECK(root.tabs.TabCount() == 0);
    CHECK(root.content == kA);
    CHECK(!RemoveFromDock(root, kW)); // ja nao esta na arvore
}

void TestDockOntoTabbedLeafKeepsAllTabs() {
    current = "TestDockOntoTabbedLeafKeepsAllTabs";
    UIDockNode root(kA);
    CHECK(ApplyDropResult(root, kW, DockDropResult{DockAction::Tab, DockSlot::None, kA}));
    // Alvo: a aba ativa (W). Encaixar C a direita nao pode perder A nem W.
    CHECK(ApplyDropResult(root, kC, DockDropResult{DockAction::Dock, DockSlot::Right, kW}));
    CHECK(FindLeafContaining(root, kA) != nullptr);
    CHECK(FindLeafContaining(root, kW) != nullptr);
    CHECK(FindLeafContaining(root, kC) != nullptr);
}

void TestInvalidTargetLeavesTreeUnchanged() {
    current = "TestInvalidTargetLeavesTreeUnchanged";
    UIDockNode root(kA);
    CHECK(!ApplyDropResult(root, kW, DockDropResult{DockAction::Dock, DockSlot::Left, UIHandle{99, 1}}));
    CHECK(FindLeafContaining(root, kW) == nullptr); // W nao foi tirado nem colocado
    CHECK(FindLeafContaining(root, kA) == &root);
    CHECK(!ApplyDropResult(root, kA, DockDropResult{DockAction::Dock, DockSlot::None, kA}));
    CHECK(!ApplyDropResult(root, kA, DockDropResult{DockAction::None, DockSlot::None, {}}));
}

void TestDroppingWindowOntoItsOwnGroupIsIgnored() {
    current = "TestDroppingWindowOntoItsOwnGroupIsIgnored";
    UIDockNode root(kA);
    CHECK(ApplyDropResult(root, kW, DockDropResult{DockAction::Tab, DockSlot::None, kA}));
    CHECK(!ApplyDropResult(root, kW, DockDropResult{DockAction::Tab, DockSlot::None, kW}));
    CHECK(root.tabs.TabCount() == 2); // nada mudou
}

// Grupo de abas: a janela fica so nas abas (content invalido), e o conteudo visivel e a aba ativa.
void TestCenterSplitMovesTheWindowIntoTheGroup() {
    current = "TestCenterSplitMovesTheWindowIntoTheGroup";
    UIDockNode root(kA);
    CHECK(root.Split(DockSlot::Center, 0.5f, kB));
    CHECK(root.tabs.TabCount() == 2);
    CHECK(!root.content.IsValid()); // A esta so na aba, nao tambem em content
    CHECK(root.tabs.IndexOf(kA).value_or(99) == 0);
    CHECK(root.VisibleContent() == kB);
    CHECK(FindLeafContaining(root, kA) == &root);
    CHECK(FindLeafContaining(root, kB) == &root);
}

void TestWindowCannotBeInAGroupTwice() {
    current = "TestWindowCannotBeInAGroupTwice";
    UITabGroupComponent group;
    CHECK(group.AddTab("a", kA));
    CHECK(!group.AddTab("again", kA));
    CHECK(!group.AddTab("none", UIHandle{}));
    CHECK(group.TabCount() == 1);

    UIDockNode root(kA);
    CHECK(!root.Split(DockSlot::Center, 0.5f, kA)); // ja e o conteudo da folha: nada muda
    CHECK(root.content == kA && root.tabs.TabCount() == 0);
    CHECK(root.Split(DockSlot::Center, 0.5f, kB));
    CHECK(!root.Split(DockSlot::Center, 0.5f, kB)); // ja esta no grupo: nada muda
    CHECK(root.tabs.TabCount() == 2);
}

void TestSelectContentActivatesItsTab() {
    current = "TestSelectContentActivatesItsTab";
    UITabGroupComponent group;
    CHECK(group.AddTab("a", kA));
    CHECK(group.AddTab("b", kB));
    CHECK(group.AddTab("c", kC));
    CHECK(group.GetActiveContent() == kA); // a primeira aba fica ativa
    CHECK(group.SelectContent(kC));
    CHECK(group.GetActiveTabIndex() == 2);
    CHECK(group.GetActiveContent() == kC);
    CHECK(!group.SelectContent(kW)); // fora do grupo: a aba ativa nao muda
    CHECK(group.GetActiveContent() == kC);
}

void TestRemovingAnEarlierTabKeepsTheActiveWindow() {
    current = "TestRemovingAnEarlierTabKeepsTheActiveWindow";
    UITabGroupComponent group;
    CHECK(group.AddTab("a", kA));
    CHECK(group.AddTab("b", kB));
    CHECK(group.AddTab("c", kC));
    CHECK(group.SelectContent(kC));
    CHECK(group.RemoveContent(kA));
    CHECK(group.GetActiveContent() == kC); // mesma janela, indice menor
    CHECK(group.GetActiveTabIndex() == 1);
    CHECK(group.RemoveContent(kC)); // a ativa era a ultima: a anterior fica ativa
    CHECK(group.GetActiveContent() == kB);
    CHECK(!group.RemoveContent(kC)); // ja saiu do grupo
}

void TestLastWindowOfAGroupLeavesAnEmptyLeaf() {
    current = "TestLastWindowOfAGroupLeavesAnEmptyLeaf";
    UIDockNode root(kA);
    CHECK(ApplyDropResult(root, kW, DockDropResult{DockAction::Tab, DockSlot::None, kA}));
    CHECK(RemoveFromDock(root, kW)); // volta a uma janela so, sem grupo
    CHECK(root.content == kA && root.tabs.TabCount() == 0);
    CHECK(RemoveFromDock(root, kA));
    CHECK(!root.content.IsValid() && root.tabs.TabCount() == 0);
    CHECK(FindLeafContaining(root, kA) == nullptr);
}

void TestSplittingAGroupMovesAllItsTabsToOneSide() {
    current = "TestSplittingAGroupMovesAllItsTabsToOneSide";
    UIDockNode root(kA);
    CHECK(root.Split(DockSlot::Center, 0.5f, kB)); // grupo [A, B] com B ativa
    CHECK(root.Split(DockSlot::Right, 0.5f, kC));
    CHECK(root.isSplit);
    UIDockNode* group = FindLeafContaining(root, kA);
    CHECK(group != nullptr && group == root.firstChild.get());
    if (group == nullptr) return;
    CHECK(group->tabs.TabCount() == 2);
    CHECK(group->VisibleContent() == kB); // a aba ativa continua ativa
    CHECK(FindLeafContaining(root, kC) == root.secondChild.get());
}

// Comparacao em pixels com folga: a proporcao do divisor passa por float e nao e exata.
bool NearPx(float a, float b) {
    return std::fabs(a - b) < 0.01f;
}

void TestDockAboveAndBelowStackWindows() {
    current = "TestDockAboveAndBelowStackWindows";
    UIDockNode root(kA);
    CHECK(ApplyDropResult(root, kW, DockDropResult{DockAction::Dock, DockSlot::Top, kA}));
    root.Layout(kScreen);
    const UIDockNode* top = FindLeafContaining(root, kW);
    const UIDockNode* base = FindLeafContaining(root, kA);
    CHECK(top && base);
    if (!top || !base) return;
    CHECK(top->bounds.position.y < base->bounds.position.y); // W acima de A
    CHECK(top->bounds.size.x == kScreen.size.x && base->bounds.size.x == kScreen.size.x);
    CHECK(top->bounds.size.y + base->bounds.size.y == kScreen.size.y); // sem folga nem sobra

    UIDockNode bottomRoot(kA);
    CHECK(ApplyDropResult(bottomRoot, kW, DockDropResult{DockAction::Dock, DockSlot::Bottom, kA}));
    bottomRoot.Layout(kScreen);
    const UIDockNode* below = FindLeafContaining(bottomRoot, kW);
    const UIDockNode* upper = FindLeafContaining(bottomRoot, kA);
    CHECK(below && upper);
    if (!below || !upper) return;
    CHECK(below->bounds.position.y > upper->bounds.position.y); // W abaixo de A
    CHECK(below->bounds.position.y + below->bounds.size.y == kScreen.size.y);
}

void TestDividerStopsAtMinimumSides() {
    current = "TestDividerStopsAtMinimumSides";
    const Rect area{{0, 0}, {300, 100}};
    UIDockNode root(kA);
    CHECK(root.Split(DockSlot::Right, 0.5f, kB));

    root.Layout(area);
    CHECK(DragSplitDivider(root, 10.0f, 80.0f)); // arrasta demais para a esquerda
    root.Layout(area);
    CHECK(NearPx(FindLeafContaining(root, kA)->bounds.size.x, 80.0f));
    CHECK(NearPx(FindLeafContaining(root, kB)->bounds.size.x, 220.0f));

    CHECK(DragSplitDivider(root, 290.0f, 80.0f)); // arrasta demais para a direita
    root.Layout(area);
    CHECK(NearPx(FindLeafContaining(root, kB)->bounds.size.x, 80.0f));
    CHECK(NearPx(FindLeafContaining(root, kA)->bounds.size.x, 220.0f));

    CHECK(DragSplitDivider(root, 150.0f, 80.0f)); // dentro dos limites: a posicao vale
    root.Layout(area);
    CHECK(NearPx(FindLeafContaining(root, kA)->bounds.size.x, 150.0f));
}

void TestDividerOfColumnUsesHeight() {
    current = "TestDividerOfColumnUsesHeight";
    const Rect area{{0, 0}, {300, 200}};
    UIDockNode root(kA);
    CHECK(root.Split(DockSlot::Bottom, 0.5f, kB));
    root.Layout(area);
    CHECK(DragSplitDivider(root, 5.0f, 60.0f));
    root.Layout(area);
    CHECK(NearPx(FindLeafContaining(root, kA)->bounds.size.y, 60.0f));
    CHECK(NearPx(FindLeafContaining(root, kB)->bounds.size.y, 140.0f));
}

void TestDividerRejectsAreaTooSmallForTwoMinimums() {
    current = "TestDividerRejectsAreaTooSmallForTwoMinimums";
    UIDockNode root(kA);
    CHECK(root.Split(DockSlot::Right, 0.5f, kB));
    root.Layout(Rect{{0, 0}, {300, 100}});
    const float before = root.splitRatio;
    CHECK(!DragSplitDivider(root, 150.0f, 200.0f));
    CHECK(root.splitRatio == before); // nada muda quando recusa

    UIDockNode leaf(kA);
    CHECK(!DragSplitDivider(leaf, 10.0f, 10.0f)); // folha nao tem divisor
}

// Clique com 2 px de tremor na barra: nao e arrasto, nao tira a janela do encaixe.
void TestTwoPixelTitleClickKeepsWindowDocked() {
    current = "TestTwoPixelTitleClickKeepsWindowDocked";
    DockDragController c;
    c.BeginPress(kWindow, {50, 50});
    CHECK(!c.Move({52, 51}));
    CHECK(!c.IsDragging());
    const DockDropResult r = c.Release({52, 51}, kLeaves);
    CHECK(r.action == DockAction::None);   // nem Float (desencaixar), nem Dock/Tab
    CHECK(r.targetLeaf == UIHandle{});
}

// Limiar padrao de 6 px: 5 px nao arrasta, 7 px arrasta.
void TestDefaultDragThresholdIsSixPixels() {
    current = "TestDefaultDragThresholdIsSixPixels";
    CHECK(DockInteractionConfig{}.dragThreshold == 6.0f);
    DockDragController c;
    c.BeginPress(kWindow, {50, 50});
    CHECK(!c.Move({55, 50}));
    CHECK(c.Move({57, 50}));
}

// Histerese: o encaixe nao troca enquanto o ponteiro treme na divisa entre duas zonas.
void TestDropSlotHysteresisDoesNotFlicker() {
    current = "TestDropSlotHysteresisDoesNotFlicker";
    const Rect leaf{{0, 0}, {100, 100}};   // zona de 25 px em cada borda
    // Sem encaixe anterior: a regra normal.
    CHECK(ComputeDropSlotSticky(leaf, {24, 50}, 0.25f, DockSlot::None, 0.15f) == DockSlot::Left);
    CHECK(ComputeDropSlotSticky(leaf, {26, 50}, 0.25f, DockSlot::None, 0.15f) == DockSlot::Center);
    // Com Left anterior: 26 px ainda e Left (dentro da margem de 15 por cento); 30 px vai para o centro.
    CHECK(ComputeDropSlotSticky(leaf, {26, 50}, 0.25f, DockSlot::Left, 0.15f) == DockSlot::Left);
    CHECK(ComputeDropSlotSticky(leaf, {30, 50}, 0.25f, DockSlot::Left, 0.15f) == DockSlot::Center);
    // Com Center anterior: uma borda a 22 px (dentro da zona, fora da margem de saida) ainda e Center.
    CHECK(ComputeDropSlotSticky(leaf, {22, 50}, 0.25f, DockSlot::Center, 0.15f) == DockSlot::Center);
    CHECK(ComputeDropSlotSticky(leaf, {5, 50}, 0.25f, DockSlot::Center, 0.15f) == DockSlot::Left);
    // Histerese zero desliga a regra.
    CHECK(ComputeDropSlotSticky(leaf, {26, 50}, 0.25f, DockSlot::Left, 0.0f) == DockSlot::Center);
}

// Durante o arrasto, Track devolve o mesmo encaixe que Release usaria, com a histerese.
void TestTrackPreviewMatchesReleaseWithHysteresis() {
    current = "TestTrackPreviewMatchesReleaseWithHysteresis";
    const std::array<DockLeafRect, 1> leaves = {DockLeafRect{UIHandle{1, 1}, Rect{{0, 0}, {100, 100}}}};
    DockDragController c;
    c.BeginPress(kWindow, {50, 50});
    CHECK(c.Move({24, 50}));                         // arrasto ativo
    DockDropResult t = c.Track({24, 50}, leaves);
    CHECK(t.action == DockAction::Dock && t.slot == DockSlot::Left);
    t = c.Track({26, 50}, leaves);                   // treme na divisa: continua Left
    CHECK(t.action == DockAction::Dock && t.slot == DockSlot::Left);
    t = c.Track({30, 50}, leaves);                   // entrou de vez no centro
    CHECK(t.action == DockAction::Tab);
    const DockDropResult r = c.Release({30, 50}, leaves);
    CHECK(r.action == DockAction::Tab);
}

// Fora de todas as folhas, a previa e Float; Track sem arrasto nao faz nada.
void TestTrackOutsideLeavesAndWithoutDrag() {
    current = "TestTrackOutsideLeavesAndWithoutDrag";
    DockDragController c;
    CHECK(c.Track({50, 50}, kLeaves).action == DockAction::None);   // sem arrasto
    c.BeginPress(kWindow, {50, 50});
    c.Move({200, 50});
    CHECK(c.Track({500, 500}, kLeaves).action == DockAction::Float);
}

// Icones de encaixe: cinco itens na camada de arrasto, com opacidade media, so sobre a folha dada.
void TestDockGuidesUseDragLayerAndMediumAlpha() {
    current = "TestDockGuidesUseDragLayerAndMediumAlpha";
    const Rect leaf{{100, 100}, {300, 200}};
    const auto items = BuildDockGuideItems(UIHandle{7, 1}, leaf, DockSlot::Right);
    CHECK(items.size() == 5);
    if (items.size() != 5) return;
    for (const auto& item : items) {
        CHECK(item.layer == static_cast<std::int32_t>(UILayer::DragPreview));
        CHECK((item.handle == UIHandle{7, 1}));
        const float alpha = item.tintPremultipliedLinear.w;
        CHECK(alpha >= 0.45f && alpha <= 0.60f);
        const Rect box{item.layout.resolvedPosition, item.layout.resolvedSize};
        const Vec2 far{box.position.x + box.size.x - 1.0f, box.position.y + box.size.y - 1.0f};
        CHECK(leaf.Contains(box.position) && leaf.Contains(far));
    }
    // Ordem: Left, Right, Top, Bottom, Center. Right e o destacado, com alfa maior.
    CHECK(items[1].tintPremultipliedLinear.w > items[0].tintPremultipliedLinear.w);
    CHECK(items[0].layout.resolvedPosition.x < items[1].layout.resolvedPosition.x);
    CHECK(items[2].layout.resolvedPosition.y < items[3].layout.resolvedPosition.y);
    CHECK(BuildDockGuideItems(UIHandle{}, leaf, DockSlot::Left).empty());        // sem dono
    CHECK(BuildDockGuideItems(UIHandle{7, 1}, Rect{{0, 0}, {0, 0}}, DockSlot::Left).empty()); // sem area
}

} // namespace

int main() {
    TestDropSlotZones();
    TestClickWithoutMoveDoesNothing();
    TestJitterBelowThresholdIsNotADrag();
    TestDragToLeftEdgeDocks();
    TestDragToCenterTabs();
    TestDragOutsideFloats();
    TestCancelDropsTheGesture();
    TestNoSpontaneousMovementWithoutInput();
    TestGestureResetsAfterRelease();
    TestClampKeepsWindowOnScreen();
    TestDockLeftOfRightLeafPlacesWindowLeft();
    TestTabJoinsLeafAndActivatesWindow();
    TestFloatRemovesAndCollapses();
    TestRemovingOneOfTwoTabsKeepsOtherAsContent();
    TestDockOntoTabbedLeafKeepsAllTabs();
    TestInvalidTargetLeavesTreeUnchanged();
    TestDroppingWindowOntoItsOwnGroupIsIgnored();
    TestCenterSplitMovesTheWindowIntoTheGroup();
    TestWindowCannotBeInAGroupTwice();
    TestSelectContentActivatesItsTab();
    TestRemovingAnEarlierTabKeepsTheActiveWindow();
    TestLastWindowOfAGroupLeavesAnEmptyLeaf();
    TestSplittingAGroupMovesAllItsTabsToOneSide();
    TestDockAboveAndBelowStackWindows();
    TestDividerStopsAtMinimumSides();
    TestDividerOfColumnUsesHeight();
    TestDividerRejectsAreaTooSmallForTwoMinimums();
    TestTwoPixelTitleClickKeepsWindowDocked();
    TestDefaultDragThresholdIsSixPixels();
    TestDropSlotHysteresisDoesNotFlicker();
    TestTrackPreviewMatchesReleaseWithHysteresis();
    TestTrackOutsideLeavesAndWithoutDrag();
    TestDockGuidesUseDragLayerAndMediumAlpha();
    std::printf("dock interaction: %d/%d checks passed\n", run - failed, run);
    return failed == 0 ? 0 : 1;
}
