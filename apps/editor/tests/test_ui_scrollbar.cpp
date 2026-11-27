// Testes sem janela: barra de rolagem unica (ui_scrollbar.hpp). Roda no ctest como Editor.UiScrollbar.
#include "ui_scrollbar.hpp"

#include <cstdio>

namespace {
int gFailures = 0;  // Quantidade de verificacoes que falharam.
// Imprime ok/FALHOU para a verificacao e conta as falhas.
void Check(bool ok, const char* what) {
  std::printf("%s: %s\n", ok ? "ok" : "FALHOU", what);
  if (!ok) ++gFailures;
}
// Igualdade de floats com tolerancia de 1e-3.
bool Near(float a, float b) { return a > b - 1e-3f && a < b + 1e-3f; }
}  // namespace

// Roda os testes: geometria do polegar, arraste, clique no trilho (pagina), hover e barra oculta quando o conteudo cabe.
int main() {
  namespace sb = editor::ui_scrollbar;
  const sb::Rect track{100.f, 10.f, sb::kWidth, 200.f};

  Check(!sb::needed(100.f, 100.f), "conteudo que cabe: sem barra");
  Check(sb::needed(101.f, 100.f), "conteudo maior: com barra");
  Check(Near(sb::maxScroll(50.f, 100.f), 0.f), "maxScroll nao negativo");
  Check(Near(sb::clampScroll(-5.f, 400.f, 100.f), 0.f) && Near(sb::clampScroll(999.f, 400.f, 100.f), 300.f), "clamp");

  // visivel 100 de 400 -> 25% do trilho (50 px)
  Check(Near(sb::thumbLength(200.f, 400.f, 100.f), 50.f), "polegar proporcional");
  Check(Near(sb::thumbLength(200.f, 100000.f, 100.f), sb::kMinThumb), "polegar minimo");
  Check(Near(sb::thumbLength(200.f, 50.f, 100.f), 200.f), "sem rolagem: polegar = trilho");
  Check(Near(sb::thumbTop(track, 400.f, 100.f, 0.f), 10.f), "polegar no topo");
  Check(Near(sb::thumbTop(track, 400.f, 100.f, 300.f), 160.f), "polegar no fim (10+200-50)");
  Check(Near(sb::thumbTop(track, 400.f, 100.f, 150.f), 85.f), "polegar no meio");

  Check(Near(sb::scrollForThumbTop(track, 400.f, 100.f, 85.f), 150.f), "posicao -> rolagem");
  Check(Near(sb::scrollForThumbTop(track, 400.f, 100.f, -50.f), 0.f), "preso no topo");
  Check(Near(sb::scrollForThumbTop(track, 400.f, 100.f, 999.f), 300.f), "preso no fim");
  Check(Near(sb::dragScroll(track, 400.f, 100.f, 95.f, 10.f), 150.f), "arraste: mouse 95, agarrou a 10 -> topo 85 -> 150");
  Check(sb::dragScroll(track, 400.f, 100.f, 120.f, 10.f) > sb::dragScroll(track, 400.f, 100.f, 100.f, 10.f), "arraste cresce com o mouse");

  Check(Near(sb::pageScroll(track, 400.f, 100.f, 150.f, 20.f), 60.f), "clique acima: pagina para cima");
  Check(Near(sb::pageScroll(track, 400.f, 100.f, 150.f, 205.f), 240.f), "clique abaixo: pagina para baixo");
  Check(Near(sb::pageScroll(track, 400.f, 100.f, 30.f, 20.f), 0.f), "pagina presa em 0");
  Check(Near(sb::pageScroll(track, 400.f, 100.f, 290.f, 205.f), 300.f), "pagina presa no fim");

  sb::Scrollbar bar;
  float scroll = 0.f;
  bar.layout(track, 400.f, 100.f);
  Check(bar.visible(), "barra visivel");
  Check(!bar.pointerDown(50.f, 50.f, scroll), "clique fora do trilho nao consome");
  Check(bar.pointerDown(104.f, 20.f, scroll) && bar.dragging() && Near(scroll, 0.f), "clique no polegar: arrasta sem rolar");
  Check(bar.pointerMove(5.f, 110.f, scroll) && scroll > 0.f, "arraste continua com o mouse fora do trilho (x=5)");
  const float s1 = scroll;
  Check(!bar.pointerMove(5.f, 110.f, scroll) && Near(scroll, s1), "mesma posicao: sem mudanca");
  Check(bar.pointerUp() && !bar.dragging(), "soltar encerra o arraste");
  Check(!bar.pointerMove(5.f, 190.f, scroll) && Near(scroll, s1), "sem arraste, mover nao rola");

  scroll = 0.f;
  bar.layout(track, 400.f, 100.f);
  Check(bar.pointerDown(104.f, 150.f, scroll) && Near(scroll, 90.f), "clique no trilho abaixo: pagina");
  Check(bar.dragging(), "apos a pagina segue arrastando");
  bar.pointerUp();

  bar.layout(track, 400.f, 100.f);
  bar.pointerMove(5.f, 5.f, scroll);
  (void)bar.visualChanged();
  bar.pointerMove(104.f, 50.f, scroll);
  Check(bar.hovered() && bar.visualChanged() && !bar.visualChanged(), "hover muda o visual uma vez");

  const auto pal = editor::palette::active();
  const auto q = bar.quads(pal, 0.f);
  Check(Near(q[0].rect.h, track.h) && q[1].rect.h > 0.f, "quads: trilho e polegar");

  bar.layout(track, 50.f, 100.f);
  Check(!bar.visible() && !bar.pointerDown(104.f, 20.f, scroll), "conteudo cabe: barra some e nao consome");
  Check(bar.quads(pal, 0.f)[1].rect.h == 0.f, "oculta: sem quads");

  std::printf("%s (%d falhas)\n", gFailures ? "FALHOU" : "OK", gFailures);
  return gFailures ? 1 : 0;
}
