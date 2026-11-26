// Geometria da edicao do Canvas na Scene (canvas_edit.hpp): caixa na area, alca sob o mouse e arrasto em pixels.
// Sem janela. Roda no ctest como Editor.CanvasEdit.
//
// Exemplo: canvas Follow de 100 x 50 em (10, 20). Arrastar o corpo em (30, -10) deixa o deslocamento em (40, 10) e a
// ancora no mesmo lugar. Num canvas Scale com a tela na metade da referencia, 50 px de arrasto valem 100 unidades.
#include "canvas_edit.hpp"

#include <cmath>
#include <cstdio>

namespace {

using namespace editor::canvas_edit;
namespace tc = tool::canvas;

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

bool Near(float a, float b) { return std::fabs(a - b) < 1e-3f; }

tc::Layout Follow(float ax, float ay, float ox, float oy, float w, float h) {
  tc::Layout l;
  l.anchorX = ax; l.anchorY = ay; l.offsetX = ox; l.offsetY = oy; l.width = w; l.height = h;
  l.resize = tc::Resize::Follow;
  return l;
}

const tc::Screen kRef{1920.f, 1080.f};

}  // namespace

int main() {
  // Caixa no modo Follow: o tamanho e o deslocamento valem em pixels, a area nao muda nada.
  {
    const Box b = boxFor(Follow(0.f, 0.f, 10.f, 20.f, 100.f, 50.f), 800.f, 600.f, kRef);
    CHECK(Near(b.x, 10.f) && Near(b.y, 20.f));
    CHECK(Near(b.w, 100.f) && Near(b.h, 50.f));
  }
  // Caixa no modo Scale: a area e metade da referencia, entao tudo cai pela metade.
  {
    tc::Layout l = Follow(0.f, 0.f, 0.f, 0.f, 1920.f, 1080.f);
    l.resize = tc::Resize::Scale;
    const Box b = boxFor(l, 960.f, 540.f, kRef);
    CHECK(Near(b.w, 960.f) && Near(b.h, 540.f));
  }
  // Alca: canto tem prioridade sobre o corpo; fora nao pega nada.
  {
    const Box b{0.f, 0.f, 100.f, 50.f};
    CHECK(gripAt(b, 100.f, 50.f) == Grip::Corner);
    CHECK(gripAt(b, 106.f, 50.f) == Grip::Corner);
    CHECK(gripAt(b, 50.f, 25.f) == Grip::Body);
    CHECK(gripAt(b, 200.f, 200.f) == Grip::None);
    CHECK(gripAt(b, 0.f, 0.f) == Grip::Anchor);
    CHECK(gripAt(b, 4.f, 6.f) == Grip::Anchor);
  }
  // Ancora (canto superior esquerdo) move o canvas, como o corpo; nao redimensiona.
  {
    tc::Layout l = Follow(0.f, 0.f, 0.f, 0.f, 100.f, 50.f);
    drag(l, Grip::Anchor, 12.f, 8.f, Ppu{1.f, 1.f});
    CHECK(Near(l.offsetX, 12.f) && Near(l.offsetY, 8.f));
    CHECK(Near(l.width, 100.f) && Near(l.height, 50.f));
  }
  // Arrasto do corpo no Follow: move o deslocamento, a ancora fica.
  {
    tc::Layout l = Follow(0.5f, 0.5f, 10.f, 20.f, 100.f, 50.f);
    drag(l, Grip::Body, 30.f, -10.f, Ppu{1.f, 1.f});
    CHECK(Near(l.offsetX, 40.f) && Near(l.offsetY, 10.f));
    CHECK(Near(l.anchorX, 0.5f) && Near(l.anchorY, 0.5f));
  }
  // Arrasto do canto no Follow: muda largura e altura pelo mesmo tanto.
  {
    tc::Layout l = Follow(0.f, 0.f, 0.f, 0.f, 100.f, 50.f);
    drag(l, Grip::Corner, 40.f, 15.f, Ppu{1.f, 1.f});
    CHECK(Near(l.width, 140.f) && Near(l.height, 65.f));
  }
  // Tamanho nunca passa de um pixel de projeto, mesmo com arrasto grande para tras.
  {
    tc::Layout l = Follow(0.f, 0.f, 0.f, 0.f, 100.f, 50.f);
    drag(l, Grip::Corner, -1000.f, -1000.f, Ppu{1.f, 1.f});
    CHECK(Near(l.width, 1.f) && Near(l.height, 1.f));
  }
  // Modo Scale: 50 px de tela valem 100 unidades quando a escala e 0,5. Cada eixo usa o seu fator.
  {
    tc::Layout l = Follow(0.f, 0.f, 0.f, 0.f, 1920.f, 1080.f);
    l.resize = tc::Resize::Scale;
    const Box b{0.f, 0.f, 960.f, 270.f};   // 1920 x 1080 vista com escala x 0,5 e y 0,25
    const Ppu p = pixelsPerUnit(b, l);
    CHECK(Near(p.x, 0.5f) && Near(p.y, 0.25f));
    drag(l, Grip::Body, 50.f, 50.f, p);
    CHECK(Near(l.offsetX, 100.f) && Near(l.offsetY, 200.f));
  }
  // Sem escala util (caixa vazia) o arrasto nao mexe no layout.
  {
    tc::Layout l = Follow(0.f, 0.f, 5.f, 5.f, 100.f, 50.f);
    drag(l, Grip::Body, 30.f, 30.f, Ppu{0.f, 0.f});
    CHECK(Near(l.offsetX, 5.f) && Near(l.offsetY, 5.f));
  }
  // Arrasto sem alca nao muda nada.
  {
    tc::Layout l = Follow(0.f, 0.f, 5.f, 5.f, 100.f, 50.f);
    drag(l, Grip::None, 30.f, 30.f, Ppu{1.f, 1.f});
    CHECK(Near(l.offsetX, 5.f) && Near(l.width, 100.f));
  }

  std::printf("%d checks, %d failed\n", run, failed);
  return failed == 0 ? 0 : 1;
}
