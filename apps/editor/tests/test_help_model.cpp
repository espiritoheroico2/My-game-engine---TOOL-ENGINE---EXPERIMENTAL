// Modelo da janela Help (SpriteUI): blocos do texto, busca por idioma, topico aberto e edicao da busca.
// Nao usa SDL nem ImGui. Roda no ctest como Editor.HelpModel.
#include "help_model.hpp"

#include <cstdio>
#include <string>

namespace {

using editor::help::Block;
using editor::help::BlockKind;
using editor::help::HelpModel;
using editor::help::SearchKey;

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

bool Contains(const std::vector<std::size_t>& indices, std::size_t value) {
  for (std::size_t i : indices) {
    if (i == value) return true;
  }
  return false;
}

void TypeInSearch(HelpModel& model, const std::string& text) {
  model.SetSearchFocus(true);
  model.InsertSearchText(text);
}

void TestParseBodyBlocks() {
  current = "TestParseBodyBlocks";
  const auto blocks = editor::help::ParseBody("# Titulo\nTexto\n- item\n```\nlinha 1\nlinha 2\n```\n");
  CHECK(blocks.size() == 4);
  CHECK(blocks[0].kind == BlockKind::Heading && blocks[0].text == "Titulo");
  CHECK(blocks[1].kind == BlockKind::Paragraph && blocks[1].text == "Texto");
  CHECK(blocks[2].kind == BlockKind::Bullet && blocks[2].text == "item");
  CHECK(blocks[3].kind == BlockKind::Code && blocks[3].text == "linha 1\nlinha 2");
}

void TestParseBodySkipsBlankLinesAndDropsOpenCode() {
  current = "TestParseBodySkipsBlankLinesAndDropsOpenCode";
  CHECK(editor::help::ParseBody("\n\nSo texto\n\n").size() == 1);
  CHECK(editor::help::ParseBody("```\nsem fechamento").empty());
}

void TestVisibleTopicsWithoutSearchShowsAll() {
  current = "TestVisibleTopicsWithoutSearchShowsAll";
  HelpModel model;
  CHECK(model.VisibleTopics().size() == editor::helpTopics().size());
}

void TestSearchMatchesTitleCaseInsensitive() {
  current = "TestSearchMatchesTitleCaseInsensitive";
  HelpModel model;
  TypeInSearch(model, "GETTING STARTED");
  CHECK(Contains(model.VisibleTopics(), 0));   // topico "start" tem o titulo "Getting started"
}

void TestSearchWithoutMatchShowsNothing() {
  current = "TestSearchWithoutMatchShowsNothing";
  HelpModel model;
  TypeInSearch(model, "zz-sem-resultado-zz");
  CHECK(model.VisibleTopics().empty());
}

void TestSearchUsesOnlyActiveLanguage() {
  current = "TestSearchUsesOnlyActiveLanguage";
  HelpModel model;
  model.SetPortuguese(true);
  TypeInSearch(model, "getting started");   // so existe em ingles
  CHECK(model.VisibleTopics().empty());
}

void TestOpenKnownTopicSelectsIt() {
  current = "TestOpenKnownTopicSelectsIt";
  HelpModel model;
  const std::string id = editor::helpTopics()[3].id;
  model.Open(id);
  CHECK(model.SelectedIndex() == 3);
  CHECK(model.Current().id == id);
}

void TestOpenUnknownTopicKeepsCurrent() {
  current = "TestOpenUnknownTopicKeepsCurrent";
  HelpModel model;
  model.SelectTopic(2);
  model.Open("topico-que-nao-existe");
  CHECK(model.SelectedIndex() == 2);
}

void TestCurrentTitleFollowsLanguage() {
  current = "TestCurrentTitleFollowsLanguage";
  HelpModel model;
  CHECK(std::string(model.CurrentTitle()) == "Getting started");
  model.SetPortuguese(true);
  CHECK(std::string(model.CurrentTitle()) == "Primeiros passos");
}

void TestSelectedTopicStaysWhenSearchHidesIt() {
  current = "TestSelectedTopicStaysWhenSearchHidesIt";
  HelpModel model;
  model.SelectTopic(0);
  TypeInSearch(model, "zz-sem-resultado-zz");
  CHECK(model.SelectedIndex() == 0);
  CHECK(!model.CurrentBlocks().empty());
}

void TestSearchEditingKeys() {
  current = "TestSearchEditingKeys";
  HelpModel model;
  TypeInSearch(model, "abc");
  model.PressSearch(SearchKey::Backspace, false);
  CHECK(model.SearchText() == "ab");
  model.PressSearch(SearchKey::Left, false);
  model.PressSearch(SearchKey::Delete, false);
  CHECK(model.SearchText() == "a");
}

void TestSearchIgnoresTextWithoutFocus() {
  current = "TestSearchIgnoresTextWithoutFocus";
  HelpModel model;
  model.InsertSearchText("abc");
  CHECK(model.SearchText().empty());
  model.SetSearchFocus(true);
  model.InsertSearchText("abc");
  model.SetSearchFocus(false);
  model.InsertSearchText("x");
  CHECK(model.SearchText() == "abc");
}

}  // namespace

int main() {
  TestParseBodyBlocks();
  TestParseBodySkipsBlankLinesAndDropsOpenCode();
  TestVisibleTopicsWithoutSearchShowsAll();
  TestSearchMatchesTitleCaseInsensitive();
  TestSearchWithoutMatchShowsNothing();
  TestSearchUsesOnlyActiveLanguage();
  TestOpenKnownTopicSelectsIt();
  TestOpenUnknownTopicKeepsCurrent();
  TestCurrentTitleFollowsLanguage();
  TestSelectedTopicStaysWhenSearchHidesIt();
  TestSearchEditingKeys();
  TestSearchIgnoresTextWithoutFocus();
  std::printf("Editor.HelpModel: %d checks, %d failed\n", run, failed);
  return failed == 0 ? 0 : 1;
}
