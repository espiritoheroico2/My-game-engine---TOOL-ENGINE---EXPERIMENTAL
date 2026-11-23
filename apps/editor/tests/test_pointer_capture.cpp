// Teste da captura do mouse dos paineis SpriteUI (pointer_capture.hpp). Regras:
// Up fora do painel ainda deve chegar (so se houve captura), e movimento sem botao nunca vira arrasto.
#include "pointer_capture.hpp"

#include <cstdio>

namespace {

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

using editor::pointer_capture::PointerCapture;

} // namespace

int main() {
  // Clique dentro e soltura dentro: a captura fecha e o Up e entregue.
  {
    PointerCapture c;
    c.down();
    CHECK(c.active());
    CHECK(c.move(true) == false);      // arrastando com o botao: nada a soltar
    CHECK(c.up() == true);             // Up entregue
    CHECK(!c.active());
  }
  // Clique dentro e soltura fora (sem captura depois): o Up ainda tem de ser entregue.
  {
    PointerCapture c;
    c.down();
    CHECK(c.up() == true);
    CHECK(c.up() == false);            // segundo Up sem clique: nada
  }
  // Sem clique, mover o mouse nunca e arrasto nem gera soltura.
  {
    PointerCapture c;
    CHECK(c.move(false) == false);
    CHECK(c.move(true) == false);
    CHECK(!c.active());
  }
  // Clique dentro, depois o botao e solto fora e o movimento chega sem botao: a captura e perdida uma vez so.
  {
    PointerCapture c;
    c.down();
    CHECK(c.move(false) == true);      // perdeu a captura: o chamador solta os controles
    CHECK(c.move(false) == false);     // ja solto: nada mais
    CHECK(c.up() == false);            // o Up que chegar depois nao reabre nada
  }
  // Reset descarta a captura (ex.: janela perdeu o foco).
  {
    PointerCapture c;
    c.down();
    c.reset();
    CHECK(!c.active());
    CHECK(c.up() == false);
  }

  std::printf("%d/%d checks passed\n", run - failed, run);
  return failed == 0 ? 0 : 1;
}
