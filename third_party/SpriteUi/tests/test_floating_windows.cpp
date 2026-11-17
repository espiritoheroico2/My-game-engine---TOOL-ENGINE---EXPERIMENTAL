// Camada L3 (janelas flutuantes): modelo puro sobre o dock. Executavel proprio, sem o harness principal.

#include "SpriteUi/Widgets/DockOps.hpp"
#include "SpriteUi/Widgets/FloatingWindows.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

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

const UIHandle kA{1, 1};
const UIHandle kB{2, 1};
const UIHandle kC{3, 1};
const UIHandle kW{4, 1};
const UIHandle kX{5, 1};
const Rect kCanvas{{0.0f, 0.0f}, {800.0f, 600.0f}};

// Raiz com A a esquerda e, a direita, um grupo de abas B + C (C ativa).
void BuildGroupedTree(UIDockNode& root) {
    root.content = kA;
    root.Split(DockSlot::Right, 0.5f, kB);
    UIDockNode* right = FindLeafContaining(root, kB);
    right->Split(DockSlot::Center, 0.5f, kC);
}

void TestClampKeepsTitleStripOnCanvas() {
    current = "TestClampKeepsTitleStripOnCanvas";
    FloatingWindowManager m(kCanvas);
    UIDockNode root;
    CHECK(m.Float(root, kW, Rect{{100.0f, 100.0f}, {200.0f, 150.0f}}));

    CHECK(m.Move(kW, -5000.0f, 0.0f));
    CHECK(m.Find(kW)->rect.position.x == kFloatingGrabWidth - 200.0f); // some a faixa de 48 px, a borda direita fica visivel
    CHECK(m.Move(kW, 10000.0f, 0.0f));
    CHECK(m.Find(kW)->rect.position.x == 800.0f - kFloatingGrabWidth);
    CHECK(m.Move(kW, 0.0f, -5000.0f));
    CHECK(m.Find(kW)->rect.position.y == 0.0f);                   // barra de titulo no topo do canvas
    CHECK(m.Move(kW, 0.0f, 10000.0f));
    CHECK(m.Find(kW)->rect.position.y == 600.0f - 30.0f);         // barra de titulo inteira dentro do canvas

    UIDockNode rootMove;
    CHECK(m.Float(rootMove, kC, Rect{{100.0f, 100.0f}, {200.0f, 150.0f}}));
    CHECK(m.Move(kC, 10.0f, -20.0f));                             // movimento dentro do canvas e exato
    CHECK(m.Find(kC)->rect.position.x == 110.0f);
    CHECK(m.Find(kC)->rect.position.y == 80.0f);

    // Flutuar uma janela fora do canvas tambem a reencaixa.
    UIDockNode root2;
    CHECK(m.Float(root2, kX, Rect{{-3000.0f, 9000.0f}, {200.0f, 150.0f}}));
    const Rect r = m.Find(kX)->rect;
    CHECK(r.position.x + r.size.x >= kFloatingGrabWidth);
    CHECK(r.position.y == 600.0f - 30.0f);
}

void TestSetCanvasReclamps() {
    current = "TestSetCanvasReclamps";
    FloatingWindowManager m(kCanvas);
    UIDockNode root;
    CHECK(m.Float(root, kW, Rect{{700.0f, 500.0f}, {200.0f, 150.0f}}));
    m.SetCanvas(Rect{{0.0f, 0.0f}, {400.0f, 300.0f}});
    CHECK(m.Find(kW)->rect.position.x == 400.0f - kFloatingGrabWidth);
    CHECK(m.Find(kW)->rect.position.y == 300.0f - 30.0f);
}

void TestResizeRespectsMinimum() {
    current = "TestResizeRespectsMinimum";
    UIDockNode root;
    {
        FloatingWindowManager m(kCanvas);
        CHECK(m.Float(root, kW, Rect{{100.0f, 100.0f}, {200.0f, 150.0f}}, Vec2{150.0f, 100.0f}));
        CHECK(m.Resize(kW, ResizeEdge::Right, -1000.0f, 0.0f));
        CHECK(m.Find(kW)->rect.size.x == 150.0f);                    // minimo em largura
        CHECK(m.Find(kW)->rect.position.x == 100.0f);
    }
    {
        FloatingWindowManager m(kCanvas);
        UIDockNode root2;
        CHECK(m.Float(root2, kW, Rect{{100.0f, 100.0f}, {200.0f, 150.0f}}, Vec2{150.0f, 100.0f}));
        // Borda esquerda puxada para dentro alem do minimo: a borda direita (300) fica parada.
        CHECK(m.Resize(kW, ResizeEdge::Left, 1000.0f, 0.0f));
        CHECK(m.Find(kW)->rect.size.x == 150.0f);
        CHECK(m.Find(kW)->rect.position.x + m.Find(kW)->rect.size.x == 300.0f);
    }
    {
        FloatingWindowManager m(kCanvas);
        UIDockNode root3;
        CHECK(m.Float(root3, kW, Rect{{100.0f, 100.0f}, {200.0f, 150.0f}}, Vec2{150.0f, 100.0f}));
        CHECK(m.Resize(kW, ResizeEdge::Bottom, 0.0f, -1000.0f));
        CHECK(m.Find(kW)->rect.size.y == 100.0f);                    // minimo em altura
        CHECK(m.Resize(kW, ResizeEdge::TopLeft, 1000.0f, 1000.0f));  // canto: nao passa do minimo
        CHECK(m.Find(kW)->rect.size.x == 150.0f);
        CHECK(m.Find(kW)->rect.size.y == 100.0f);
    }
    {
        FloatingWindowManager m(kCanvas);
        UIDockNode root4;
        CHECK(m.Float(root4, kW, Rect{{100.0f, 100.0f}, {200.0f, 150.0f}}));
        CHECK(m.Resize(kW, ResizeEdge::BottomRight, 30.0f, 20.0f));  // crescer funciona
        CHECK(m.Find(kW)->rect.size.x == 230.0f);
        CHECK(m.Find(kW)->rect.size.y == 170.0f);
        CHECK(!m.Resize(kW, ResizeEdge::None, 5.0f, 5.0f));          // sem borda: nada
    }
}

void TestMaximizeRestoreAndMinimize() {
    current = "TestMaximizeRestoreAndMinimize";
    FloatingWindowManager m(kCanvas);
    UIDockNode root;
    const Rect original{{100.0f, 120.0f}, {200.0f, 150.0f}};
    CHECK(m.Float(root, kW, original));
    CHECK(m.Maximize(kW));
    CHECK(m.Find(kW)->state == FloatState::Maximized);
    CHECK(m.Find(kW)->rect.position.x == 0.0f && m.Find(kW)->rect.size.x == 800.0f);
    CHECK(!m.Move(kW, 10.0f, 10.0f));                                 // maximizada nao se move
    CHECK(!m.Resize(kW, ResizeEdge::Right, 10.0f, 0.0f));
    CHECK(m.Restore(kW));
    CHECK(m.Find(kW)->state == FloatState::Normal);
    CHECK(m.Find(kW)->rect.position.x == original.position.x && m.Find(kW)->rect.size.y == original.size.y);
    CHECK(m.Minimize(kW));
    CHECK(m.Find(kW)->state == FloatState::Minimized);
    CHECK(m.Restore(kW));
    CHECK(!m.Restore(kW));                                            // ja normal: nada muda
}

void TestBringToFrontKeepsOrderAndUniqueZ() {
    current = "TestBringToFrontKeepsOrderAndUniqueZ";
    FloatingWindowManager m(kCanvas);
    UIDockNode r1, r2, r3;
    CHECK(m.Float(r1, kA, Rect{{10.0f, 10.0f}, {200.0f, 150.0f}}));
    CHECK(m.Float(r2, kB, Rect{{20.0f, 20.0f}, {200.0f, 150.0f}}));
    CHECK(m.Float(r3, kC, Rect{{30.0f, 30.0f}, {200.0f, 150.0f}}));
    CHECK(m.BringToFront(kA));
    CHECK(m.Find(kA)->zOrder > m.Find(kC)->zOrder);
    CHECK(m.Find(kB)->zOrder < m.Find(kC)->zOrder);                  // a ordem dos outros se mantem
    CHECK(m.Find(kA)->zOrder != m.Find(kB)->zOrder && m.Find(kA)->zOrder != m.Find(kC)->zOrder);
    const std::uint32_t topBefore = m.Find(kA)->zOrder;
    CHECK(m.BringToFront(kA));                                       // ja e a do topo: nada muda
    CHECK(m.Find(kA)->zOrder == topBefore);
    CHECK(!m.BringToFront(kW));                                      // janela desconhecida

    // Contador perto do fim: o estouro renumera sem perder a ordem relativa e sem valores repetidos.
    FloatingWindowManager near(kCanvas, std::numeric_limits<std::uint32_t>::max() - 2u);
    UIDockNode n1, n2, n3;
    CHECK(near.Float(n1, kA, Rect{{10.0f, 10.0f}, {200.0f, 150.0f}}));
    CHECK(near.Float(n2, kB, Rect{{20.0f, 20.0f}, {200.0f, 150.0f}}));
    CHECK(near.Float(n3, kC, Rect{{30.0f, 30.0f}, {200.0f, 150.0f}}));
    CHECK(near.Find(kA)->zOrder < near.Find(kB)->zOrder);
    CHECK(near.Find(kB)->zOrder < near.Find(kC)->zOrder);
    for (int i = 0; i < 10; ++i) {
        CHECK(near.BringToFront(i % 2 == 0 ? kA : kB));
        const std::uint32_t za = near.Find(kA)->zOrder;
        const std::uint32_t zb = near.Find(kB)->zOrder;
        const std::uint32_t zc = near.Find(kC)->zOrder;
        CHECK(za != zb && zb != zc && za != zc);
        CHECK(zc < za || zc < zb);                                   // C nunca volta a ficar por cima
    }
    CHECK(near.Find(kB)->zOrder > near.Find(kC)->zOrder || near.Find(kA)->zOrder > near.Find(kC)->zOrder);
}

void TestFloatTakesTabOutOfItsGroup() {
    current = "TestFloatTakesTabOutOfItsGroup";
    UIDockNode root;
    BuildGroupedTree(root);
    FloatingWindowManager m(kCanvas);
    CHECK(m.Float(root, kB, Rect{{100.0f, 100.0f}, {200.0f, 150.0f}}));
    CHECK(FindLeafContaining(root, kB) == nullptr);                   // saiu da arvore
    UIDockNode* leafC = FindLeafContaining(root, kC);
    CHECK(leafC != nullptr);
    CHECK(leafC->tabs.TabCount() == 0 && leafC->content == kC);       // grupo com uma aba volta a conteudo unico
    CHECK(m.Find(kB)->dockAnchor == kC);
    CHECK(!m.Float(root, kB, Rect{{0.0f, 0.0f}, {200.0f, 150.0f}}));  // ja flutuante
}

void TestDockReturnsToSameGroup() {
    current = "TestDockReturnsToSameGroup";
    UIDockNode root;
    BuildGroupedTree(root);
    FloatingWindowManager m(kCanvas);
    CHECK(m.Float(root, kB, Rect{{100.0f, 100.0f}, {200.0f, 150.0f}}));
    CHECK(m.Dock(root, kB, DockSlot::Center));
    UIDockNode* leaf = FindLeafContaining(root, kB);
    CHECK(leaf != nullptr && leaf == FindLeafContaining(root, kC)); // mesmo grupo de antes
    CHECK(leaf->tabs.TabCount() == 2);
    CHECK(leaf->tabs.GetActiveContent() == kB);
    CHECK(FindLeafContaining(root, kA) != leaf);
    CHECK(m.Find(kB) == nullptr);
    CHECK(!m.Dock(root, kB, DockSlot::Center));                      // nao esta mais flutuando
}

void TestDockWithoutGroupUsesFirstLeaf() {
    current = "TestDockWithoutGroupUsesFirstLeaf";
    UIDockNode root;
    root.content = kA;
    root.Split(DockSlot::Right, 0.5f, kB);
    FloatingWindowManager m(kCanvas);
    CHECK(m.Float(root, kA, Rect{{100.0f, 100.0f}, {200.0f, 150.0f}}));
    CHECK(!root.isSplit);                                            // a arvore colapsou: so B
    CHECK(m.Dock(root, kA, DockSlot::Left));
    CHECK(root.isSplit);
    CHECK(root.firstChild && FindLeafContaining(*root.firstChild, kA) == root.firstChild.get());
    CHECK(FindLeafContaining(root, kB) != nullptr);
}

void TestDockRefusesBadRequests() {
    current = "TestDockRefusesBadRequests";
    UIDockNode root;
    BuildGroupedTree(root);
    FloatingWindowManager m(kCanvas);
    CHECK(!m.Dock(root, kX));                                        // nunca flutuou
    CHECK(m.Float(root, kX, Rect{{0.0f, 0.0f}, {200.0f, 150.0f}}));
    CHECK(!m.Dock(root, kX, DockSlot::None));
    CHECK(!m.Float(root, UIHandle{}, Rect{{0.0f, 0.0f}, {200.0f, 150.0f}}));
    CHECK(!m.Float(root, kW, Rect{{0.0f, 0.0f}, {0.0f, 150.0f}}));   // sem area
    CHECK(!m.Float(root, kW, Rect{{std::nanf(""), 0.0f}, {200.0f, 150.0f}}));
    CHECK(m.Find(kX) != nullptr);
}

void TestFloatEnlargesToMinimumSize() {
    current = "TestFloatEnlargesToMinimumSize";
    FloatingWindowManager m(kCanvas);
    UIDockNode root;
    CHECK(m.Float(root, kW, Rect{{10.0f, 10.0f}, {10.0f, 10.0f}}, Vec2{150.0f, 100.0f}));
    CHECK(m.Find(kW)->rect.size.x == 150.0f && m.Find(kW)->rect.size.y == 100.0f);
}

void TestSnapSuggestions() {
    current = "TestSnapSuggestions";
    FloatingWindowManager m(kCanvas);
    const std::array<DockLeafRect, 2> leaves = {
        DockLeafRect{UIHandle{11, 1}, Rect{{0.0f, 0.0f}, {400.0f, 600.0f}}},
        DockLeafRect{UIHandle{12, 1}, Rect{{400.0f, 0.0f}, {400.0f, 600.0f}}},
    };
    const UIHandle left{11, 1};

    FloatingSnap s = m.SuggestSnap({3.0f, 300.0f}, leaves);          // borda esquerda do canvas
    CHECK(s.canvasEdge && s.slot == DockSlot::Left);
    s = m.SuggestSnap({797.0f, 300.0f}, leaves);
    CHECK(s.canvasEdge && s.slot == DockSlot::Right);
    s = m.SuggestSnap({400.0f, 3.0f}, leaves);
    CHECK(s.canvasEdge && s.slot == DockSlot::Top);
    s = m.SuggestSnap({600.0f, 595.0f}, leaves);
    CHECK(s.canvasEdge && s.slot == DockSlot::Bottom);

    s = m.SuggestSnap({200.0f, 300.0f}, leaves);                     // centro da folha: juntar como aba
    CHECK(!s.canvasEdge && s.slot == DockSlot::Center && s.targetLeaf == left);

    s = m.SuggestSnap({50.0f, 300.0f}, leaves);                      // zona esquerda da folha A
    CHECK(!s.canvasEdge && s.slot == DockSlot::Left && s.targetLeaf == left);

    s = m.SuggestSnap({390.0f, 300.0f}, leaves);                     // zona direita da folha A
    CHECK(!s.canvasEdge && s.slot == DockSlot::Right && s.targetLeaf == left);

    s = m.SuggestSnap({3.0f, 300.0f}, leaves, 0.25f, 0.0f);          // margem zero: sem encaixe de borda
    CHECK(!s.canvasEdge && s.slot == DockSlot::Left && s.targetLeaf == left);

    s = m.SuggestSnap({-5.0f, 300.0f}, leaves);                      // fora do canvas e das folhas
    CHECK(s.slot == DockSlot::None && !s.canvasEdge);
}

} // namespace

int main() {
    TestClampKeepsTitleStripOnCanvas();
    TestSetCanvasReclamps();
    TestResizeRespectsMinimum();
    TestMaximizeRestoreAndMinimize();
    TestBringToFrontKeepsOrderAndUniqueZ();
    TestFloatTakesTabOutOfItsGroup();
    TestDockReturnsToSameGroup();
    TestDockWithoutGroupUsesFirstLeaf();
    TestDockRefusesBadRequests();
    TestFloatEnlargesToMinimumSize();
    TestSnapSuggestions();
    std::printf("floating windows: %d/%d checks passed\n", run - failed, run);
    return failed == 0 ? 0 : 1;
}
