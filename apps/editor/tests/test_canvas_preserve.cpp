// Teste de "Preserve image": retangulo de encaixe (imagem larga em caixa alta, alta em larga, igual), ida e volta no
// JSON de preserveAspect/slotBackImage/slotFrontImage, arquivo antigo e as imagens dos slots do Inventory.
#include "canvas_widgets.hpp"
#include "components.hpp"
#include "view_shapes.hpp"

#include <cmath>
#include <cstdio>
#include <string>

static int g_fail = 0;

// Confere uma condicao e conta a falha.
static void check(bool ok, const char* what) {
  if (!ok) { std::printf("FALHOU: %s\n", what); ++g_fail; }
}

// Igualdade com folga pequena.
static bool near(float a, float b) { return std::fabs(a - b) < 1e-3f; }

int main() {
  using namespace editor;
  // --- encaixe: imagem larga em caixa alta
  view::FitRect f = view::fitAspect(100.f, 200.f, 400.f, 100.f);
  check(near(f.w, 100.f) && near(f.h, 25.f) && near(f.x, 0.f) && near(f.y, 87.5f), "larga em caixa alta: largura cheia, centrada na vertical");
  // --- imagem alta em caixa larga
  f = view::fitAspect(200.f, 100.f, 100.f, 400.f);
  check(near(f.h, 100.f) && near(f.w, 25.f) && near(f.y, 0.f) && near(f.x, 87.5f), "alta em caixa larga: altura cheia, centrada na horizontal");
  // --- proporcao igual: ocupa tudo (e amplia imagem pequena)
  f = view::fitAspect(200.f, 100.f, 20.f, 10.f);
  check(near(f.w, 200.f) && near(f.h, 100.f) && near(f.x, 0.f) && near(f.y, 0.f), "proporcao igual: caixa inteira");
  f = view::fitAspect(50.f, 50.f, 0.f, 10.f);
  check(near(f.w, 50.f) && near(f.h, 50.f), "imagem invalida: caixa inteira");

  // --- ida e volta dos campos novos
  CanvasComp a;
  a.count = 2;
  initUiKind(a.items[0], static_cast<int>(UiKind::Image));
  a.items[0].preserveAspect = true;
  initUiKind(a.items[1], static_cast<int>(UiKind::Inventory));
  a.items[1].preserveAspect = true;
  std::snprintf(a.items[1].slotBackImage, sizeof a.items[1].slotBackImage, "%s", "Assets/slot_fundo.png");
  std::snprintf(a.items[1].slotFrontImage, sizeof a.items[1].slotFrontImage, "%s", "Assets/slot_frente.jpg");
  CanvasComp b;
  readItems(itemsJson(a), b);
  check(b.count == 2 && b.items[0].preserveAspect && b.items[1].preserveAspect, "preserveAspect sobrevive");
  check(std::string(b.items[1].slotBackImage) == "Assets/slot_fundo.png", "slotBackImage sobrevive");
  check(std::string(b.items[1].slotFrontImage) == "Assets/slot_frente.jpg", "slotFrontImage sobrevive");

  // --- arquivo antigo: sem os campos
  tool::Json old = tool::Json::array();
  tool::Json q = tool::Json::object();
  q.set("kind", static_cast<int>(UiKind::Inventory));
  old.push(q);
  CanvasComp c;
  readItems(old, c);
  check(!c.items[0].preserveAspect && c.items[0].slotBackImage[0] == 0 && c.items[0].slotFrontImage[0] == 0, "arquivo antigo: padroes");

  // --- Inventory: sem imagem desenha o quadrado; com imagens, fundo no lugar do quadrado e frente por cima
  const float col[4] = {1.f, 1.f, 1.f, 1.f};
  char ent[12][32] = {};
  view::ShapeList plain, img;
  canvas_widgets::drawInventory(plain, {0, 0}, {400, 300}, col, 3.f, 15.f, 1.f, 2, 2, ent, 0);
  canvas_widgets::drawInventory(img, {0, 0}, {400, 300}, col, 3.f, 15.f, 1.f, 2, 2, ent, 0, "back.png", "front.png", true);
  int panels = 0, images = 0, fit = 0;
  for (const auto& s : plain.shapes()) panels += s.kind == view::ShapeKind::Panel;
  check(panels == 4, "sem imagens: 4 quadrados como antes");
  panels = 0;
  for (const auto& s : img.shapes()) {
    panels += s.kind == view::ShapeKind::Panel;
    if (s.kind == view::ShapeKind::Image) { ++images; fit += s.fit; }
  }
  check(panels == 0 && images == 8 && fit == 8, "com imagens: 4 fundos + 4 frentes, sem quadrado, com preserveAspect");
  check(img.shapes().size() >= 2 && img.shapes()[0].text == "back.png" && img.shapes()[1].text == "front.png", "fundo primeiro, frente depois");

  if (g_fail == 0) std::printf("OK\n");
  return g_fail == 0 ? 0 : 1;
}
