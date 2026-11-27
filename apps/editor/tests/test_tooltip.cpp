// Tooltip (tooltip.hpp): espera de 0,5 s com o mouse parado, reinicio ao mexer/trocar de alvo e posicao do balao. Sem janela.
// Roda no ctest como Editor.Tooltip. So a logica de estado e posicao; o desenho usa o Canvas da SpriteUI.
#include "tooltip.hpp"

#include <cstdio>

namespace {
int run = 0, failed = 0;
void Check(bool ok, const char* expr, int line) {
  ++run;
  if (!ok) { ++failed; std::fprintf(stderr, "[FAIL] linha %d: %s\n", line, expr); }
}
#define CHECK(e) Check((e), #e, __LINE__)
}

int main() {
  using editor::tooltip::Tooltip;
  using editor::tooltip::kDelay;

  // Mouse parado sobre o alvo: so aparece depois de kDelay.
  Tooltip t;
  t.beginFrame(50.f, 50.f, 0.f);                  // primeiro quadro fixa a ancora
  t.target(0.f, 0.f, 100.f, 100.f, "ajuda");
  CHECK(!t.visible());
  t.beginFrame(50.f, 50.f, 0.3f); t.target(0.f, 0.f, 100.f, 100.f, "ajuda");
  CHECK(!t.visible());
  t.beginFrame(50.f, 50.f, 0.3f); t.target(0.f, 0.f, 100.f, 100.f, "ajuda");
  CHECK(t.visible() && t.stillSeconds() >= kDelay);

  // Tremor pequeno nao reinicia; movimento grande reinicia.
  t.beginFrame(51.f, 50.f, 0.1f); t.target(0.f, 0.f, 100.f, 100.f, "ajuda");
  CHECK(t.visible());
  t.beginFrame(80.f, 50.f, 0.1f); t.target(0.f, 0.f, 100.f, 100.f, "ajuda");
  CHECK(!t.visible());

  // Fora do retangulo ou suprimido: nao aparece.
  Tooltip u;
  u.beginFrame(10.f, 10.f, 0.f);
  u.beginFrame(10.f, 10.f, 1.f); u.target(200.f, 200.f, 10.f, 10.f, "x");
  CHECK(!u.visible());
  u.beginFrame(10.f, 10.f, 1.f); u.target(0.f, 0.f, 50.f, 50.f, "x"); u.suppress();
  CHECK(!u.visible());
  u.beginFrame(10.f, 10.f, 1.f); u.target(0.f, 0.f, 50.f, 50.f, "");   // texto vazio nao registra
  CHECK(!u.visible());

  // Posicao: ao lado do mouse; vira para a esquerda/cima na borda; nunca sai da tela.
  float x = 0.f, y = 0.f;
  Tooltip::place(100.f, 100.f, 80.f, 30.f, 800.f, 600.f, x, y);
  CHECK(x > 100.f && y > 100.f);
  Tooltip::place(790.f, 590.f, 80.f, 30.f, 800.f, 600.f, x, y);
  CHECK(x + 80.f <= 800.f && y + 30.f <= 600.f && x < 790.f && y < 590.f);
  Tooltip::place(5.f, 5.f, 2000.f, 2000.f, 800.f, 600.f, x, y);   // maior que a tela: fica em 0
  CHECK(x == 0.f && y == 0.f);

  if (failed) { std::fprintf(stderr, "%d de %d verificacoes falharam\n", failed, run); return 1; }
  std::printf("tooltip: %d verificacoes ok\n", run);
  return 0;
}
