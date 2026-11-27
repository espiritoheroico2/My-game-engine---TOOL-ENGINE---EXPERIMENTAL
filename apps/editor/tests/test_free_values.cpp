// Teste das regras de valores digitados livremente e do alfa padrao (free_values.hpp). Regra pura, sem janela.
#include "free_values.hpp"

#include <cmath>
#include <cstdio>
#include <limits>

namespace {

using namespace editor::free_values;

int run = 0;  // Contadores de verificacoes feitas e que falharam.
int failed = 0;

// Registra uma verificacao; falha imprime a linha e a expressao no stderr.
void Check(bool ok, const char* expr, int line) {
  ++run;
  if (!ok) {
    ++failed;
    std::fprintf(stderr, "[FAIL] line %d: %s\n", line, expr);
  }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)

// Igualdade de floats com tolerancia de 1e-4.
bool Near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

// Tamanho digitado: valores comuns passam, extremos sao presos, NaN usa o padrao.
void TestFontClamp() {
  CHECK(Near(ClampFontPx(96.f), 96.f));
  CHECK(Near(ClampFontPx(900.f), 200.f));
  CHECK(Near(ClampFontPx(1.f), 4.f));
  CHECK(Near(ClampFontPx(std::numeric_limits<float>::quiet_NaN(), 14.f), 14.f));
  CHECK(Near(ClampTypedTextScale(2.5f), 2.5f));
  CHECK(Near(ClampTypedTextScale(50.f), 4.f));
  CHECK(Near(ClampTypedTextScale(0.f), 0.5f));
}

// Dados antigos (3 numeros) carregam com alfa 1; com 4 numeros o alfa e mantido.
void TestAlphaDefault() {
  float c[4] = {0.f, 0.f, 0.f, 0.3f};
  const double old3[3] = {0.2, 0.4, 0.6};
  ReadRgba(old3, 3, c);
  CHECK(Near(c[0], 0.2f) && Near(c[1], 0.4f) && Near(c[2], 0.6f) && Near(c[3], 1.f));
  const double now4[4] = {1.0, 0.5, 0.25, 0.4};
  ReadRgba(now4, 4, c);
  CHECK(Near(c[3], 0.4f) && Near(c[0], 1.f));
  float d[4] = {0.1f, 0.2f, 0.3f, 1.f};
  ReadRgba(nullptr, 0, d);
  CHECK(Near(d[0], 0.1f) && Near(d[3], 1.f));
  const double wild[4] = {5.0, -1.0, 0.5, 9.0};
  ReadRgba(wild, 4, d);
  CHECK(Near(d[0], 1.f) && Near(d[1], 0.f) && Near(d[3], 1.f));
}

}  // namespace

// Roda os testes e sai com 1 se alguma verificacao falhou.
int main() {
  TestFontClamp();
  TestAlphaDefault();
  std::printf("free_values: %d checks, %d failed\n", run, failed);
  return failed == 0 ? 0 : 1;
}
