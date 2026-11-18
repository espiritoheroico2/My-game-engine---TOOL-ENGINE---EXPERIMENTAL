// Regras de janelas: camada, popup captura o clique, arraste so pelo grip, saida do dock com limiar.
#include "window_manager.hpp"

#include <cstdio>

namespace {

int checks = 0;
int failures = 0;

void Check(bool ok, const char* expression, int line) {
  ++checks;
  if (!ok) {
    ++failures;
    std::printf("FAIL line %d: %s\n", line, expression);
  }
}
#define CHECK(x) Check((x), #x, __LINE__)

using namespace editor::wm;

WindowState win(int id, Rect r, bool docked = false) {
  WindowState w;
  w.id = id; w.rect = r; w.docked = docked; w.zOrder = id;
  return w;
}

}  // namespace

int main() {
  // 1) Janela flutuante fica sempre acima de encaixada, mesmo com zOrder maior na encaixada.
  {
    WindowManager m;
    WindowState docked = win(1, {0, 0, 200, 200}, true);
    docked.zOrder = 100;
    m.upsert(docked);
    m.upsert(win(2, {50, 50, 200, 200}, false));
    CHECK(m.pick(60, 60) == 2);
    CHECK(m.pick(10, 10) == 1);   // so a encaixada cobre este ponto
  }

  // 2) Popup aberto captura o clique: fora dele nao chega a janela de tras; dentro, o popup recebe.
  {
    WindowManager m;
    m.upsert(win(1, {0, 0, 300, 300}));
    WindowState menu = win(7, {100, 100, 80, 120});
    menu.popup = true;
    m.upsert(menu);
    CHECK(m.pick(150, 150) == 1);                 // popup fechado: a janela recebe
    m.setOpenPopup(7, true);
    CHECK(m.pick(150, 150) == 7);                 // popup aberto: o menu recebe
    CHECK(m.pick(20, 20) == kCapturedByPopup);    // fora do menu: captura, a janela de tras nao recebe
    m.setOpenPopup(7, false);
    CHECK(m.pick(20, 20) == 1);
  }

  // 3) Arraste so pelo grip: clique no meio do titulo nao move a janela.
  {
    WindowManager m;
    WindowState w = win(3, {100, 100, 300, 200});
    m.upsert(w);
    CHECK(!m.beginPress(3, 200, 110));            // titulo, fora do grip
    CHECK(!m.drag().active);
    CHECK(m.beginPress(3, 104, 110));             // grip (14 px a esquerda)
    CHECK(m.drag().active);
    CHECK(m.movePointer(150, 130));
    CHECK(m.find(3)->rect.x == 146.f && m.find(3)->rect.y == 120.f);
    CHECK(m.endPress());                          // houve arraste
    CHECK(!m.drag().active);
  }

  // 4) Encaixada: arrastar pouco nao tira do dock; passar do limiar tira.
  {
    WindowManager m;
    m.upsert(win(4, {0, 0, 300, 200}, true));
    CHECK(m.beginPress(4, 5, 5));
    CHECK(!m.movePointer(10, 5));                 // 5 px: abaixo do limiar
    CHECK(m.find(4)->docked);
    CHECK(m.movePointer(40, 5));                  // 35 px: passa do limiar
    CHECK(!m.find(4)->docked);
    CHECK(m.effectiveLayer(*m.find(4)) == Layer::DragPreview);
    CHECK(m.endPress());
    CHECK(!m.find(4)->docked);                    // continua solta depois de soltar
  }

  // 5) Clique sem arraste nao conta como arraste (acoes da janela continuam).
  {
    WindowManager m;
    m.upsert(win(5, {0, 0, 300, 200}, true));
    CHECK(m.beginPress(5, 5, 5));
    CHECK(!m.endPress());
    CHECK(m.find(5)->docked);
  }

  // 6) bringToFront: a janela clicada sobe dentro da propria camada.
  {
    WindowManager m;
    m.upsert(win(10, {0, 0, 200, 200}));
    m.upsert(win(11, {0, 0, 200, 200}));
    CHECK(m.pick(10, 10) == 11);                  // maior id ganha no empate de zOrder
    m.bringToFront(10);
    CHECK(m.pick(10, 10) == 10);
  }

  // 7) Janela escondida nao recebe clique.
  {
    WindowManager m;
    WindowState w = win(20, {0, 0, 100, 100});
    w.visible = false;
    m.upsert(w);
    CHECK(m.pick(10, 10) == kNoTarget);
  }

  // 8) Remover uma janela que esta sendo arrastada cancela o arraste.
  {
    WindowManager m;
    m.upsert(win(30, {0, 0, 300, 200}));
    CHECK(m.beginPress(30, 5, 5));
    m.remove(30);
    CHECK(!m.drag().active);
  }

  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
