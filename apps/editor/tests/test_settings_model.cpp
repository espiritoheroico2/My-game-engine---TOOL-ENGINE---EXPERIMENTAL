// Modelo da janela Configuracoes (settings_model.hpp): abas, limites de escolha, tamanho do jogo, sombra e
// divisao de tela. Sem janela. Roda no ctest como Editor.SettingsModel.
#include "settings_model.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

using namespace editor::settings_model;

int run = 0;
int failed = 0;

void Check(bool ok, const char* expr, int line) {
  ++run;
  if (!ok) {
    ++failed;
    std::fprintf(stderr, "[FAIL] line %d: %s\n", line, expr);
  }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)

void TestTabNamesMatchTheBar() {
  CHECK(kTabCount == 8);
  CHECK(std::strcmp(kTabNames[0], "Appearance") == 0);
  CHECK(std::strcmp(kTabNames[2], "Graphics") == 0);
  CHECK(std::strcmp(kTabNames[7], "Physics") == 0);
}

void TestClampChoiceStaysInRange() {
  CHECK(ClampChoice(-3, 5) == 0);
  CHECK(ClampChoice(2, 5) == 2);
  CHECK(ClampChoice(9, 5) == 4);
  CHECK(ClampChoice(1, 0) == 0);
}

void TestDefaultRenderScaleDependsOnApi() {
  CHECK(DefaultRenderScale(true) == 0.5f);
  CHECK(DefaultRenderScale(false) == 1.0f);
}

void TestFixedGameSizeFromModes() {
  int w = -1, h = -1;
  CHECK(!FixedGameSize(0, 800, 600, w, h)); // acompanha a janela: w e h ficam como estavam
  CHECK(w == -1 && h == -1);
  CHECK(FixedGameSize(1, 0, 0, w, h) && w == 640 && h == 360);
  CHECK(FixedGameSize(3, 0, 0, w, h) && w == 1920 && h == 1080);
  CHECK(FixedGameSize(4, 0, 0, w, h) && w == 2560 && h == 1440);
  CHECK(!FixedGameSize(6, 800, 600, w, h));
}

void TestCustomSizeIsClamped() {
  int w = 0, h = 0;
  CHECK(FixedGameSize(kResolutionCustom, 1000, 700, w, h) && w == 1000 && h == 700);
  CHECK(FixedGameSize(kResolutionCustom, 10, 10, w, h) && w == 160 && h == 90);
  CHECK(FixedGameSize(kResolutionCustom, 9000, 9000, w, h) && w == 3840 && h == 2160);
}

void TestShadowQualityRoundTrip() {
  for (int index = 0; index < kShadowQualityCount; ++index) {
    CHECK(ShadowQualityIndex(ShadowResolution(index)) == index);
  }
  CHECK(ShadowResolution(0) == 512u);
  CHECK(ShadowResolution(3) == 4096u);
  CHECK(ShadowResolution(9) == 4096u); // indice fora da faixa cai no degrau de cima
  CHECK(ShadowQualityIndex(300u) == 0);
  CHECK(ShadowQualityIndex(3000u) == 3);
}

void TestSplitIndexRoundTrip() {
  CHECK(SplitIndex(true) == 0);
  CHECK(SplitIndex(false) == 1);
  CHECK(SplitIsVertical(SplitIndex(true)));
  CHECK(!SplitIsVertical(SplitIndex(false)));
}

} // namespace

int main() {
  TestTabNamesMatchTheBar();
  TestClampChoiceStaysInRange();
  TestDefaultRenderScaleDependsOnApi();
  TestFixedGameSizeFromModes();
  TestCustomSizeIsClamped();
  TestShadowQualityRoundTrip();
  TestSplitIndexRoundTrip();

  std::printf("settings model: %d checks, %d failed\n", run, failed);
  return failed == 0 ? 0 : 1;
}
