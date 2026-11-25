// Modelo da barra de status (SpriteUI): textos, tons, limite de 4 refreshes por segundo e troca de idioma.
// Nao usa SDL. Roda no ctest como Editor.StatusBarModel.
#include "statusbar_model.hpp"

#include <cstdio>
#include <string>

namespace {

using editor::statusbar::Info;
using editor::statusbar::StatusBarModel;
using editor::statusbar::Tone;

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

const char* Identity(const char* s) { return s; }

Info SampleInfo() {
  Info info;
  info.api = "Software (CPU)";
  info.frameMs = 20.0;
  info.scene = "Main";
  info.entities = 42;
  info.errors = 0;
  info.warnings = 3;
  info.statusLine = "Saved";
  info.language = "en";
  return info;
}

void TestSegmentsInOrderWithText() {
  current = "TestSegmentsInOrderWithText";
  StatusBarModel model;
  CHECK(model.refresh(0.0, SampleInfo(), Identity));
  const auto& s = model.segments();
  CHECK(s.size() == 6);
  CHECK(s[0].text == "API: Software (CPU)");
  CHECK(s[1].text == "50 FPS");
  CHECK(s[2].text == "scene: Main");
  CHECK(s[3].text == "entities: 42");
  CHECK(s[4].text == "errors: 0  warnings: 3");
  CHECK(s[5].text == "Saved");
  CHECK(model.language() == "en");
}

void TestFpsIsZeroWithoutFrameTime() {
  current = "TestFpsIsZeroWithoutFrameTime";
  Info info = SampleInfo();
  info.frameMs = 0.0;
  StatusBarModel model;
  model.refresh(0.0, info, Identity);
  CHECK(model.segments()[1].text == "0 FPS");
}

void TestErrorsUseErrorTone() {
  current = "TestErrorsUseErrorTone";
  Info info = SampleInfo();
  info.errors = 2;
  StatusBarModel model;
  model.refresh(0.0, info, Identity);
  CHECK(model.segments()[4].tone == Tone::Error);
  info.errors = 0;
  model.refresh(1.0, info, Identity);
  CHECK(model.segments()[4].tone == Tone::Dim);
}

void TestEmptyStatusLineIsOmitted() {
  current = "TestEmptyStatusLineIsOmitted";
  Info info = SampleInfo();
  info.statusLine.clear();
  StatusBarModel model;
  model.refresh(0.0, info, Identity);
  CHECK(model.segments().size() == 5);
}

void TestRefreshAtMostFourTimesPerSecond() {
  current = "TestRefreshAtMostFourTimesPerSecond";
  StatusBarModel model;
  Info info = SampleInfo();
  CHECK(model.refresh(0.0, info, Identity));
  info.frameMs = 10.0;
  CHECK(!model.refresh(0.1, info, Identity));   // dentro do intervalo: nada muda
  CHECK(model.segments()[1].text == "50 FPS");
  CHECK(model.refresh(0.25, info, Identity));   // passou o intervalo: o FPS muda
  CHECK(model.segments()[1].text == "100 FPS");
}

void TestNoChangeReturnsFalse() {
  current = "TestNoChangeReturnsFalse";
  StatusBarModel model;
  const Info info = SampleInfo();
  CHECK(model.refresh(0.0, info, Identity));
  CHECK(!model.refresh(1.0, info, Identity));
}

void TestLanguageChangeAloneRefreshes() {
  current = "TestLanguageChangeAloneRefreshes";
  StatusBarModel model;
  Info info = SampleInfo();
  model.refresh(0.0, info, Identity);
  info.language = "pt";
  CHECK(model.refresh(1.0, info, Identity));
  CHECK(model.language() == "pt");
}

void TestTranslatorIsUsedForLabels() {
  current = "TestTranslatorIsUsedForLabels";
  StatusBarModel model;
  auto translate = [](const char* s) -> const char* { return std::string(s) == "scene: %s" ? "cena: %s" : s; };
  model.refresh(0.0, SampleInfo(), translate);
  CHECK(model.segments()[2].text == "cena: Main");
}

}  // namespace

int main() {
  TestSegmentsInOrderWithText();
  TestFpsIsZeroWithoutFrameTime();
  TestErrorsUseErrorTone();
  TestEmptyStatusLineIsOmitted();
  TestRefreshAtMostFourTimesPerSecond();
  TestNoChangeReturnsFalse();
  TestLanguageChangeAloneRefreshes();
  TestTranslatorIsUsedForLabels();
  std::printf("Editor.StatusBarModel: %d checks, %d failed\n", run, failed);
  return failed == 0 ? 0 : 1;
}
