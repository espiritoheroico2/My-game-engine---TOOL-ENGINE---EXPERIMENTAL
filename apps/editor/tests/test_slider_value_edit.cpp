// Teste da regra do campo numerico dos deslizantes (slider_value_edit.hpp): texto digitado, arredondamento, limites e
// texto invalido. Regra pura, sem janela.
#include "slider_value_edit.hpp"

#include <cmath>
#include <cstdio>

namespace {

using namespace editor::slider_edit;

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

bool Near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

void TestParseAcceptsDotAndComma() {
  float v = 0.f;
  CHECK(ParseTypedNumber("2.5", v) && Near(v, 2.5f));
  CHECK(ParseTypedNumber("2,5", v) && Near(v, 2.5f));
  CHECK(ParseTypedNumber("  -3 ", v) && Near(v, -3.f));
  CHECK(ParseTypedNumber("+4", v) && Near(v, 4.f));
  CHECK(ParseTypedNumber("0", v) && Near(v, 0.f));
}

void TestParseRejectsGarbage() {
  float v = 7.f;
  CHECK(!ParseTypedNumber("", v));
  CHECK(!ParseTypedNumber("   ", v));
  CHECK(!ParseTypedNumber("abc", v));
  CHECK(!ParseTypedNumber("1.2.3", v));
  CHECK(!ParseTypedNumber("3px", v));
  CHECK(!ParseTypedNumber("nan", v));
  CHECK(!ParseTypedNumber("inf", v));
  CHECK(v == 7.f);   // nada foi escrito na saida
}

void TestCommitClampsToRange() {
  CHECK(Near(CommitSliderText("50", 1.f, 0.f, 10.f, 0.f, false), 10.f));
  CHECK(Near(CommitSliderText("-5", 1.f, 0.f, 10.f, 0.f, false), 0.f));
  CHECK(Near(CommitSliderText("7.25", 1.f, 0.f, 10.f, 0.f, false), 7.25f));
}

void TestCommitRoundsIntegerSliders() {
  CHECK(Near(CommitSliderText("2.4", 0.f, 0.f, 5.f, 1.f, true), 2.f));
  CHECK(Near(CommitSliderText("2.6", 0.f, 0.f, 5.f, 1.f, true), 3.f));
  CHECK(Near(CommitSliderText("9", 0.f, 1.f, 5.f, 1.f, true), 5.f));
}

void TestCommitSnapsToStep() {
  // Faixa 0 a 1 com passo 0.01 (cor): 0.333 vira 0.33.
  CHECK(Near(CommitSliderText("0.333", 0.f, 0.f, 1.f, 0.01f, false), 0.33f));
  // Faixa 1 a 2 com passo 0.25 parte de 1: 1.6 vira 1.5.
  CHECK(Near(CommitSliderText("1.6", 1.f, 1.f, 2.f, 0.25f, false), 1.5f));
  // Passo zero: valor continuo.
  CHECK(Near(CommitSliderText("0.333", 0.f, 0.f, 1.f, 0.f, false), 0.333f));
}

void TestInvalidTextKeepsCurrent() {
  CHECK(Near(CommitSliderText("", 4.f, 0.f, 10.f, 0.f, false), 4.f));
  CHECK(Near(CommitSliderText("oops", 4.f, 0.f, 10.f, 0.f, false), 4.f));
  CHECK(Near(CommitSliderText("x", 4.f, 0.f, 10.f, 1.f, true), 4.f));
}

void TestReversedRangeReturnsLow() {
  CHECK(Near(CommitSliderText("3", 0.f, 4.f, 2.f, 0.f, false), 4.f));
}

} // namespace

int main() {
  TestParseAcceptsDotAndComma();
  TestParseRejectsGarbage();
  TestCommitClampsToRange();
  TestCommitRoundsIntegerSliders();
  TestCommitSnapsToStep();
  TestInvalidTextKeepsCurrent();
  TestReversedRangeReturnsLow();
  std::printf("slider value edit: %d/%d checks passed\n", run - failed, run);
  return failed == 0 ? 0 : 1;
}
