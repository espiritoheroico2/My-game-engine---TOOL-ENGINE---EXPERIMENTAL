// Widget numerico de arraste (drag_number.hpp): sobe/desce, limites, passo fino/grosso e clique sem arraste.
// Sem janela. Roda no ctest como Editor.DragNumber.
#include "drag_number.hpp"

#include <cstdio>

namespace {

using namespace editor::drag_number;

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

void TestRightIncreasesLeftDecreases() {
  DragNumber d;
  float out = 0.f;
  d.Press(1u, 100.f, 5.f);
  CHECK(d.Move(150.f, false, false, 2, -100.f, 100.f, out));
  CHECK(out > 5.f);
  CHECK(d.Dragging());
  const float high = out;
  CHECK(d.Move(100.f, false, false, 2, -100.f, 100.f, out));
  CHECK(out < high);
  CHECK(!d.Release());   // arrastou: nao vira edicao de texto
}

void TestLimits() {
  DragNumber d;
  float out = 0.f;
  d.Press(1u, 0.f, 1.f);
  CHECK(d.Move(100000.f, false, false, 1, 0.f, 3.f, out));
  CHECK(out == 3.f);
  CHECK(d.Move(-100000.f, false, false, 1, 0.f, 3.f, out));
  CHECK(out == 0.f);
}

void TestClickWithoutDragIsText() {
  DragNumber d;
  float out = 9.f;
  d.Press(2u, 40.f, 1.f);
  CHECK(!d.Move(41.f, false, false, 2, 0.f, 10.f, out));   // abaixo do limiar
  CHECK(out == 9.f);
  CHECK(d.Release());   // clique comum: o campo abre como texto
  CHECK(!d.Pending());
}

void TestStepScalesWithDecimalsAndModifiers() {
  CHECK(StepFor(0) == 1.f);
  CHECK(StepFor(2) < StepFor(1));
  CHECK(StepMultiplier(true, false) < StepMultiplier(false, false));
  CHECK(StepMultiplier(false, true) > StepMultiplier(false, false));
  float fine = 0.f, normal = 0.f, coarse = 0.f;
  DragNumber d;
  d.Press(1u, 0.f, 0.f); CHECK(d.Move(40.f, true, false, 2, -1e9f, 1e9f, fine)); d.Release();
  d.Press(1u, 0.f, 0.f); CHECK(d.Move(40.f, false, false, 2, -1e9f, 1e9f, normal)); d.Release();
  d.Press(1u, 0.f, 0.f); CHECK(d.Move(40.f, false, true, 2, -1e9f, 1e9f, coarse)); d.Release();
  CHECK(fine < normal && normal < coarse);
  // Campo inteiro (0 casas): cada pixel vale 1.
  float whole = 0.f;
  d.Press(1u, 0.f, 10.f); CHECK(d.Move(20.f, false, false, 0, 0.f, 1000.f, whole)); d.Release();
  CHECK(whole == 30.f);
}

}  // namespace

int main() {
  TestRightIncreasesLeftDecreases();
  TestLimits();
  TestClickWithoutDragIsText();
  TestStepScalesWithDecimalsAndModifiers();
  std::printf("drag number: %d checks, %d failed\n", run, failed);
  return failed == 0 ? 0 : 1;
}
