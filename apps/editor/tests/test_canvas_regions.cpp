// Testes sem janela da numeracao de regioes do Canvas (canvas_regions.hpp). Roda no ctest como Editor.CanvasRegions.
#include "canvas_regions.hpp"

#include <cstdio>

namespace {

int gFailures = 0;

void Check(bool ok, const char* what) {
  std::printf("%s: %s\n", ok ? "ok" : "FALHOU", what);
  if (!ok) ++gFailures;
}

}  // namespace

int main() {
  namespace cr = editor::canvas_regions;
  namespace cd = editor::canvas_division;

  {
    cr::Regions r;
    std::vector<cd::Point> pts;
    std::vector<cd::Line> lines;
    cr::refresh(r, pts, lines);
    Check(cr::regionAt(r, 0.5f, 0.5f) == 1, "canvas sem linhas: uma regiao, numero 1");
    Check(r.next == 2, "proximo numero depois da primeira regiao e 2");
  }
  {
    cr::Regions r;
    std::vector<cd::Point> pts;
    std::vector<cd::Line> lines;
    cr::refresh(r, pts, lines);
    cd::add(pts, 0.5f, 0.0f);    // topo
    cd::add(pts, 0.5f, 1.0f);    // base
    cd::addLine(pts, lines, 0, 1, false);   // linha vertical no meio
    cr::refresh(r, pts, lines);
    const int left = cr::regionAt(r, 0.2f, 0.5f), right = cr::regionAt(r, 0.8f, 0.5f);
    Check(left != 0 && right != 0 && left != right, "linha vertical divide o canvas em duas regioes");
    Check(left == 1, "a parte que era maior (a esquerda, com uma coluna a mais) mantem o numero 1");
    Check(right == 2, "a outra parte ganha o proximo numero (2)");
    Check(cr::regionAt(r, 0.5f, 0.5f) == 0, "a propria linha e parede (sem regiao)");
  }
  {
    // Fusao: regioes 4 e 7 separadas por uma parede; ao remover a parede, viram a regiao 4.
    cr::Regions r;
    std::vector<cd::Point> pts;
    std::vector<cd::Line> lines;
    cd::add(pts, 0.5f, 0.0f);
    cd::add(pts, 0.5f, 1.0f);
    cd::addLine(pts, lines, 0, 1, false);
    cr::refresh(r, pts, lines);
    // Forca os numeros 4 (esquerda) e 7 (direita), como se tivessem sido criados assim.
    for (std::size_t c = 0; c < r.cell.size(); ++c) {
      if (r.cell[c] == 1) r.cell[c] = static_cast<std::uint16_t>((c % cr::kGrid) < cr::kGrid / 2 ? 4 : 7);
      if (r.cell[c] == 2) r.cell[c] = 7;
    }
    r.next = 8;
    cd::removeLine(lines, 0);
    cr::refresh(r, pts, lines);
    Check(cr::regionAt(r, 0.2f, 0.5f) == 4 && cr::regionAt(r, 0.8f, 0.5f) == 4, "fusao da regiao 4 com a 7 = regiao 4");
    Check(r.next == 8, "a fusao nao cria numero novo");
  }
  {
    // Ordem de criacao: a regiao criada depois recebe o proximo numero, mesmo que outra tenha sumido antes.
    cr::Regions r;
    std::vector<cd::Point> pts;
    std::vector<cd::Line> lines;
    cr::refresh(r, pts, lines);                       // regiao 1
    cd::add(pts, 0.5f, 0.0f);
    cd::add(pts, 0.5f, 1.0f);
    cd::addLine(pts, lines, 0, 1, false);
    cr::refresh(r, pts, lines);                       // divide: 1 e 2
    cd::removeLine(lines, 0);
    cr::refresh(r, pts, lines);                       // funde: 1
    cd::add(pts, 0.5f, 0.0f);
    cd::add(pts, 0.5f, 1.0f);
    cd::addLine(pts, lines, 2, 3, false);
    cr::refresh(r, pts, lines);                       // divide de novo: 1 e 3 (o 2 nao volta)
    Check(cr::regionAt(r, 0.8f, 0.5f) == 3, "numero nunca reaproveitado: a nova divisao cria o 3");
  }
  {
    cr::Regions r;
    std::vector<cd::Point> pts;
    std::vector<cd::Line> lines;
    cr::refresh(r, pts, lines);
    float cx = 0.f, cy = 0.f;
    Check(cr::centerOf(r, 1, cx, cy) && cx > 0.4f && cx < 0.6f, "centro da regiao 1 fica no meio do canvas");
    Check(!cr::centerOf(r, 9, cx, cy), "regiao inexistente nao tem centro");
  }

  std::printf("%s\n", gFailures == 0 ? "CanvasRegions OK" : "CanvasRegions FALHOU");
  return gFailures == 0 ? 0 : 1;
}
