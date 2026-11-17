// Dialogo modal (Modal.hpp): bloqueio do fundo, foco preso, Esc cancela, Enter confirma e retorno do foco. Sem janela.
#include "SpriteUi/Widgets/Modal.hpp"

#include <cstdio>

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

constexpr Rect kDialog{{100.0f, 100.0f}, {200.0f, 100.0f}};
constexpr Vec2 kInside{150.0f, 150.0f};
constexpr Vec2 kBackdrop{20.0f, 20.0f};
constexpr std::uint64_t kOpener = 42;

ModalControl OpenModal(ModalConfig config = {}) {
    ModalControl modal(config);
    modal.Open(kDialog, 3, 1, kOpener); // tres controles, o do meio comeca focado
    return modal;
}

void TestClosedDoesNotBlockOrHandleKeys() {
    ModalControl modal;
    CHECK(modal.PointerDown(kBackdrop) == ModalPointer::Pass);
    CHECK(modal.PointerMove(kBackdrop) == ModalPointer::Pass);
    CHECK(modal.Key(ModalKey::Enter) == ModalEvent::None);
    CHECK(modal.Key(ModalKey::Escape) == ModalEvent::None);
}

void TestBackdropIsBlockedAndDialogIsRouted() {
    ModalControl modal = OpenModal();
    CHECK(modal.PointerDown(kInside) == ModalPointer::Dialog);
    CHECK(modal.PointerDown(kBackdrop) == ModalPointer::Blocked);
    CHECK(modal.PointerMove(kBackdrop) == ModalPointer::Blocked);
    CHECK(modal.IsOpen()); // clique no fundo nao fecha por padrao
}

void TestOutsideClickCanDismissWhenConfigured() {
    ModalControl modal = OpenModal(ModalConfig{true});
    CHECK(modal.PointerMove(kBackdrop) == ModalPointer::Blocked); // movimento nunca fecha
    CHECK(modal.IsOpen());
    CHECK(modal.PointerDown(kBackdrop) == ModalPointer::Cancelled);
    CHECK(!modal.IsOpen());
    CHECK(modal.ReturnFocusTarget() == kOpener);
}

void TestFocusStartsOnDefaultAndCyclesInside() {
    ModalControl modal = OpenModal();
    CHECK(modal.Focus() == 1);
    CHECK(modal.Key(ModalKey::Tab) == ModalEvent::FocusMoved);
    CHECK(modal.Focus() == 2);
    CHECK(modal.Key(ModalKey::Tab) == ModalEvent::FocusMoved);
    CHECK(modal.Focus() == 0); // volta ao inicio do dialogo
    CHECK(modal.Key(ModalKey::BackTab) == ModalEvent::FocusMoved);
    CHECK(modal.Focus() == 2); // Shift+Tab da volta pela ponta
    CHECK(modal.IsOpen());
}

void TestSingleOrNoControlKeepsFocus() {
    ModalControl one;
    one.Open(kDialog, 1, 0, kOpener);
    CHECK(one.Key(ModalKey::Tab) == ModalEvent::None);
    CHECK(one.Focus() == 0);

    ModalControl none;
    none.Open(kDialog, 0, 0, kOpener);
    CHECK(none.Key(ModalKey::Tab) == ModalEvent::None);
    CHECK(none.Key(ModalKey::Enter) == ModalEvent::Confirmed);
    CHECK(none.Activated() == NoControlIndex);
}

void TestEnterConfirmsFocusedControlAndCloses() {
    ModalControl modal = OpenModal();
    CHECK(modal.Key(ModalKey::Tab) == ModalEvent::FocusMoved); // foco no controle 2
    CHECK(modal.Key(ModalKey::Enter) == ModalEvent::Confirmed);
    CHECK(modal.Activated() == 2);
    CHECK(!modal.IsOpen());
    CHECK(modal.ReturnFocusTarget() == kOpener);
}

void TestEscapeCancelsAndCloses() {
    ModalControl modal = OpenModal();
    CHECK(modal.Key(ModalKey::Escape) == ModalEvent::Cancelled);
    CHECK(!modal.IsOpen());
    CHECK(modal.Key(ModalKey::Enter) == ModalEvent::None); // fechado nao confirma de novo
}

void TestDefaultIndexOutOfRangeFallsBackToFirst() {
    ModalControl modal;
    modal.Open(kDialog, 2, 9, kOpener);
    CHECK(modal.Focus() == 0);
}

void TestReopenResetsFocusAndActivation() {
    ModalControl modal = OpenModal();
    CHECK(modal.Key(ModalKey::Enter) == ModalEvent::Confirmed);
    modal.Open(kDialog, 3, 2, 7);
    CHECK(modal.Focus() == 2);
    CHECK(modal.Activated() == NoControlIndex);
    CHECK(modal.ReturnFocusTarget() == 7);
}

} // namespace

int main() {
    TestClosedDoesNotBlockOrHandleKeys();
    TestBackdropIsBlockedAndDialogIsRouted();
    TestOutsideClickCanDismissWhenConfigured();
    TestFocusStartsOnDefaultAndCyclesInside();
    TestSingleOrNoControlKeepsFocus();
    TestEnterConfirmsFocusedControlAndCloses();
    TestEscapeCancelsAndCloses();
    TestDefaultIndexOutOfRangeFallsBackToFirst();
    TestReopenResetsFocusAndActivation();

    std::printf("modal: %d checks, %d failed\n", run, failed);
    return failed == 0 ? 0 : 1;
}
