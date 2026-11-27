// Teste dos recursos novos do Canvas: campos novos no JSON (ida e volta e arquivo antigo), quebra de linha de texto,
// sombra em camadas e os elementos compostos (Window, Menu, List, Inventory, ListContainer).
#include "canvas_text.hpp"
#include "canvas_widgets.hpp"
#include "components.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

static int g_fail = 0;

// Confere uma condicao e conta a falha.
static void check(bool ok, const char* what) {
  if (!ok) { std::printf("FALHOU: %s\n", what); ++g_fail; }
}

// Medida falsa: 10 unidades por byte, para a quebra ser previsivel.
static float fake(const std::string& s) { return 10.f * static_cast<float>(s.size()); }

// Conta as formas de um tipo na lista.
static int countKind(const editor::view::ShapeList& l, editor::view::ShapeKind k) {
  int n = 0;
  for (const auto& s : l.shapes()) n += s.kind == k ? 1 : 0;
  return n;
}

int main() {
  using namespace editor;
  // --- ida e volta de todos os campos novos
  CanvasComp a;
  a.count = 2;
  UiElem& e = a.items[0];
  initUiKind(e, static_cast<int>(UiKind::Inventory));
  e.rows = 5; e.cols = 7;
  std::snprintf(e.image, sizeof e.image, "%s", "Assets/icone.png");
  e.shadow = true; e.shadowX = 3.f; e.shadowY = -4.f; e.shadowBlur = 6.f;
  e.shadowColor[0] = 0.1f; e.shadowColor[1] = 0.2f; e.shadowColor[2] = 0.3f; e.shadowColor[3] = 0.4f;
  e.wrapWidth = 250.f;
  e.entryCount = 2;
  std::snprintf(e.entries[0], kUiEntryLen, "%s", "Espada");
  std::snprintf(e.entries[1], kUiEntryLen, "%s", "Escudo");
  initUiKind(a.items[1], static_cast<int>(UiKind::Menu));
  CanvasComp b;
  readItems(itemsJson(a), b);
  const UiElem& r = b.items[0];
  check(b.count == 2, "dois elementos lidos");
  check(r.kind == static_cast<int>(UiKind::Inventory), "tipo novo sobrevive");
  check(std::string(r.image) == "Assets/icone.png", "caminho da imagem");
  check(r.shadow && r.shadowX == 3.f && r.shadowY == -4.f && r.shadowBlur == 6.f, "sombra: ligada, deslocamento, blur");
  check(r.shadowColor[0] == 0.1f && r.shadowColor[3] == 0.4f, "sombra: cor");
  check(r.wrapWidth == 250.f, "largura de quebra");
  check(r.rows == 5 && r.cols == 7, "linhas e colunas do inventario");
  check(r.entryCount == 2 && std::string(r.entries[1]) == "Escudo", "entradas");
  check(b.items[1].entryCount == 3 && std::string(b.items[1].entries[0]) == "Start", "Menu: entradas padrao");
  CanvasComp twice;
  readItems(itemsJson(b), twice);
  check(std::string(twice.items[0].entries[0]) == "Espada" && twice.items[0].shadowBlur == 6.f, "segunda ida e volta igual");

  // --- arquivo antigo: sem os campos novos
  tool::Json old = tool::Json::array();
  tool::Json q = tool::Json::object();
  q.set("kind", 1);
  old.push(q);
  CanvasComp c;
  readItems(old, c);
  const UiElem& o = c.items[0];
  check(o.image[0] == 0 && !o.shadow && o.wrapWidth == 0.f && o.entryCount == 0, "arquivo antigo: sem imagem, sem sombra, sem quebra");
  check(o.radius == 4.f, "arquivo antigo: raio do Panel continua 4");
  check(o.rows == 3 && o.cols == 4, "arquivo antigo: inventario 3x4");
  // Valores absurdos sao presos.
  tool::Json bad = tool::Json::array();
  tool::Json bq = tool::Json::object();
  bq.set("kind", 99); bq.set("rows", 1000.0); bq.set("wrap", -5.0);
  bad.push(bq);
  CanvasComp d;
  readItems(bad, d);
  check(d.items[0].kind == static_cast<int>(UiKind::Count) - 1 && d.items[0].rows == 16 && d.items[0].wrapWidth == 0.f, "valores fora da faixa sao presos");

  // --- quebra de linha
  using canvas_text::wrapLines;
  auto l1 = wrapLines("aaa bbb ccc", 50.f, fake);
  check(l1.size() == 3 && l1[0] == "aaa" && l1[2] == "ccc", "largura 50: tres linhas");
  auto l2 = wrapLines("aaa bbb ccc", 80.f, fake);
  check(l2.size() == 2 && l2[0] == "aaa bbb" && l2[1] == "ccc", "largura 80: duas linhas");
  auto l3 = wrapLines("aaa bbb ccc", 0.f, fake);
  check(l3.size() == 1 && l3[0] == "aaa bbb ccc", "largura 0: sem quebra");
  auto l4 = wrapLines("ab\ncd", 1000.f, fake);
  check(l4.size() == 2 && l4[1] == "cd", "quebra forcada por newline");
  auto l5 = wrapLines("abcdefgh ij", 30.f, fake);
  check(l5.size() == 4 && l5[0] == "abc" && l5[1] == "def" && l5[2] == "gh" && l5[3] == "ij", "palavra longa e partida em pedacos de 3");
  check(wrapLines("", 50.f, fake).size() == 1, "texto vazio: uma linha vazia");
  auto l6 = wrapLines("a   b", 100.f, fake);
  check(l6.size() == 1 && l6[0] == "a b", "espacos repetidos colapsam");

  // --- sombra
  canvas_paint::Shadow sh;
  view::ShapeList off;
  canvas_paint::drawShadowBox(off, {0, 0}, {100, 50}, 0.f, sh);
  check(off.shapes().empty(), "sombra desligada: nada");
  sh.on = true;
  view::ShapeList hard;
  canvas_paint::drawShadowBox(hard, {0, 0}, {100, 50}, 0.f, sh);
  check(hard.shapes().size() == 1 && hard.shapes()[0].a.x == 2.f, "sombra dura: uma copia deslocada");
  sh.blur = 6.f;
  view::ShapeList soft;
  canvas_paint::drawShadowBox(soft, {0, 0}, {100, 50}, 0.f, sh);
  check(soft.shapes().size() == static_cast<size_t>(canvas_paint::shadowSteps(6.f)) && soft.shapes().size() > 1, "sombra suave: varias camadas");
  check(soft.shapes().front().a.x < soft.shapes().back().a.x, "camada mais larga vem primeiro");
  int texts = 0;
  canvas_paint::drawShadowText({10, 10}, sh, [&](view::Pixel, view::Color) { ++texts; });
  check(texts > 1, "sombra de texto com blur: varias copias");

  // --- elementos compostos
  const float col[4] = {1.f, 1.f, 1.f, 1.f};
  char ent[3][32] = {"A", "B", "C"};
  view::ShapeList w;
  canvas_widgets::drawWindow(w, {0, 0}, {300, 200}, col, 4.f, 15.f, "Titulo");
  check(countKind(w, view::ShapeKind::Text) == 1, "Window: o titulo");
  view::ShapeList mn;
  canvas_widgets::drawMenu(mn, {0, 0}, {100, 120}, col, 0.f, 15.f, 1.f, ent, 3);
  check(countKind(mn, view::ShapeKind::Text) == 3, "Menu: um rotulo por botao");
  float h0 = -1.f;
  bool equal = true;
  for (const auto& s : mn.shapes()) {
    if (s.kind != view::ShapeKind::RoundRect && s.kind != view::ShapeKind::Panel) continue;
    const float h = s.b.y - s.a.y;
    if (h0 < 0.f) h0 = h;
    else if (std::abs(h - h0) > 0.01f) equal = false;
  }
  check(equal && h0 > 0.f, "Menu: botoes com a mesma altura");
  view::ShapeList ls;
  canvas_widgets::drawList(ls, {0, 0}, {200, 300}, col, 0.f, 15.f, ent, 3);
  check(countKind(ls, view::ShapeKind::Text) == 3 && countKind(ls, view::ShapeKind::Line) == 2, "List: 3 linhas e 2 separadores");
  view::ShapeList inv;
  canvas_widgets::drawInventory(inv, {0, 0}, {400, 300}, col, 3.f, 15.f, 1.f, 3, 4, ent, 2);
  check(countKind(inv, view::ShapeKind::Panel) == 12 && countKind(inv, view::ShapeKind::Text) == 2, "Inventory: 3x4 slots e 2 rotulos");
  view::ShapeList lc;
  char many[12][32] = {};
  for (int i = 0; i < 12; ++i) std::snprintf(many[i], 32, "Row %d", i);
  const int shown = canvas_widgets::drawListContainer(lc, {0, 0}, {200, 100}, col, 0.f, 15.f, many, 12);
  check(shown > 0 && shown < 12 && countKind(lc, view::ShapeKind::Text) == shown, "ListContainer: so as linhas que cabem");

  std::printf(g_fail ? "%d falha(s)\n" : "ok\n", g_fail);
  return g_fail ? 1 : 0;
}
