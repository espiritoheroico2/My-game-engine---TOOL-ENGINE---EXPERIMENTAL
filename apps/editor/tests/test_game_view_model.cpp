// Modelo da janela Jogo (game_view_model.hpp): letterbox (proporcao mantida, barras nas laterais), divisao entre duas
// cameras e recorte do modo preencher. Sem janela. Roda no ctest como Editor.GameViewModel. O letterbox e comparado
// com o Contain da biblioteca SpriteUI (ImageDrawRect): as duas contas precisam bater.
#include "game_view_model.hpp"

#include "SpriteUi/Elements/Image.hpp"

#include <cmath>
#include <cstdio>

namespace {

using namespace editor::game_view_model;
namespace SU = ToolEngine::SpriteUi;

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

bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }

// 16:9 numa janela 800 x 800: imagem 800 x 450, centrada (barras de 175 px em cima e embaixo).
void TestWideImageInSquareWindowGetsTopAndBottomBars() {
  const Rect r = letterbox({0.f, 0.f, 800.f, 800.f}, 1600.f, 900.f);
  CHECK(Near(r.w, 800.f));
  CHECK(Near(r.h, 450.f));
  CHECK(Near(r.x, 0.f));
  CHECK(Near(r.y, 175.f));
}

// Janela mais larga que a imagem: a imagem ocupa a altura toda e as barras vao para as laterais.
void TestWiderWindowGetsSideBars() {
  const Rect r = letterbox({0.f, 0.f, 1600.f, 900.f}, 800.f, 600.f);   // 4:3 numa janela 16:9
  CHECK(Near(r.h, 900.f));
  CHECK(Near(r.w, 1200.f));
  CHECK(Near(r.x, 200.f));   // 200 px de barra de cada lado
  CHECK(Near(r.y, 0.f));
  CHECK(Near(r.w / r.h, 800.f / 600.f));   // proporcao mantida
}

// Mudar o tamanho da janela recalcula: o letterbox acompanha a area nova sem guardar nada.
void TestLetterboxFollowsResize() {
  const Rect small = letterbox({10.f, 20.f, 400.f, 300.f}, 800.f, 600.f);
  const Rect big = letterbox({10.f, 20.f, 1000.f, 600.f}, 800.f, 600.f);
  CHECK(Near(small.w, 400.f) && Near(small.h, 300.f));
  CHECK(Near(big.w, 800.f) && Near(big.h, 600.f));   // o limite e a altura: a largura sobra 200 px, 100 de cada lado
  CHECK(Near(big.x, 10.f + 100.f));
}

// O mesmo encaixe que a biblioteca SpriteUI faz no modo Conter (Contain): cruza com ImageDrawRect.
void TestLetterboxMatchesSpriteUiContain() {
  const SU::Rect content{{5.f, 7.f}, {640.f, 480.f}};
  const SU::Rect lib = SU::Elements::ImageDrawRect(content, {1280.f, 1080.f}, SU::Elements::ImageFit::Contain);
  const Rect mine = letterbox({5.f, 7.f, 640.f, 480.f}, 1280.f, 1080.f);
  CHECK(Near(mine.x, lib.position.x, 0.5f));
  CHECK(Near(mine.y, lib.position.y, 0.5f));
  CHECK(Near(mine.w, lib.size.x, 0.5f));
  CHECK(Near(mine.h, lib.size.y, 0.5f));
}

// Duas cameras lado a lado: cada uma recebe metade, com a folga no meio.
void TestSplitVerticalGivesHalfEach() {
  const auto cells = splitCells({0.f, 0.f, 800.f, 500.f}, 2, true, 2.f);
  CHECK(cells.size() == 2u);
  CHECK(Near(cells[0].w, 399.f) && Near(cells[1].w, 399.f));
  CHECK(Near(cells[1].x, 401.f));
  CHECK(Near(cells[0].h, 500.f));
}

// Duas cameras em cima e embaixo: a altura e dividida e a segunda celula comeca depois da folga.
void TestSplitHorizontalStacksCells() {
  const auto cells = splitCells({0.f, 0.f, 800.f, 500.f}, 2, false, 2.f);
  CHECK(Near(cells[0].h, 249.f) && Near(cells[1].h, 249.f));
  CHECK(Near(cells[1].y, 251.f));
  CHECK(Near(cells[1].w, 800.f));
}

// Uma camera usa a area toda; zero camera nao tem celula.
void TestSplitSingleAndNone() {
  CHECK(splitCells({3.f, 4.f, 100.f, 50.f}, 1, true, 2.f).size() == 1u);
  CHECK(splitCells({0.f, 0.f, 100.f, 50.f}, 0, true, 2.f).empty());
}

// Preencher: imagem 16:9 numa area quadrada corta os lados; o recorte fica centrado e sem distorcer.
void TestFillCropsCenteredWithoutDistortion() {
  const auto uv = fillUv({0.f, 0.f, 800.f, 800.f}, 1600.f, 900.f);
  CHECK(Near(uv[2], 900.f / 1600.f));   // largura visivel = area / imagem
  CHECK(Near(uv[0], (1.f - uv[2]) * 0.5f));
  CHECK(Near(uv[1], 0.f) && Near(uv[3], 1.f));
  const auto tall = fillUv({0.f, 0.f, 800.f, 400.f}, 800.f, 800.f);   // imagem quadrada numa area 2:1: corta em cima e embaixo
  CHECK(Near(tall[3], 0.5f) && Near(tall[1], 0.25f));
}

// Tamanho zero (imagem ou area) nao divide por zero: a area toda, ou a textura toda.
void TestZeroSizesAreSafe() {
  const Rect area{1.f, 2.f, 300.f, 200.f};
  const Rect r = letterbox(area, 0.f, 0.f);
  CHECK(Near(r.x, 1.f) && Near(r.w, 300.f) && Near(r.h, 200.f));
  const auto uv = fillUv(area, 0.f, 10.f);
  CHECK(Near(uv[2], 1.f) && Near(uv[3], 1.f));
}

}  // namespace

int main() {
  TestWideImageInSquareWindowGetsTopAndBottomBars();
  TestWiderWindowGetsSideBars();
  TestLetterboxFollowsResize();
  TestLetterboxMatchesSpriteUiContain();
  TestSplitVerticalGivesHalfEach();
  TestSplitHorizontalStacksCells();
  TestSplitSingleAndNone();
  TestFillCropsCenteredWithoutDistortion();
  TestZeroSizesAreSafe();
  std::printf("Editor.GameViewModel: %d checks, %d failed\n", run, failed);
  return failed == 0 ? 0 : 1;
}
