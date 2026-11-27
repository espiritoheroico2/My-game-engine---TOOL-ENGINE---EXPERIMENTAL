// Teste do raio de canto dos elementos de UI: limite em metade do menor lado e gravar/ler no JSON.
#include "canvas_paint.hpp"
#include "components.hpp"

#include <cstdio>

static int g_fail = 0;

// Confere uma condicao e conta a falha.
static void check(bool ok, const char* what) {
  if (!ok) { std::printf("FALHOU: %s\n", what); ++g_fail; }
}

int main() {
  using editor::canvas_paint::clampRadius;
  check(clampRadius(5.f, 100.f, 40.f) == 5.f, "raio pequeno fica igual");
  check(clampRadius(999.f, 100.f, 40.f) == 20.f, "raio grande limita em metade do menor lado");
  check(clampRadius(999.f, 30.f, 80.f) == 15.f, "menor lado e a largura");
  check(clampRadius(-3.f, 100.f, 40.f) == 0.f, "raio negativo vira 0");
  check(clampRadius(10.f, 0.f, 40.f) == 0.f, "lado zero: sem raio");

  // Ida e volta: salva com raio e le de volta.
  editor::CanvasComp a;
  a.count = 1;
  a.items[0].kind = static_cast<int>(editor::UiKind::Button);
  a.items[0].radius = 12.5f;
  tool::Json arr = editor::itemsJson(a);
  editor::CanvasComp b;
  editor::readItems(arr, b);
  check(b.count == 1, "um elemento lido");
  check(b.items[0].radius == 12.5f, "raio sobrevive a ida e volta");

  // Arquivo antigo sem o campo "radius": Panel vira 4, Button vira 6, Text fica 0.
  tool::Json old = tool::Json::array();
  for (int kind : {1, 2, 0}) {
    tool::Json q = tool::Json::object();
    q.set("kind", kind);
    old.push(q);
  }
  editor::CanvasComp c;
  editor::readItems(old, c);
  check(c.count == 3, "tres elementos antigos lidos");
  check(c.items[0].radius == 4.f, "arquivo antigo: Panel 4");
  check(c.items[1].radius == 6.f, "arquivo antigo: Button 6");
  check(c.items[2].radius == 0.f, "arquivo antigo: Text 0");

  std::printf(g_fail ? "%d falha(s)\n" : "ok\n", g_fail);
  return g_fail ? 1 : 0;
}
