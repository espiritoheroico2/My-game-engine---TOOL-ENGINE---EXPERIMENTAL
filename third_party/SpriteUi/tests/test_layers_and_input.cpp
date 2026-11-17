// Camadas (z-order) e roteamento de entrada: roteiros que reproduzem os problemas relatados.
// Executavel proprio, sem o harness principal. Usa o pipeline real: LayoutTree -> LayoutSceneBridge -> roteador.

#include "SpriteUi/Elements/Button.hpp"
#include "SpriteUi/Input/InputRouter.hpp"
#include "SpriteUi/Layout/LayoutBridge.hpp"
#include "SpriteUi/Layout/LayoutTree.hpp"
#include "SpriteUi/Rendering/DrawList.hpp"

#include <cstdio>
#include <vector>

namespace {

using namespace ToolEngine::SpriteUi;
using namespace ToolEngine::SpriteUi::Input;

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

FlexItem Fixed(float w, float h) {
    FlexItem style;
    style.width = {SizeMode::Fixed, w};
    style.height = {SizeMode::Fixed, h};
    return style;
}

// Pilha de duas janelas que se sobrepoem na regiao (100..200, 100..200). A e B sao irmaos na coluna da raiz.
struct Overlap {
    LayoutTree tree;
    LayoutSceneBridge bridge;
    UIHandle root{};
    UIHandle winA{};
    UIHandle winB{};
    UIHandle childA{}; // botao dentro de A, com zOrder proprio (como um host faria)
    UIHandle popup{};  // irmao de B, na camada Popup

    Overlap() {
        FlexItem column = Fixed(400.0f, 400.0f);
        column.direction = LayoutDirection::Column;
        root = tree.Create({}, column);

        winA = tree.Create(root, Fixed(200.0f, 200.0f));
        FlexItem bStyle = Fixed(200.0f, 200.0f);
        bStyle.margin = {100.0f, -100.0f, 0.0f, 0.0f};
        winB = tree.Create(root, bStyle);
        // Botao de A dentro da regiao de sobreposicao (150..210): aparece por cima de B.
        FlexItem childStyle = Fixed(60.0f, 60.0f);
        childStyle.margin = {150.0f, 150.0f, 0.0f, 0.0f};
        childA = tree.Create(winA, childStyle);
        popup = tree.Create(root, Fixed(40.0f, 40.0f));

        ElementVisual window;
        window.clipOverflow = true;
        bridge.SetVisual(winA, window);
        bridge.SetVisual(winB, window);

        ElementVisual child;
        child.focusable = true;
        child.zOrder = 1.0f; // Filho com zOrder proprio: antes da correcao, ele vencia no desenho e perdia no acerto.
        bridge.SetVisual(childA, child);

        ElementVisual pop;
        pop.layer = static_cast<std::int32_t>(UILayer::Popup);
        bridge.SetVisual(popup, pop);
        tree.Layout({{0.0f, 0.0f}, {400.0f, 400.0f}});
    }
};

// Dono do item de desenho mais alto que cobre o ponto. Usa a mesma ordem do RenderExtractor (DrawsAbove).
UIHandle TopDrawnOwner(const SceneExtractionResult& scene, Vec2 point) {
    UIHandle top{};
    DrawOrderKey topKey{};
    bool found = false;
    for (const Rendering::UIRenderItem& item : scene.renderItems) {
        const Rect box{item.layout.resolvedPosition, item.layout.resolvedSize};
        if (!box.Contains(point)) continue;
        const DrawOrderKey key{item.layer, item.zOrder, item.paintOrder, item.handle.Pack()};
        if (!found || DrawsAbove(key, topKey)) {
            topKey = key;
            top = item.handle;
            found = true;
        }
    }
    return top;
}

UIHandle TopHitOwner(const SceneExtractionResult& scene, Vec2 point) {
    UIEventRouter router;
    UIInputSnapshot input;
    input.pointerPositionLogical = point;
    input.frameId = 1;
    input.windowFocused = true;
    (void)router.Process(input, scene.hitTargets);
    return router.GetState().hoveredEntity;
}

void TestDrawAndHitAgreeOnOverlap() {
    current = "TestDrawAndHitAgreeOnOverlap";
    Overlap s;
    const SceneExtractionResult scene = s.bridge.Extract(s.tree);
    const Vec2 inOverlap{110.0f, 110.0f}; // dentro de A e de B, fora do botao de A
    CHECK(TopDrawnOwner(scene, inOverlap) == TopHitOwner(scene, inOverlap));
    CHECK(TopHitOwner(scene, inOverlap) == s.winB); // B foi criada depois: esta por cima

    // Botao de A na sobreposicao: tem zOrder proprio, entao fica por cima de B tanto no desenho quanto no acerto.
    const Vec2 onChild{170.0f, 170.0f};
    CHECK(TopDrawnOwner(scene, onChild) == s.childA);
    CHECK(TopHitOwner(scene, onChild) == s.childA);
}

void TestClickingBottomWindowRaisesItInDrawAndHit() {
    current = "TestClickingBottomWindowRaisesItInDrawAndHit";
    Overlap s;
    const Rect bBefore = s.tree.Get(s.winB)->node.rect;
    CHECK(s.tree.BringToFront(s.winA));
    s.tree.Layout({{0.0f, 0.0f}, {400.0f, 400.0f}});
    // Subir nao mexe no layout: a janela B continua no mesmo lugar.
    const Rect bAfter = s.tree.Get(s.winB)->node.rect;
    CHECK(bBefore.position.x == bAfter.position.x && bBefore.position.y == bAfter.position.y);
    const SceneExtractionResult scene = s.bridge.Extract(s.tree);
    const Vec2 inOverlap{110.0f, 110.0f};
    CHECK(TopDrawnOwner(scene, inOverlap) == s.winA);
    CHECK(TopHitOwner(scene, inOverlap) == s.winA);
}

void TestPopupLayerStaysAboveWindowsInDrawAndHit() {
    current = "TestPopupLayerStaysAboveWindowsInDrawAndHit";
    Overlap s;
    // Popup em (0..40) do eixo: nao e o mesmo ponto que a sobreposicao. Usa a coluna de A na faixa do popup.
    const SceneExtractionResult scene = s.bridge.Extract(s.tree);
    const auto popupOrder = scene.OrderOf(s.popup);
    CHECK(popupOrder.has_value());
    if (popupOrder) CHECK(popupOrder->layer == static_cast<std::int32_t>(UILayer::Popup));
    // Ponto no popup com janela A por baixo: popup vence no desenho e no acerto.
    const auto rect = s.tree.Get(s.popup)->node.rect;
    const Vec2 onPopup{rect.position.x + 5.0f, rect.position.y + 5.0f};
    CHECK(TopDrawnOwner(scene, onPopup) == s.popup);
    CHECK(TopHitOwner(scene, onPopup) == s.popup);
}

void TestDragRootIsAboveEverythingAndNotMovedByLayering() {
    current = "TestDragRootIsAboveEverythingAndNotMovedByLayering";
    Overlap s;
    s.bridge.SetDragRoot(s.winA);
    const SceneExtractionResult scene = s.bridge.Extract(s.tree);
    const Vec2 inOverlap{170.0f, 170.0f};
    // A esta sendo arrastada: passa por cima de B mesmo sendo criada antes, inclusive o botao dela.
    CHECK(TopDrawnOwner(scene, inOverlap) == s.childA);
    CHECK(TopHitOwner(scene, inOverlap) == s.childA);
    const auto orderA = scene.OrderOf(s.winA);
    CHECK(orderA.has_value() && orderA->layer == static_cast<std::int32_t>(UILayer::DragPreview));
    // Subarvore inteira vai junto: o filho de A tambem esta na camada de arrasto.
    const auto orderChild = scene.OrderOf(s.childA);
    CHECK(orderChild.has_value() && orderChild->layer == static_cast<std::int32_t>(UILayer::DragPreview));

    s.bridge.ClearDragRoot();
    const SceneExtractionResult after = s.bridge.Extract(s.tree);
    CHECK(TopHitOwner(after, inOverlap) == s.childA); // sem arrasto, o botao de A segue a ordem normal
    CHECK(TopHitOwner(after, Vec2{110.0f, 110.0f}) == s.winB);
}

void TestSameHandleOrderFromDrawAndHitForOwnerOfText() {
    current = "TestSameHandleOrderFromDrawAndHitForOwnerOfText";
    Overlap s;
    const SceneExtractionResult scene = s.bridge.Extract(s.tree);
    const auto order = scene.OrderOf(s.childA);
    CHECK(order.has_value());
    if (order) {
        // zOrder efetivo do filho = zOrder do pai (0) + o proprio (1).
        CHECK(order->zOrder == 1.0f);
    }
}

// ---------------------------------------------------------------- entrada

// Traduz o evento do roteador para o botao (como o host faz) e conta cliques.
struct ButtonHarness {
    Elements::Button button;
    UIHandle handle{};
    int clicks = 0;

    void Feed(const std::vector<UIEvent>& events) {
        for (const UIEvent& e : events) {
            if (e.target != handle) continue;
            Elements::PointerEvent pe;
            pe.position = e.screenPosition;
            pe.pointerId = e.pointerId;
            pe.inside = e.inside;
            switch (e.type) {
            case UIEventType::PointerEnter: pe.phase = Elements::PointerPhase::Enter; break;
            case UIEventType::PointerLeave: pe.phase = Elements::PointerPhase::Leave; break;
            case UIEventType::PointerDown: pe.phase = Elements::PointerPhase::Down; break;
            case UIEventType::PointerMove: pe.phase = Elements::PointerPhase::Move; break;
            case UIEventType::PointerUp: pe.phase = Elements::PointerPhase::Up; break;
            case UIEventType::Cancel: pe.phase = Elements::PointerPhase::Cancel; break;
            case UIEventType::Click: ++clicks; continue;
            default: continue;
            }
            (void)button.OnPointer(pe);
        }
    }
};

UIInputSnapshot Snap(Vec2 p, std::uint64_t frame, bool down, bool pressed, bool released) {
    UIInputSnapshot in;
    in.pointerPositionLogical = p;
    in.frameId = frame;
    in.windowFocused = true;
    in.primaryDown = down;
    in.primaryPressed = pressed;
    in.primaryReleased = released;
    return in;
}

// Reproducao do relato: botao recem-criado, e move, down, up chegam no mesmo quadro. Espera-se um clique.
void TestMoveDownUpOnNewButtonInSameFrameClicks() {
    current = "TestMoveDownUpOnNewButtonInSameFrameClicks";
    LayoutTree tree;
    LayoutSceneBridge bridge;
    const UIHandle root = tree.Create({}, Fixed(300.0f, 300.0f));
    const UIHandle btn = tree.Create(root, Fixed(120.0f, 28.0f));
    ElementVisual v;
    v.focusable = true;
    bridge.SetVisual(btn, v);
    tree.Layout({{0.0f, 0.0f}, {300.0f, 300.0f}});
    const SceneExtractionResult scene = bridge.Extract(tree);

    ButtonHarness h;
    h.handle = btn;
    UIEventRouter router;
    const Vec2 p{30.0f, 10.0f};
    const std::uint64_t frame = 7; // o mesmo quadro para as tres entradas
    h.Feed(router.Process(Snap(p, frame, false, false, false), scene.hitTargets));
    h.Feed(router.Process(Snap(p, frame, true, true, false), scene.hitTargets));
    h.Feed(router.Process(Snap(p, frame, false, false, true), scene.hitTargets));
    CHECK(h.clicks == 1);
    CHECK(!h.button.states.pressed);
}

// Um unico snapshot com press e release no mesmo quadro tambem conta como clique.
void TestPressAndReleaseInOneSnapshotClicks() {
    current = "TestPressAndReleaseInOneSnapshotClicks";
    LayoutTree tree;
    LayoutSceneBridge bridge;
    const UIHandle root = tree.Create({}, Fixed(300.0f, 300.0f));
    const UIHandle btn = tree.Create(root, Fixed(120.0f, 28.0f));
    ElementVisual v;
    v.focusable = true;
    bridge.SetVisual(btn, v);
    tree.Layout({{0.0f, 0.0f}, {300.0f, 300.0f}});
    const SceneExtractionResult scene = bridge.Extract(tree);

    ButtonHarness h;
    h.handle = btn;
    UIEventRouter router;
    h.Feed(router.Process(Snap({30.0f, 10.0f}, 1, true, true, true), scene.hitTargets));
    CHECK(h.clicks == 1);
}

// Clique no primeiro toque, sem foco previo: foco nao pode engolir o clique.
void TestFirstClickOnUnfocusedButtonClicks() {
    current = "TestFirstClickOnUnfocusedButtonClicks";
    LayoutTree tree;
    LayoutSceneBridge bridge;
    const UIHandle root = tree.Create({}, Fixed(300.0f, 300.0f));
    const UIHandle btn = tree.Create(root, Fixed(120.0f, 28.0f));
    ElementVisual v;
    v.focusable = true;
    bridge.SetVisual(btn, v);
    tree.Layout({{0.0f, 0.0f}, {300.0f, 300.0f}});
    const SceneExtractionResult scene = bridge.Extract(tree);

    ButtonHarness h;
    h.handle = btn;
    UIEventRouter router;
    h.Feed(router.Process(Snap({30.0f, 10.0f}, 1, false, false, false), scene.hitTargets));
    h.Feed(router.Process(Snap({30.0f, 10.0f}, 2, true, true, false), scene.hitTargets));
    h.Feed(router.Process(Snap({30.0f, 10.0f}, 3, false, false, true), scene.hitTargets));
    CHECK(h.clicks == 1);
    CHECK(router.GetState().focusedEntity == btn);
}

// Botao desativado no pipeline real: nao clica, e cobre o botao de baixo (que tambem nao recebe o clique).
void TestDisabledButtonFromBridgeDoesNotClickOrLeak() {
    current = "TestDisabledButtonFromBridgeDoesNotClickOrLeak";
    LayoutTree tree;
    LayoutSceneBridge bridge;
    const UIHandle root = tree.Create({}, Fixed(300.0f, 300.0f));
    const UIHandle under = tree.Create(root, Fixed(120.0f, 28.0f));
    FlexItem overStyle = Fixed(120.0f, 28.0f);
    overStyle.margin = {-120.0f, 0.0f, 0.0f, 0.0f}; // Row: volta para cima do botao de baixo
    const UIHandle over = tree.Create(root, overStyle);
    ElementVisual u;
    u.focusable = true;
    bridge.SetVisual(under, u);
    ElementVisual o = u;
    o.enabled = false;
    bridge.SetVisual(over, o);
    tree.Layout({{0.0f, 0.0f}, {300.0f, 300.0f}});
    const SceneExtractionResult scene = bridge.Extract(tree);

    ButtonHarness hUnder;
    hUnder.handle = under;
    UIEventRouter router;
    hUnder.Feed(router.Process(Snap({30.0f, 10.0f}, 1, true, true, false), scene.hitTargets));
    hUnder.Feed(router.Process(Snap({30.0f, 10.0f}, 2, false, false, true), scene.hitTargets));
    CHECK(hUnder.clicks == 0);
    CHECK(!router.GetState().focusedEntity.IsValid());
    CHECK(!router.GetState().hoveredEntity.IsValid());
}

// Alvo desativado cobrindo o ponteiro: nao recebe evento e nao deixa o alvo de baixo receber.
void TestDisabledTargetDoesNotLeakEvents() {
    current = "TestDisabledTargetDoesNotLeakEvents";
    HitTarget below;
    below.handle = UIHandle{1, 1};
    below.localToScreen = {};
    below.localSize = {100.0f, 100.0f};
    below.paintOrder = 1;
    HitTarget top = below;
    top.handle = UIHandle{2, 1};
    top.paintOrder = 2;
    top.enabled = false;
    const std::vector<HitTarget> targets{below, top};

    UIEventRouter router;
    const auto events = router.Process(Snap({50.0f, 50.0f}, 1, true, true, false), targets);
    bool leaked = false;
    for (const UIEvent& e : events) leaked = leaked || e.target == below.handle;
    CHECK(!leaked);
    CHECK(!router.GetState().hoveredEntity.IsValid());
    CHECK(router.GetState().consumedThisFrame);
}

} // namespace

int main() {
    TestDrawAndHitAgreeOnOverlap();
    TestClickingBottomWindowRaisesItInDrawAndHit();
    TestPopupLayerStaysAboveWindowsInDrawAndHit();
    TestDragRootIsAboveEverythingAndNotMovedByLayering();
    TestSameHandleOrderFromDrawAndHitForOwnerOfText();
    TestMoveDownUpOnNewButtonInSameFrameClicks();
    TestPressAndReleaseInOneSnapshotClicks();
    TestFirstClickOnUnfocusedButtonClicks();
    TestDisabledTargetDoesNotLeakEvents();
    TestDisabledButtonFromBridgeDoesNotClickOrLeak();
    std::printf("layers and input: %d/%d checks passed\n", run - failed, run);
    return failed == 0 ? 0 : 1;
}
