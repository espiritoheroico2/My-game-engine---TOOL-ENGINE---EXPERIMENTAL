// Modelo do Inspector (inspector_model.hpp): expansores que guardam o estado, slider preso em min e max, NaN e
// infinito recusados, numero digitado com virgula, cor HEX invalida que mantem a cor atual. Sem janela. Roda no
// ctest como Editor.InspectorModel.
#include "inspector_model.hpp"

#include <cmath>
#include <cstdio>
#include <limits>

namespace {

using namespace editor::inspector_model;

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

bool Nearly(float a, float b) {
  return std::fabs(a - b) < 1e-3f;
}

// Expansor: o estado fica no modelo. Ler nao muda; Toggle inverte so a secao pedida; SetAll vale para todas.
void TestExpandersKeepTheirState() {
  Expanders e;
  CHECK(e.IsOpen(Section::MeshRenderer));
  CHECK(e.IsOpen(Section::Audio));
  CHECK(!e.IsOpen(Section::PostFx));        // pos-processamento comeca fechado
  CHECK(e.IsOpen(Section::MeshRenderer));   // ler de novo nao muda nada

  e.Toggle(Section::MeshRenderer);
  CHECK(!e.IsOpen(Section::MeshRenderer));
  CHECK(e.IsOpen(Section::Light));          // so a secao pedida mudou
  CHECK(!e.IsOpen(Section::MeshRenderer));  // continua fechada no quadro seguinte

  e.Toggle(Section::MeshRenderer);
  CHECK(e.IsOpen(Section::MeshRenderer));

  e.SetAll(false);
  for (std::size_t i = 0; i < kSectionCount; ++i) CHECK(!e.open[i]);
  e.SetAll(true);
  CHECK(e.IsOpen(Section::Camera));
}

// Slider: valor dentro do intervalo passa; abaixo do minimo fica no minimo; acima do maximo fica no maximo.
void TestSliderRespectsMinAndMax() {
  const Range r{0.f, 2.f};
  float out = -1.f;
  CHECK(AcceptInRange(1.25f, r, out) && Nearly(out, 1.25f));
  CHECK(AcceptInRange(-5.f, r, out) && out == 0.f);
  CHECK(AcceptInRange(7.f, r, out) && out == 2.f);
  CHECK(AcceptInRange(0.f, r, out) && out == 0.f);
  CHECK(AcceptInRange(2.f, r, out) && out == 2.f);
  // intervalo com minimo igual ao maximo: o valor e fixo
  const Range fixed{0.5f, 0.5f};
  CHECK(AcceptInRange(9.f, fixed, out) && out == 0.5f);
}

// NaN e infinito nao chegam ao objeto: devolve false e o valor anterior fica.
void TestNonFiniteIsRefused() {
  const Range r{0.f, 1.f};
  float out = 0.3f;
  CHECK(!AcceptInRange(std::numeric_limits<float>::quiet_NaN(), r, out));
  CHECK(out == 0.3f);
  CHECK(!AcceptInRange(std::numeric_limits<float>::infinity(), r, out));
  CHECK(out == 0.3f);
  CHECK(!AcceptInRange(-std::numeric_limits<float>::infinity(), r, out));
  CHECK(out == 0.3f);

  float num = 4.f;
  CHECK(!ParseNumber("nan", num));
  CHECK(!ParseNumber("NaN", num));
  CHECK(!ParseNumber("inf", num));
  CHECK(!ParseNumber("-infinity", num));
  CHECK(num == 4.f);
}

// Numero digitado: ponto ou virgula, espacos nas pontas; texto ruim nao muda o valor.
void TestParseNumber() {
  float v = 0.f;
  CHECK(ParseNumber("2,5", v) && Nearly(v, 2.5f));
  CHECK(ParseNumber(" 3.25 ", v) && Nearly(v, 3.25f));
  CHECK(ParseNumber("-7", v) && Nearly(v, -7.f));
  v = 9.f;
  CHECK(!ParseNumber("", v) && v == 9.f);
  CHECK(!ParseNumber("12abc", v) && v == 9.f);
  CHECK(!ParseNumber("0x10", v) && v == 9.f);
  CHECK(!ParseNumber("1,2,3", v) && v == 9.f);

  float clamped = 0.f;
  CHECK(ParseNumberInRange("500", Range{0.f, 200.f}, clamped) && clamped == 200.f);
  CHECK(ParseNumberInRange("-1", Range{0.f, 200.f}, clamped) && clamped == 0.f);
}

// HEX valido: escreve rgb. HEX invalido: rgb fica como estava (a cor atual nao muda).
void TestInvalidHexKeepsCurrentColor() {
  float rgb[3] = {0.1f, 0.2f, 0.3f};
  CHECK(ApplyHexColor("#FF8000", rgb));
  CHECK(Nearly(rgb[0], 1.f) && Nearly(rgb[1], 128.f / 255.f) && Nearly(rgb[2], 0.f));

  const float before[3] = {0.25f, 0.5f, 0.75f};
  float cur[3] = {before[0], before[1], before[2]};
  CHECK(!ApplyHexColor("#GG0000", cur));
  CHECK(cur[0] == before[0] && cur[1] == before[1] && cur[2] == before[2]);
  CHECK(!ApplyHexColor("#FFF", cur));
  CHECK(!ApplyHexColor("", cur));
  CHECK(!ApplyHexColor("#12345678", cur));
  CHECK(!ApplyHexColor("ola!!", cur));
  CHECK(cur[0] == before[0] && cur[1] == before[1] && cur[2] == before[2]);

  CHECK(ApplyHexColor("  abcdef ", cur));   // sem # e com espacos, minusculas
  CHECK(Nearly(cur[0], 0xAB / 255.f));
}

void TestHexTextRoundTrip() {
  const float rgb[3] = {1.f, 0.f, 0.5f};
  const std::string text = HexText(rgb);
  CHECK(text == "#FF0080");
  float back[3] = {0.f, 0.f, 0.f};
  CHECK(ApplyHexColor(text, back));
  CHECK(Nearly(back[0], 1.f) && Nearly(back[2], 128.f / 255.f));
  const float out[3] = {2.f, -1.f, 0.f};   // fora da faixa: prende em 0 a 1
  CHECK(HexText(out) == "#FF0000");
}

void TestFormatNumber() {
  CHECK(FormatNumber(1.5f, 2) == "1.50");
  CHECK(FormatNumber(-3.f, 0) == "-3");
  CHECK(FormatNumber(0.125f, 3) == "0.125");
}

void TestLockedObjectIsReadOnly() {
  CHECK(EditAllowed(false));
  CHECK(!EditAllowed(true));
}

} // namespace

int main() {
  TestExpandersKeepTheirState();
  TestSliderRespectsMinAndMax();
  TestNonFiniteIsRefused();
  TestParseNumber();
  TestInvalidHexKeepsCurrentColor();
  TestHexTextRoundTrip();
  TestFormatNumber();
  TestLockedObjectIsReadOnly();
  std::printf("Editor.InspectorModel: %d checks, %d failed\n", run, failed);
  return failed == 0 ? 0 : 1;
}
