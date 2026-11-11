// Camada L4 (campo de texto): operacoes de edicao, selecao, desfazer e persistencia do texto.
#include "SpriteUi/Widgets/TextEdit.hpp"

#include <cstdio>
#include <string>

namespace {

using ToolEngine::SpriteUi::Widgets::TextEditState;

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

void TestInsertAtCaretAndTyping() {
    current = "TestInsertAtCaretAndTyping";
    TextEditState e;
    e.InsertText("abc");
    CHECK(e.Text() == "abc");
    CHECK(e.Caret() == 3);
    e.MoveLeft();
    e.InsertText("X");
    CHECK(e.Text() == "abXc");
    CHECK(e.Caret() == 3);
}

void TestControlCharactersAreDropped() {
    current = "TestControlCharactersAreDropped";
    TextEditState e;
    e.InsertText("a\nb\tc\x7F");
    CHECK(e.Text() == "abc");
}

void TestBackspaceRemovesWholeCodepoint() {
    current = "TestBackspaceRemovesWholeCodepoint";
    TextEditState e("aé");          // 'é' tem 2 bytes em UTF-8
    CHECK(e.Caret() == 3);
    CHECK(e.Backspace());
    CHECK(e.Text() == "a");
    CHECK(e.Caret() == 1);
    CHECK(!TextEditState("").Backspace());
}

void TestDeleteRemovesForwardCodepoint() {
    current = "TestDeleteRemovesForwardCodepoint";
    TextEditState e("éa");
    e.MoveHome();
    CHECK(e.Delete());
    CHECK(e.Text() == "a");
    CHECK(e.Caret() == 0);
    CHECK(e.Delete());           // apaga o 'a' restante
    CHECK(e.Text().empty());
    CHECK(!e.Delete());          // fim do texto: nada a apagar
}

void TestArrowMovesByCodepoint() {
    current = "TestArrowMovesByCodepoint";
    TextEditState e("aéb");
    CHECK(e.Caret() == 4);
    e.MoveLeft();
    CHECK(e.Caret() == 3);
    e.MoveLeft();
    CHECK(e.Caret() == 1);       // pulou os 2 bytes do 'é'
    e.MoveRight();
    CHECK(e.Caret() == 3);
    e.MoveLeft(); e.MoveLeft(); e.MoveLeft();
    CHECK(e.Caret() == 0);       // nao passa do inicio
}

void TestShiftSelectsAndTypingReplaces() {
    current = "TestShiftSelectsAndTypingReplaces";
    TextEditState e("hello world");
    e.MoveHome();
    for (int i = 0; i < 5; ++i) e.MoveRight(true);
    CHECK(e.HasSelection());
    CHECK(e.SelectedText() == "hello");
    e.InsertText("bye");
    CHECK(e.Text() == "bye world");
    CHECK(!e.HasSelection());
}

void TestSelectAllCopyCutPaste() {
    current = "TestSelectAllCopyCutPaste";
    TextEditState e("abc");
    e.SelectAll();
    CHECK(e.Copy() == "abc");
    CHECK(e.Text() == "abc");                     // copiar nao altera
    CHECK(e.Cut() == "abc");
    CHECK(e.Text().empty());
    e.Paste("xyz");
    CHECK(e.Text() == "xyz");
}

void TestUndoRestoresPreviousState() {
    current = "TestUndoRestoresPreviousState";
    TextEditState e;
    e.InsertText("one");
    e.InsertText(" two");
    CHECK(e.Undo());
    CHECK(e.Text() == "one");
    CHECK(e.Undo());
    CHECK(e.Text().empty());
    CHECK(!e.Undo());
}

void TestHomeAndEndWithSelection() {
    current = "TestHomeAndEndWithSelection";
    TextEditState e("abcd");
    e.MoveHome(true);
    CHECK(e.SelectedText() == "abcd");
    e.MoveEnd();
    CHECK(!e.HasSelection());
    CHECK(e.Caret() == 4);
}

void TestTextIsNeverChangedByReadingOrIdleFrames() {
    current = "TestTextIsNeverChangedByReadingOrIdleFrames";
    // Regressao do bug "campo apagando sozinho": so operacoes explicitas mudam o texto. Mil quadros de leitura
    // (como um redesenho) e nenhuma entrada nao podem alterar texto, caret, ancora nem historico.
    TextEditState e("texto digitado");
    e.MoveLeft(true);
    const std::string text = e.Text();
    const std::size_t caret = e.Caret();
    const std::size_t anchor = e.Anchor();
    volatile std::size_t sink = 0;
    for (int frame = 0; frame < 1000; ++frame) {
        sink = sink + e.Text().size() + e.Caret() + e.Anchor() + (e.HasSelection() ? 1u : 0u) + e.SelectedText().size();
    }
    CHECK(e.Text() == text);
    CHECK(e.Caret() == caret);
    CHECK(e.Anchor() == anchor);
    // O construtor nao grava historico, e leitura nao grava nada: nao ha o que desfazer.
    CHECK(!e.Undo());
    CHECK(e.Text() == text);
}

} // namespace

int main() {
    TestInsertAtCaretAndTyping();
    TestControlCharactersAreDropped();
    TestBackspaceRemovesWholeCodepoint();
    TestDeleteRemovesForwardCodepoint();
    TestArrowMovesByCodepoint();
    TestShiftSelectsAndTypingReplaces();
    TestSelectAllCopyCutPaste();
    TestUndoRestoresPreviousState();
    TestHomeAndEndWithSelection();
    TestTextIsNeverChangedByReadingOrIdleFrames();
    std::printf("text edit: %d/%d checks passed\n", run - failed, run);
    return failed == 0 ? 0 : 1;
}
