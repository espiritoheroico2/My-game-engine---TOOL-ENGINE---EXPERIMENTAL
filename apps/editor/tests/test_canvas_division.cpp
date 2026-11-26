// Testes sem janela dos pontos de divisao do Canvas (canvas_division.hpp). Roda no ctest como Editor.CanvasDivision.
#include "canvas_division.hpp"

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
  namespace cd = editor::canvas_division;
  const cd::Rect canvas{100.f, 50.f, 400.f, 300.f};

  {
    std::vector<cd::Point> pts;
    const std::size_t i = cd::add(pts, 0.5f, 0.25f);
    Check(i == 0 && pts.size() == 1, "add cria um ponto e devolve o indice");
    Check(cd::displayName(pts[0], 0) == "P1", "nome padrao do primeiro ponto e P1");
    pts[0].name = "entrada";
    Check(cd::displayName(pts[0], 0) == "entrada", "nome dado pelo usuario substitui o padrao");
  }
  {
    std::vector<cd::Point> pts;
    cd::add(pts, 2.f, -1.f);
    Check(Near(pts[0].x, 1.f) && Near(pts[0].y, 0.f), "add limita a fracao ao canvas (0..1)");
    cd::move(pts, 0, 0.3f, 0.6f);
    Check(Near(pts[0].x, 0.3f) && Near(pts[0].y, 0.6f), "move muda a fracao");
    cd::move(pts, 0, 9.f, 9.f);
    Check(Near(pts[0].x, 1.f) && Near(pts[0].y, 1.f), "move limita ao canvas");
    cd::move(pts, 5, 0.1f, 0.1f);
    Check(pts.size() == 1, "move com indice invalido nao muda nada");
  }
  {
    std::vector<cd::Point> pts;
    cd::add(pts, 0.5f, 0.25f);
    float sx = 0.f, sy = 0.f;
    cd::toPixels(pts[0], canvas, sx, sy);
    Check(Near(sx, 300.f) && Near(sy, 125.f), "toPixels: 0.5 e 0.25 dentro de (100,50,400x300) vira (300,125)");
    Check(cd::hitAt(pts, canvas, 303.f, 128.f) == 0, "clique a 3 px acerta o ponto");
    Check(cd::hitAt(pts, canvas, 320.f, 125.f) == -1, "clique a 20 px nao acerta");
  }
  {
    std::vector<cd::Point> pts;
    cd::add(pts, 0.1f, 0.1f);
    cd::add(pts, 0.9f, 0.9f);
    std::vector<cd::Line> none;
    cd::remove(pts, none, 0);
    Check(pts.size() == 1 && Near(pts[0].x, 0.9f), "remove apaga o ponto certo");
    cd::remove(pts, none, 7);
    Check(pts.size() == 1, "remove com indice invalido nao muda nada");
  }

  {
    std::vector<cd::Point> pts;
    cd::add(pts, 0.0f, 0.5f);   // 0: (100,200)
    cd::add(pts, 1.0f, 0.5f);   // 1: (500,200)
    cd::add(pts, 0.5f, 1.0f);   // 2: (300,350)
    std::vector<cd::Line> lines;
    Check(cd::addLine(pts, lines, 0, 1, false), "addLine liga dois pontos");
    Check(!cd::addLine(pts, lines, 1, 0, true), "addLine recusa a mesma linha no sentido contrario");
    Check(!cd::addLine(pts, lines, 2, 2, false), "addLine recusa linha de um ponto para ele mesmo");
    cd::addLine(pts, lines, 1, 2, true);
    Check(cd::lineAt(pts, lines, canvas, 300.f, 202.f) == 0, "clique a 2 px do meio da linha 0 acerta a linha");
    Check(cd::lineAt(pts, lines, canvas, 300.f, 230.f) == -1, "clique a 30 px da linha nao acerta");
    Check(cd::lineAt(pts, lines, canvas, 400.f, 275.f) == 1, "linha pontilhada tambem acerta (no meio do segmento)");
    cd::remove(pts, lines, 1);
    Check(pts.size() == 2 && lines.empty(), "remover o ponto apaga as linhas que o usavam");
  }
  {
    std::vector<cd::Point> pts;
    cd::add(pts, 0.1f, 0.1f);   // 0
    cd::add(pts, 0.2f, 0.2f);   // 1
    cd::add(pts, 0.3f, 0.3f);   // 2
    std::vector<cd::Line> lines;
    cd::addLine(pts, lines, 2, 0, false);
    cd::remove(pts, lines, 1);
    Check(lines.size() == 1 && lines[0].a == 1 && lines[0].b == 0, "remover um ponto desce os indices das linhas restantes");
    cd::removeLine(lines, 0);
    Check(lines.empty(), "removeLine apaga a linha");
  }

  std::printf("%s\n", gFailures == 0 ? "CanvasDivision OK" : "CanvasDivision FALHOU");
  return gFailures == 0 ? 0 : 1;
}
