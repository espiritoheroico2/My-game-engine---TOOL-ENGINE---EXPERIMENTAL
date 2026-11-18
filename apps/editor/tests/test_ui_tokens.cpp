// Teste dos tokens de aparencia (ui_tokens.hpp): padroes, limites da forma, troca de cor e volta ao padrao.
// Sem janela; roda no ctest como Editor.UiTokens.
#include "ui_tokens.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>

namespace {

using namespace editor::ui_tokens;

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

// Cada token tem nome de arquivo unico e texto, e o padrao fica dentro de 0 a 1.
void TestColorTableIsCompleteAndInRange() {
  std::set<std::string> keys;
  for (std::size_t i = 0; i < kColorCount; ++i) {
    const ColorInfo& info = infoOf(static_cast<Color>(i));
    CHECK(info.key && *info.key);
    CHECK(info.label && *info.label);
    CHECK(keys.insert(info.key).second);
    CHECK(info.fallback.r >= 0.f && info.fallback.r <= 1.f && info.fallback.g >= 0.f && info.fallback.g <= 1.f);
    CHECK(info.fallback.b >= 0.f && info.fallback.b <= 1.f && info.fallback.a >= 0.f && info.fallback.a <= 1.f);
  }
  CHECK(kColorCount == 14);
  CHECK(infoOf(static_cast<Color>(kColorCount)).key == nullptr);
  CHECK(infoOf(static_cast<Color>(255)).label == nullptr);
}

// Forma padrao: 125 por cento de escala, cantos 4, espaco 4, recuo 14.
void TestShapeDefaults() {
  const Shape s;
  CHECK(s.uiScale == 1.25f);
  CHECK(s.rounding == 4.f);
  CHECK(s.spacing == 4.f);
  CHECK(s.padding == 14.f);
  CHECK(kUiScaleDefault == 1.25f);
}

// Valores fora da faixa sao presos; NaN volta ao padrao.
void TestShapeIsClamped() {
  Shape big;
  big.rounding = 99.f;
  big.spacing = -3.f;
  big.padding = 1000.f;
  big.uiScale = 9.f;
  const Shape a = Clamped(big);
  CHECK(a.rounding == kRoundingMax);
  CHECK(a.spacing == kSpacingMin);
  CHECK(a.padding == kPaddingMax);
  CHECK(a.uiScale == kUiScaleMax);

  Shape nan;
  nan.uiScale = std::nanf("");
  nan.rounding = std::nanf("");
  const Shape b = Clamped(nan);
  CHECK(b.uiScale == kUiScaleDefault);
  CHECK(b.rounding == kRoundingDefault);
}

void TestSetColorChangesAndClamps() {
  Tokens t;
  CHECK(!t.setColor(Color::Accent, t.color(Color::Accent)));   // mesmo valor: nao muda
  CHECK(t.setColor(Color::Accent, Rgba{2.f, -1.f, 0.5f, 1.f}));
  CHECK(t.color(Color::Accent).r == 1.f);
  CHECK(t.color(Color::Accent).g == 0.f);
  CHECK(t.color(Color::Accent).b == 0.5f);
  // Os outros tokens nao mudam.
  CHECK(t.color(Color::Panel) == infoOf(Color::Panel).fallback);
  CHECK(!t.setColor(static_cast<Color>(255), Rgba{1.f, 1.f, 1.f, 1.f}));
  CHECK(t.color(static_cast<Color>(255)) == Rgba{});
}

void TestResetColorsAndShape() {
  Tokens t;
  t.setColor(Color::Text, Rgba{0.f, 0.f, 0.f, 1.f});
  t.setShape(Shape{10.f, 12.f, 20.f, 1.5f});
  CHECK(t.shape().uiScale == 1.5f);
  t.resetAll();
  CHECK(t.color(Color::Text) == infoOf(Color::Text).fallback);
  CHECK(t.shape() == Shape{});
}

void TestSetShapeReportsChange() {
  Tokens t;
  CHECK(!t.setShape(Shape{}));                                // igual ao padrao
  CHECK(t.setShape(Shape{6.f, 4.f, 14.f, 1.25f}));
  CHECK(!t.setShape(Shape{6.f, 4.f, 14.f, 1.25f}));
  CHECK(t.setShape(Shape{6.f, 4.f, 14.f, 99.f}));              // prende em 2.0
  CHECK(t.shape().uiScale == kUiScaleMax);
}

}  // namespace

int main() {
  TestColorTableIsCompleteAndInRange();
  TestShapeDefaults();
  TestShapeIsClamped();
  TestSetColorChangesAndClamps();
  TestResetColorsAndShape();
  TestSetShapeReportsChange();
  if (failed == 0) {
    std::printf("Editor.UiTokens: %d checks, 0 failed\n", run);
    return 0;
  }
  std::printf("Editor.UiTokens: %d checks, %d failed\n", run, failed);
  return 1;
}
