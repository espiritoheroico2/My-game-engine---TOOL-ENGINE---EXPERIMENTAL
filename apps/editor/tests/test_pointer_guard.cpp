// Testes sem janela: limites de janela solta (window_bounds.hpp) e guarda do botao do mouse (pointer_guard.hpp).
// Roda no ctest como Editor.PointerGuard.
#include "pointer_guard.hpp"
#include "window_bounds.hpp"

#include <cstdio>

namespace {

int gFailures = 0;

void Check(bool ok, const char* what) {
  std::printf("%s: %s\n", ok ? "ok" : "FALHOU", what);
  if (!ok) ++gFailures;
}

bool Near(float a, float b) { return a > b - 0.01f && a < b + 0.01f; }

}  // namespace

int main() {
  namespace wb = editor::window_bounds;
  namespace pg = editor::pointer_guard;
  const wb::Box area{0.f, 0.f, 1240.f, 840.f};

  {
    const auto p = wb::keepInside({400.f, 200.f}, 360.f, area, 28.f, 80.f);
    Check(Near(p.x, 400.f) && Near(p.y, 200.f), "janela dentro da area fica onde esta");
  }
  {
    const auto p = wb::keepInside({1500.f, 300.f}, 360.f, area, 28.f, 80.f);
    Check(Near(p.x, 1160.f), "janela solta pela direita deixa 80 px visiveis (x = area.w - grab)");
  }
  {
    const auto p = wb::keepInside({-900.f, 300.f}, 360.f, area, 28.f, 80.f);
    Check(Near(p.x, -280.f), "janela solta pela esquerda deixa 80 px visiveis (x = grab - w)");
  }
  {
    const auto p = wb::keepInside({300.f, -50.f}, 360.f, area, 28.f, 80.f);
    Check(Near(p.y, 0.f), "cabecalho nao sobe da area (y >= origem)");
  }
  {
    const auto p = wb::keepInside({300.f, 2000.f}, 360.f, area, 28.f, 80.f);
    Check(Near(p.y, 812.f), "cabecalho nao desce alem da base (y <= h - cabecalho)");
  }
  {
    const auto p = wb::keepInside({0.f, 0.f}, 20.f, area, 28.f, 80.f);
    Check(Near(p.x, 0.f), "janela mais estreita que o grab nao exige largura impossivel");
  }

  {
    pg::State s;
    Check(!pg::releaseMissed(s, false, false), "sem botao preso: nada a sintetizar");
    pg::observeLeft(s, true);
    Check(pg::releaseMissed(s, false, false), "preso no editor e solto no SDL: sintetizar soltura");
    Check(!pg::releaseMissed(s, true, false), "botao de fato preso: a guarda nao age");
    Check(!pg::releaseMissed(s, false, true), "roteiro de teste ativo: a guarda nao age");
    Check(!pg::releaseMissed(s, false, false, true), "soltura real ainda na fila: nao sintetiza (evita soltura dupla)");
    pg::observeLeft(s, false);
    Check(!pg::releaseMissed(s, false, false), "soltura ja vista pelo editor: nada a fazer");
  }

  std::printf("%s\n", gFailures == 0 ? "PointerGuard OK" : "PointerGuard FALHOU");
  return gFailures == 0 ? 0 : 1;
}
