// Arrastar e soltar com carga (DragDrop.hpp): limiar de arraste, filtro por tipo, alvo de cima, soltura fora
// de alvo, cancelamento com Escape e entrada nao finita. Sem janela.
#include "SpriteUi/Widgets/DragDrop.hpp"

#include <cstdio>
#include <limits>
#include <vector>

namespace {

using namespace ToolEngine::SpriteUi;
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

constexpr UIHandle kSource{1, 1};
constexpr UIHandle kFolder{2, 1};
constexpr UIHandle kImageSlot{3, 1};
constexpr UIHandle kBehindFolder{4, 1};

// Alvos de teste: a pasta aceita qualquer carga; o slot de imagem so aceita carga do tipo "image".
// O "behind" fica atras da pasta (mesma area) e a pasta, por estar depois, e a de cima.
std::vector<DropTarget> Targets() {
    return {
        DropTarget{kBehindFolder, Rect{{0.0f, 0.0f}, {100.0f, 100.0f}}, ""},
        DropTarget{kFolder, Rect{{0.0f, 0.0f}, {100.0f, 100.0f}}, ""},
        DropTarget{kImageSlot, Rect{{200.0f, 0.0f}, {50.0f, 50.0f}}, "image"},
    };
}

DragPayload Payload(const char* type, const char* data) {
    return DragPayload{type, data};
}

void TestClickWithoutMovementIsNotADrop() {
    DragDropControl drag;
    CHECK(drag.PointerDown(kSource, {10.0f, 10.0f}, Payload("file", "a.txt")));
    CHECK(drag.Phase() == DragPhase::Pending);
    CHECK(!drag.PointerUp({10.0f, 10.0f}, Targets()).has_value());
    CHECK(drag.Phase() == DragPhase::Idle);
}

void TestSmallMoveStaysPendingUntilThreshold() {
    DragDropControl drag;
    drag.threshold = 4.0f;
    CHECK(drag.PointerDown(kSource, {10.0f, 10.0f}, Payload("file", "a.txt")));
    CHECK(!drag.PointerMove({12.0f, 10.0f}, Targets())); // 2 px: abaixo do limiar
    CHECK(drag.Phase() == DragPhase::Pending);
    CHECK(drag.PointerMove({15.0f, 10.0f}, Targets())); // 5 px: passou do limiar
    CHECK(drag.Phase() == DragPhase::Dragging);
    CHECK(drag.Payload().data == "a.txt");
}

void TestDropOnAcceptingTargetReturnsPayload() {
    DragDropControl drag;
    CHECK(drag.PointerDown(kSource, {300.0f, 300.0f}, Payload("file", "a.txt")));
    CHECK(drag.PointerMove({20.0f, 20.0f}, Targets()));
    CHECK(drag.HoveredTarget().has_value());
    CHECK(*drag.HoveredTarget() == kFolder); // o de cima ganha da area sobreposta

    const auto result = drag.PointerUp({20.0f, 20.0f}, Targets());
    CHECK(result.has_value());
    CHECK(result->source == kSource);
    CHECK(result->target == kFolder);
    CHECK(result->payload.type == "file");
    CHECK(result->payload.data == "a.txt");
    CHECK(drag.Phase() == DragPhase::Idle);
}

void TestTargetRejectsOtherPayloadType() {
    DragDropControl drag;
    CHECK(drag.PointerDown(kSource, {300.0f, 300.0f}, Payload("file", "a.txt")));
    CHECK(drag.PointerMove({220.0f, 20.0f}, Targets()));
    CHECK(!drag.HoveredTarget().has_value()); // o slot de imagem nao aceita "file"
    CHECK(!drag.PointerUp({220.0f, 20.0f}, Targets()).has_value());
}

void TestTypedTargetAcceptsMatchingPayload() {
    DragDropControl drag;
    CHECK(drag.PointerDown(kSource, {300.0f, 300.0f}, Payload("image", "logo.png")));
    CHECK(drag.PointerMove({220.0f, 20.0f}, Targets()));
    CHECK(drag.HoveredTarget().has_value());
    CHECK(*drag.HoveredTarget() == kImageSlot);
    const auto result = drag.PointerUp({220.0f, 20.0f}, Targets());
    CHECK(result.has_value() && result->target == kImageSlot);
}

void TestHoverChangesOnlyWhenTargetChanges() {
    DragDropControl drag;
    CHECK(drag.PointerDown(kSource, {300.0f, 300.0f}, Payload("file", "a.txt")));
    CHECK(drag.PointerMove({20.0f, 20.0f}, Targets()));   // entrou no alvo
    CHECK(!drag.PointerMove({30.0f, 30.0f}, Targets()));  // continua no mesmo alvo
    CHECK(drag.PointerMove({500.0f, 500.0f}, Targets())); // saiu de todos
    CHECK(!drag.HoveredTarget().has_value());
    CHECK(!drag.PointerUp({500.0f, 500.0f}, Targets()).has_value()); // soltar fora nao entrega
    CHECK(drag.Phase() == DragPhase::Idle);
}

void TestCancelEndsGestureWithoutDrop() {
    DragDropControl drag;
    CHECK(drag.PointerDown(kSource, {300.0f, 300.0f}, Payload("file", "a.txt")));
    CHECK(drag.PointerMove({20.0f, 20.0f}, Targets()));
    CHECK(drag.Cancel());
    CHECK(drag.Phase() == DragPhase::Idle);
    CHECK(!drag.HoveredTarget().has_value());
    CHECK(!drag.PointerUp({20.0f, 20.0f}, Targets()).has_value());
    CHECK(!drag.Cancel()); // sem gesto, nada a cancelar
}

void TestCancelWorksBeforeThreshold() {
    DragDropControl drag;
    CHECK(drag.PointerDown(kSource, {10.0f, 10.0f}, Payload("file", "a.txt")));
    CHECK(drag.Cancel());
    CHECK(drag.Phase() == DragPhase::Idle);
}

void TestSecondPressWhileBusyIsRefused() {
    DragDropControl drag;
    CHECK(drag.PointerDown(kSource, {10.0f, 10.0f}, Payload("file", "a.txt")));
    CHECK(!drag.PointerDown(kImageSlot, {50.0f, 50.0f}, Payload("image", "x")));
    CHECK(drag.Payload().data == "a.txt");
}

void TestInvalidInputIsIgnored() {
    DragDropControl drag;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(!drag.PointerDown(UIHandle{}, {10.0f, 10.0f}, Payload("file", "a.txt"))); // origem invalida
    CHECK(!drag.PointerDown(kSource, {nan, 10.0f}, Payload("file", "a.txt")));
    CHECK(drag.Phase() == DragPhase::Idle);

    CHECK(drag.PointerDown(kSource, {300.0f, 300.0f}, Payload("file", "a.txt")));
    CHECK(!drag.PointerMove({nan, nan}, Targets()));
    CHECK(drag.Phase() == DragPhase::Pending);
    CHECK(drag.PointerMove({20.0f, 20.0f}, Targets()));
    const auto result = drag.PointerUp({nan, nan}, Targets()); // soltura nao finita usa o ultimo ponto valido
    CHECK(result.has_value() && result->target == kFolder);
}

} // namespace

int main() {
    TestClickWithoutMovementIsNotADrop();
    TestSmallMoveStaysPendingUntilThreshold();
    TestDropOnAcceptingTargetReturnsPayload();
    TestTargetRejectsOtherPayloadType();
    TestTypedTargetAcceptsMatchingPayload();
    TestHoverChangesOnlyWhenTargetChanges();
    TestCancelEndsGestureWithoutDrop();
    TestCancelWorksBeforeThreshold();
    TestSecondPressWhileBusyIsRefused();
    TestInvalidInputIsIgnored();

    std::printf("DragDrop: %d checks, %d failed\n", run, failed);
    return failed == 0 ? 0 : 1;
}
