// Camadas (z-order) e roteamento de entrada: roteiros que reproduzem os problemas relatados.
// Executavel proprio, sem o harness principal. Usa o pipeline real: LayoutTree -> LayoutSceneBridge -> roteador.

#include "SpriteUi/Elements/Button.hpp"
#include "SpriteUi/Elements/Window.hpp"
#include "SpriteUi/Widgets/DockInteraction.hpp"
#include "SpriteUi/Widgets/Widgets.hpp"
#include "SpriteUi/Core/DrawOrder.hpp"
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

void TestLayerPriorityIsIndependentOfZOrder() {
    current = "TestLayerPriorityIsIndependentOfZOrder";
    const DrawOrderKey docked{static_cast<std::int32_t>(UILayer::Docked), 1000.f, 1, 1};
    const DrawOrderKey popup{static_cast<std::int32_t>(UILayer::Popup), -1000.f, 1, 2};
    const DrawOrderKey drag{static_cast<std::int32_t>(UILayer::DragPreview), -2000.f, 1, 3};
    CHECK(DrawsAbove(popup, docked));
    CHECK(DrawsAbove(drag, popup));
    const DrawOrderKey higherZ{1, 2.f, 0, 0};
    const DrawOrderKey lowerZ{1, 1.f, 100, 100};
    const DrawOrderKey laterPaint{1, 1.f, 2, 0};
    const DrawOrderKey earlierPaint{1, 1.f, 1, 100};
    CHECK(DrawsAbove(higherZ, lowerZ));
    CHECK(DrawsAbove(laterPaint, earlierPaint));
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

// Um botao recem-criado recebe o primeiro clique quando os snapshots chegam em quadros distintos.
void TestNewButtonReceivesFirstClick() {
    current = "TestNewButtonReceivesFirstClick";
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
    h.Feed(router.Process(Snap(p, 7, false, false, false), scene.hitTargets));
    h.Feed(router.Process(Snap(p, 8, true, true, false), scene.hitTargets));
    h.Feed(router.Process(Snap(p, 9, false, false, true), scene.hitTargets));
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

// Janela flutuante sobre uma encaixada: a flutuante recebe o clique mesmo com paintOrder menor.
void TestFloatingWindowWinsOverDockedRegardlessOfPaint() {
    current = "TestFloatingWindowWinsOverDockedRegardlessOfPaint";
    HitTarget docked;
    docked.handle = UIHandle{1, 1};
    docked.localSize = {300.0f, 300.0f};
    docked.layer = static_cast<std::int32_t>(UILayer::Docked);
    docked.zOrder = 50.0f;
    docked.paintOrder = 9;
    HitTarget floating = docked;
    floating.handle = UIHandle{2, 1};
    floating.layer = static_cast<std::int32_t>(UILayer::Floating);
    floating.zOrder = 0.0f;
    floating.paintOrder = 1;
    const std::vector<HitTarget> targets{docked, floating};
    UIEventRouter router;
    (void)router.Process(Snap({10.0f, 10.0f}, 1, false, false, false), targets);
    CHECK(router.GetState().hoveredEntity == floating.handle);
}

// Menu aberto: um fundo de captura na camada Popup recebe o clique; a janela de tras nao recebe.
void TestPopupBackdropBlocksWindowBelow() {
    current = "TestPopupBackdropBlocksWindowBelow";
    HitTarget below;
    below.handle = UIHandle{1, 1};
    below.localSize = {300.0f, 300.0f};
    below.layer = static_cast<std::int32_t>(UILayer::Docked);
    below.paintOrder = 1;
    HitTarget backdrop = below;
    backdrop.handle = UIHandle{2, 1};
    backdrop.layer = static_cast<std::int32_t>(UILayer::Popup);
    backdrop.paintOrder = 2;
    const std::vector<HitTarget> targets{below, backdrop};
    UIEventRouter router;
    const auto events = router.Process(Snap({20.0f, 20.0f}, 1, true, true, false), targets);
    bool reachedBelow = false;
    for (const UIEvent& e : events) reachedBelow = reachedBelow || e.target == below.handle;
    CHECK(!reachedBelow);
    CHECK(router.GetState().hoveredEntity == backdrop.handle);
}

// TopmostAt segue a mesma regra de camada que o clique: menu (Popup) cobre a janela de tras.
void TestTopmostAtMatchesClickRule() {
    current = "TestTopmostAtMatchesClickRule";
    HitTarget below;
    below.handle = UIHandle{1, 1};
    below.localSize = {300.0f, 300.0f};
    below.layer = static_cast<std::int32_t>(UILayer::Docked);
    below.paintOrder = 1;
    HitTarget menu = below;
    menu.handle = UIHandle{2, 1};
    menu.layer = static_cast<std::int32_t>(UILayer::Popup);
    menu.localSize = {80.0f, 80.0f};
    menu.paintOrder = 2;
    const std::vector<HitTarget> targets{below, menu};
    CHECK(UIEventRouter::TopmostAt(targets, {20.0f, 20.0f}) == menu.handle);
    CHECK(UIEventRouter::TopmostAt(targets, {200.0f, 200.0f}) == below.handle);
    CHECK(!UIEventRouter::TopmostAt(targets, {400.0f, 400.0f}).IsValid());
}

// Itens de janela levam a camada da janela (flutuante acima de encaixada no desenho).
void TestWindowItemsCarryLayer() {
    current = "TestWindowItemsCarryLayer";
    Elements::Window win;
    win.border = {{0.0f, 0.0f}, {200.0f, 150.0f}};
    const auto items = Elements::BuildWindowItems(win, Elements::WindowStyle{}, 3.0f, 0,
                                                  static_cast<std::int32_t>(UILayer::Floating));
    CHECK(!items.empty());
    bool allFloating = true;
    for (const auto& item : items) allFloating = allFloating && item.layer == static_cast<std::int32_t>(UILayer::Floating);
    CHECK(allFloating);
}

// Arraste de janela encaixada: 10 px nao tiram do dock; 20 px tiram. Janela solta usa o limiar menor.
void TestDockedWindowNeedsUndockThreshold() {
    current = "TestDockedWindowNeedsUndockThreshold";
    const UIHandle w{7, 1};
    Widgets::DockDragController docked;
    docked.BeginPress(w, {100.0f, 100.0f}, true);
    CHECK(!docked.Move({110.0f, 100.0f}));
    CHECK(docked.Move({120.0f, 100.0f}));

    Widgets::DockDragController floating;
    floating.BeginPress(w, {100.0f, 100.0f}, false);
    CHECK(floating.Move({107.0f, 100.0f}));
}

// Grip: so os 14 px a esquerda do titulo.
void TestWindowGripIsOnlyLeftIcon() {
    current = "TestWindowGripIsOnlyLeftIcon";
    const Rect title{{0.0f, 0.0f}, {300.0f, 24.0f}};
    const Rect grip = Widgets::WindowGripRect(title, 14.0f);
    CHECK(grip.Contains({5.0f, 10.0f}));
    CHECK(!grip.Contains({100.0f, 10.0f}));
}

} // namespace

int main() {
    TestLayerPriorityIsIndependentOfZOrder();
    TestNewButtonReceivesFirstClick();
    TestPressAndReleaseInOneSnapshotClicks();
    TestFirstClickOnUnfocusedButtonClicks();
    TestDisabledTargetDoesNotLeakEvents();
    TestFloatingWindowWinsOverDockedRegardlessOfPaint();
    TestPopupBackdropBlocksWindowBelow();
    TestWindowItemsCarryLayer();
    TestTopmostAtMatchesClickRule();
    TestDockedWindowNeedsUndockThreshold();
    TestWindowGripIsOnlyLeftIcon();
    std::printf("layers and input: %d/%d checks passed\n", run - failed, run);
    return failed == 0 ? 0 : 1;
}
