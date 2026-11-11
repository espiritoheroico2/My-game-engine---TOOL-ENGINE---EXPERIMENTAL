// Camada L4b (tela de inicio, sem janela): estado dos campos, foco, selecao e acoes do modelo.
// Nao usa SDL nem ImGui. Roda no ctest como EditorStartScreenModel.
#include "start_screen_model.hpp"

#include <cstdio>
#include <filesystem>

namespace {

using editor::start::Field;
using editor::start::Key;
using editor::start::Model;

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

std::string TempRoot() {
  const auto dir = std::filesystem::temp_directory_path() / "tool_start_screen_test";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  return dir.string();
}

void TestStartsWithNameFocused() {
  current = "TestStartsWithNameFocused";
  Model m("", "/tmp/x");
  CHECK(m.hasFocus(Field::Name));
  CHECK(m.field(Field::Name).Text() == "Meu Jogo");
  CHECK(m.field(Field::Name).Caret() == 8);
}

void TestTypingOnlyGoesToFocusedField() {
  current = "TestTypingOnlyGoesToFocusedField";
  Model m("", "/tmp/x");
  m.Focus(Field::Author);
  m.InsertText("Joao");
  CHECK(m.field(Field::Author).Text() == "Joao");
  CHECK(m.field(Field::Name).Text() == "Meu Jogo");   // o nome nao muda
  m.Blur();
  m.InsertText("ignorado");
  CHECK(m.field(Field::Author).Text() == "Joao");     // sem foco, nada entra
}

void TestBackspaceRemovesWholeUtf8Codepoint() {
  current = "TestBackspaceRemovesWholeUtf8Codepoint";
  Model m("", "/tmp/x");
  m.Focus(Field::Author);
  m.InsertText("João");                               // 'ã' e 2 bytes
  m.Press(Key::Backspace, false);
  CHECK(m.field(Field::Author).Text() == "Joã");
  m.Press(Key::Backspace, false);
  CHECK(m.field(Field::Author).Text() == "Jo");
}

void TestSelectAllThenTypeReplaces() {
  current = "TestSelectAllThenTypeReplaces";
  Model m("", "/tmp/x");
  m.SelectAll();
  CHECK(m.field(Field::Name).HasSelection());
  CHECK(m.field(Field::Name).SelectedText() == "Meu Jogo");
  m.InsertText("Novo");
  CHECK(m.field(Field::Name).Text() == "Novo");
}

void TestCopyCutPasteUseClipboardBuffer() {
  current = "TestCopyCutPasteUseClipboardBuffer";
  Model m("", "/tmp/x");
  m.SelectAll();
  m.Copy();
  CHECK(m.clipboardOut() == "Meu Jogo");
  CHECK(m.field(Field::Name).Text() == "Meu Jogo");   // copiar nao altera
  m.Cut();
  CHECK(m.clipboardOut() == "Meu Jogo");
  CHECK(m.field(Field::Name).Text().empty());
  m.Paste("Colado");
  CHECK(m.field(Field::Name).Text() == "Colado");
}

void TestTabCyclesFields() {
  current = "TestTabCyclesFields";
  Model m("", "/tmp/x");
  m.Press(Key::Tab, false);
  CHECK(m.focusField() == Field::Author);
  m.Press(Key::Tab, false);
  CHECK(m.focusField() == Field::Location);
  m.Press(Key::Tab, true);                            // Shift+Tab volta
  CHECK(m.focusField() == Field::Author);
}

void TestCaretFollowsClickOffsetAndShift() {
  current = "TestCaretFollowsClickOffsetAndShift";
  Model m("", "/tmp/x");
  m.SetCaret(4, false);
  CHECK(m.field(Field::Name).Caret() == 4);
  m.SetCaret(0, true);                                // Shift+clique estende
  CHECK(m.field(Field::Name).SelectedText() == "Meu ");
}

void TestCreateWithEmptyLocationUsesDefault() {
  current = "TestCreateWithEmptyLocationUsesDefault";
  const std::string root = TempRoot();
  Model m("", "");
  m.SetText(Field::Location, "");
  CHECK(m.CreateProject(root));
  CHECK(m.done());
  CHECK(std::filesystem::exists(m.result()));
}

void TestCreateFailureKeepsDialogOpen() {
  current = "TestCreateFailureKeepsDialogOpen";
  Model m("", "/caminho/que/nao/existe/xyz");
  m.SetText(Field::Name, "");                         // nome vazio: criar deve falhar com erro
  CHECK(!m.CreateProject("/tmp"));
  CHECK(!m.done());
  CHECK(!m.error().empty());
}

void TestOpenMissingFolderShowsError() {
  current = "TestOpenMissingFolderShowsError";
  Model m("", "/tmp");
  m.SetText(Field::OpenPath, "/pasta/que/nao/existe");
  CHECK(!m.OpenPath());
  CHECK(!m.done());
  CHECK(!m.error().empty());
}

void TestOpenLastWithoutProjectDoesNothing() {
  current = "TestOpenLastWithoutProjectDoesNothing";
  Model m("", "/tmp");
  CHECK(!m.OpenLast());
  CHECK(!m.done());
}

void TestEnterInOpenFieldOpensPath() {
  current = "TestEnterInOpenFieldOpensPath";
  const std::string root = TempRoot();
  Model m("", root);
  m.Focus(Field::OpenPath);
  m.SetText(Field::OpenPath, "/pasta/que/nao/existe");
  m.Press(Key::Enter, false);
  CHECK(!m.done());
  CHECK(!m.error().empty());
}

void TestReadingStateNeverChangesText() {
  current = "TestReadingStateNeverChangesText";
  // Regra R3 (campo nao apaga sozinho): ler o estado mil vezes nao muda texto nem caret.
  Model m("", "/tmp");
  m.Focus(Field::Author);
  m.InsertText("abc");
  const std::string before = m.field(Field::Author).Text();
  volatile std::size_t sink = 0;
  for (int i = 0; i < 1000; ++i) {
    sink = sink + m.field(Field::Author).Text().size() + m.field(Field::Author).Caret() + (m.hasFocus(Field::Author) ? 1u : 0u);
  }
  CHECK(m.field(Field::Author).Text() == before);
  CHECK(m.field(Field::Author).Caret() == 3);
}

} // namespace

int main() {
  TestStartsWithNameFocused();
  TestTypingOnlyGoesToFocusedField();
  TestBackspaceRemovesWholeUtf8Codepoint();
  TestSelectAllThenTypeReplaces();
  TestCopyCutPasteUseClipboardBuffer();
  TestTabCyclesFields();
  TestCaretFollowsClickOffsetAndShift();
  TestCreateWithEmptyLocationUsesDefault();
  TestCreateFailureKeepsDialogOpen();
  TestOpenMissingFolderShowsError();
  TestOpenLastWithoutProjectDoesNothing();
  TestEnterInOpenFieldOpensPath();
  TestReadingStateNeverChangesText();
  std::printf("start screen model: %d/%d checks passed\n", run - failed, run);
  return failed == 0 ? 0 : 1;
}
