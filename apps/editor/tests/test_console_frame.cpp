// Testes da moldura do Console (console_frame.hpp): posicao, X, arraste pelo grip e limites da janela.
// Sem janela e sem SDL. Roda no ctest como Editor.ConsoleFrame.
#include "console_frame.hpp"

#include <cstdio>

namespace {

int gFailures = 0;

void Check(bool ok, const char* what) {
  std::printf("%s: %s\n", ok ? "ok" : "FALHOU", what);
  if (!ok) ++gFailures;
}

bool Inside(ToolEngine::SpriteUi::Rect r, ToolEngine::SpriteUi::Vec2 p) {
  return p.x >= r.position.x && p.x < r.position.x + r.size.x && p.y >= r.position.y && p.y < r.position.y + r.size.y;
}

ToolEngine::SpriteUi::Vec2 Center(ToolEngine::SpriteUi::Rect r) {
  return {r.position.x + r.size.x * 0.5f, r.position.y + r.size.y * 0.5f};
}

}  // namespace

int main() {
  namespace cf = editor::console_frame;
  namespace SW = ToolEngine::SpriteUi::Widgets;
  using SU_Vec2 = ToolEngine::SpriteUi::Vec2;

  // 1. Primeira posicao: centralizada na horizontal, na parte de baixo, inteira dentro da janela.
  {
    cf::Frame f;
    f.place(1280.f, 800.f);
    const auto r = f.rect();
    Check(f.placed(), "primeira vez: a janela e posicionada");
    Check(r.size.x == cf::Metrics::kWidth && r.size.y == cf::Metrics::kHeight, "tamanho padrao 720 x 280");
    Check(r.position.x == (1280.f - r.size.x) * 0.5f, "centralizada na horizontal");
    Check(r.position.y + r.size.y <= 800.f && r.position.y > 800.f * 0.5f, "fica na parte de baixo da janela");
  }

  // 2. Janela pequena: a moldura encolhe e continua inteira dentro dela.
  {
    cf::Frame f;
    f.place(400.f, 200.f);
    const auto r = f.rect();
    Check(r.position.x >= 0.f && r.position.y >= 0.f, "janela pequena: origem nao sai da tela");
    Check(r.position.x + r.size.x <= 400.f && r.position.y + r.size.y <= 200.f, "janela pequena: cabe inteira");
  }

  // 3. Clique no X fecha; a posicao nao muda.
  {
    cf::Frame f;
    f.place(1280.f, 800.f);
    const auto before = f.position();
    const auto closeRect = SW::WindowFrameButtonRect(f.rect(), f.metrics(), false, SW::WindowFrameButton::Close);
    const cf::Event ev = f.pointerDown(Center(closeRect));
    Check(ev == cf::Event::Close, "X: clique dispara o fechamento");
    Check(!f.dragging() && f.position().x == before.x && f.position().y == before.y, "X: nao arrasta a janela");
  }

  // 4. Grip arrasta a janela; o movimento segue o ponteiro e soltar encerra o arraste.
  {
    cf::Frame f;
    f.place(1280.f, 800.f);
    const SU_Vec2 start = f.position();
    const auto grip = f.gripRect();
    const cf::Event ev = f.pointerDown(Center(grip));
    Check(ev == cf::Event::DragStart && f.dragging(), "grip: inicia o arraste");
    f.pointerMove({Center(grip).x + 60.f, Center(grip).y + 30.f});
    Check(f.position().x == start.x + 60.f && f.position().y == start.y + 30.f, "grip: a janela segue o ponteiro");
    f.pointerUp();
    Check(!f.dragging(), "soltar: encerra o arraste");
    f.pointerMove({Center(grip).x + 200.f, Center(grip).y + 200.f});
    Check(f.position().x == start.x + 60.f && f.position().y == start.y + 30.f, "depois de soltar: movimento nao arrasta");
  }

  // 5. Clique no titulo (fora do grip) e no conteudo nao arrastam nem fecham.
  {
    cf::Frame f;
    f.place(1280.f, 800.f);
    const auto title = f.titleRect();
    const SU_Vec2 onTitle{title.position.x + title.size.x * 0.7f, title.position.y + title.size.y * 0.5f};
    Check(f.pointerDown(onTitle) == cf::Event::None && !f.dragging(), "titulo fora do grip: nao arrasta");
    const auto content = f.content();
    Check(f.pointerDown(Center(content)) == cf::Event::None && !f.dragging(), "conteudo: clique nao arrasta nem fecha");
  }

  // 6. Arraste para fora da tela: a janela para na borda visivel.
  {
    cf::Frame f;
    f.place(1280.f, 800.f);
    const auto grip = f.gripRect();
    f.pointerDown(Center(grip));
    f.pointerMove({-5000.f, -5000.f});
    f.place(1280.f, 800.f);   // o host chama place() a cada quadro e prende a posicao
    const auto r = f.rect();
    Check(r.position.x >= 0.f && r.position.y >= 0.f, "arraste para fora: volta para dentro da borda superior/esquerda");
    f.pointerMove({9000.f, 9000.f});
    f.place(1280.f, 800.f);
    const auto r2 = f.rect();
    Check(r2.position.x + r2.size.x <= 1280.f && r2.position.y + r2.size.y <= 800.f,
          "arraste para fora: volta para dentro da borda inferior/direita");
  }

  // 7. Antes do primeiro quadro, nenhum clique conta.
  {
    cf::Frame f;
    Check(f.pointerDown({10.f, 10.f}) == cf::Event::None, "sem janela posicionada: clique nao faz nada");
  }

  std::printf("%s (%d falha(s))\n", gFailures == 0 ? "TODOS OS TESTES PASSARAM" : "HA FALHAS", gFailures);
  return gFailures == 0 ? 0 : 1;
}
