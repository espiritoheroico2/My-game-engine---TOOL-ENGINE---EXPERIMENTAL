// Regras puras do redimensionamento pelo mouse (resize_hover.hpp): cursor por borda e destaque da borda.
// Sem janela e sem SDL. Roda no ctest como Editor.ResizeHover.
#include "resize_hover.hpp"

#include <cstdio>

namespace {

int gFailures = 0;
int gChecks = 0;

void Check(bool ok, const char* what) {
  ++gChecks;
  std::printf("%s: %s\n", ok ? "ok" : "FALHOU", what);
  if (!ok) ++gFailures;
}

}  // namespace

int main() {
  namespace rh = editor::resize_hover;
  using CS = rh::CursorShape;
  namespace SU = ToolEngine::SpriteUi;

  // 1. Cursor por borda: lados viram setas, cantos viram diagonais, sem borda vira seta normal.
  Check(rh::EdgeCursor(0) == CS::Arrow, "sem borda: seta normal");
  Check(rh::EdgeCursor(1) == CS::ResizeH, "borda esquerda: seta horizontal");
  Check(rh::EdgeCursor(2) == CS::ResizeH, "borda direita: seta horizontal");
  Check(rh::EdgeCursor(4) == CS::ResizeV, "borda do topo: seta vertical");
  Check(rh::EdgeCursor(8) == CS::ResizeV, "borda de baixo: seta vertical");
  Check(rh::EdgeCursor(5) == CS::ResizeNWSE, "canto superior esquerdo: diagonal \\");
  Check(rh::EdgeCursor(10) == CS::ResizeNWSE, "canto inferior direito: diagonal \\");
  Check(rh::EdgeCursor(6) == CS::ResizeNESW, "canto superior direito: diagonal /");
  Check(rh::EdgeCursor(9) == CS::ResizeNESW, "canto inferior esquerdo: diagonal /");

  // 2. Divisoria: corte lado a lado pede seta horizontal; corte empilhado pede vertical.
  Check(rh::DividerCursor(true) == CS::ResizeH, "divisoria lado a lado: horizontal");
  Check(rh::DividerCursor(false) == CS::ResizeV, "divisoria empilhada: vertical");

  // 3. Destaque da borda: uma faixa por borda marcada, dentro da moldura.
  const SU::Rect win{{100.f, 50.f}, {300.f, 200.f}};
  const rh::Highlight left = rh::EdgeHighlight(win, 1, 4.f);
  Check(left.count == 1 && left.rects[0].position.x == 100.f && left.rects[0].size.x == 4.f,
        "borda esquerda: faixa de 4 px encostada na esquerda");
  const rh::Highlight corner = rh::EdgeHighlight(win, 2 | 8, 4.f);
  Check(corner.count == 2, "canto inferior direito: duas faixas");
  Check(corner.rects[0].position.x == 100.f + 300.f - 4.f, "faixa direita fica por dentro da borda direita");
  Check(corner.rects[1].position.y == 50.f + 200.f - 4.f, "faixa de baixo fica por dentro da borda de baixo");
  Check(rh::EdgeHighlight(win, 0, 4.f).count == 0, "sem borda, nenhum destaque");

  // 4. Destaque da divisoria: faixa centrada no corte, do tamanho da folha.
  const SU::Rect leaf{{0.f, 0.f}, {800.f, 600.f}};
  const SU::Rect vert = rh::DividerHighlight(leaf, true, 400.f, 4.f);
  Check(vert.position.x == 398.f && vert.size.x == 4.f && vert.size.y == 600.f, "corte lado a lado: faixa vertical centrada");
  const SU::Rect horz = rh::DividerHighlight(leaf, false, 300.f, 4.f);
  Check(horz.position.y == 298.f && horz.size.y == 4.f && horz.size.x == 800.f, "corte empilhado: faixa horizontal centrada");

  std::printf("Editor.ResizeHover: %d checks, %d failed\n", gChecks, gFailures);
  return gFailures == 0 ? 0 : 1;
}
