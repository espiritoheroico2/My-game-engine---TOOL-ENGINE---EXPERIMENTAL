// Testes da moldura do Console (window_frame.hpp): posicao, X, arraste pelo grip e limites da janela.
// Sem janela e sem SDL. Roda no ctest como Editor.ConsoleFrame.
#include "window_frame.hpp"

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
  namespace wf = editor::window_frame;
  namespace SW = ToolEngine::SpriteUi::Widgets;
  using SU_Vec2 = ToolEngine::SpriteUi::Vec2;

  // 1. Primeira posicao: centralizada na horizontal, na parte de baixo, inteira dentro da janela.
  {
    wf::Frame f;
    f.place(1280.f, 800.f);
    const auto r = f.rect();
    Check(f.placed(), "primeira vez: a janela e posicionada");
    Check(r.size.x == wf::Metrics::kWidth && r.size.y == wf::Metrics::kHeight, "tamanho padrao 720 x 280");
    Check(r.position.x == (1280.f - r.size.x) * 0.5f, "centralizada na horizontal");
    Check(r.position.y + r.size.y <= 800.f && r.position.y > 800.f * 0.5f, "fica na parte de baixo da janela");
  }

  // 2. Janela pequena: a moldura encolhe e continua inteira dentro dela.
  {
    wf::Frame f;
    f.place(400.f, 200.f);
    const auto r = f.rect();
    Check(r.position.x >= 0.f && r.position.y >= 0.f, "janela pequena: origem nao sai da tela");
    Check(r.position.x + r.size.x <= 400.f && r.position.y + r.size.y <= 200.f, "janela pequena: cabe inteira");
  }

  // 3. Clique no X fecha; a posicao nao muda.
  {
    wf::Frame f;
    f.place(1280.f, 800.f);
    const auto before = f.position();
    const auto closeRect = SW::WindowFrameButtonRect(f.rect(), f.metrics(), false, SW::WindowFrameButton::Close);
    const wf::Event ev = f.pointerDown(Center(closeRect));
    Check(ev == wf::Event::Close, "X: clique dispara o fechamento");
    Check(!f.dragging() && f.position().x == before.x && f.position().y == before.y, "X: nao arrasta a janela");
  }

  // 4. Grip arrasta a janela; o movimento segue o ponteiro e soltar encerra o arraste.
  {
    wf::Frame f;
    f.place(1280.f, 800.f);
    const SU_Vec2 start = f.position();
    const auto grip = f.gripRect();
    const wf::Event ev = f.pointerDown(Center(grip));
    Check(ev == wf::Event::DragStart && f.dragging(), "grip: inicia o arraste");
    f.pointerMove({Center(grip).x + 60.f, Center(grip).y + 30.f});
    Check(f.position().x == start.x + 60.f && f.position().y == start.y + 30.f, "grip: a janela segue o ponteiro");
    f.pointerUp();
    Check(!f.dragging(), "soltar: encerra o arraste");
    f.pointerMove({Center(grip).x + 200.f, Center(grip).y + 200.f});
    Check(f.position().x == start.x + 60.f && f.position().y == start.y + 30.f, "depois de soltar: movimento nao arrasta");
  }

  // 5. Clique no titulo (fora do grip) e no conteudo nao arrastam nem fecham.
  {
    wf::Frame f;
    f.place(1280.f, 800.f);
    const auto title = f.titleRect();
    const SU_Vec2 onTitle{title.position.x + title.size.x * 0.7f, title.position.y + title.size.y * 0.5f};
    Check(f.pointerDown(onTitle) == wf::Event::None && !f.dragging(), "titulo fora do grip: nao arrasta");
    const auto content = f.content();
    Check(f.pointerDown(Center(content)) == wf::Event::None && !f.dragging(), "conteudo: clique nao arrasta nem fecha");
  }

  // 6. Arraste para fora da tela: a janela para na borda visivel.
  {
    wf::Frame f;
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
    wf::Frame f;
    Check(f.pointerDown({10.f, 10.f}) == wf::Event::None, "sem janela posicionada: clique nao faz nada");
  }

  // 8. Icone "?": circulo logo depois do grip, dentro do cabecalho. Clique nele pede a Help (nao arrasta).
  {
    wf::Frame f;
    f.place(1280.f, 800.f);
    const auto help = f.helpRect();
    const auto grip = f.gripRect();
    Check(help.position.x >= grip.position.x + grip.size.x, "? fica depois do grip");
    Check(help.position.y >= f.header().position.y && help.position.y + help.size.y <= f.header().position.y + f.header().size.y,
          "? cabe na altura do cabecalho");
    Check(f.titleTextX() >= help.position.x + help.size.x, "titulo comeca depois do ?");
    Check(f.pointerDown(Center(help)) == wf::Event::Help, "clique no ? pede a Help");
    Check(!f.dragging(), "clique no ? nao arrasta");
  }

  // 9. Minimizar: o botao da moldura recolhe a janela ao cabecalho; de novo volta a altura cheia.
  {
    wf::Frame f;
    f.place(1280.f, 800.f);
    const float full = f.rect().size.y;
    const auto minimize = wf::Event::Minimize;
    const auto btn = ToolEngine::SpriteUi::Widgets::WindowFrameButtonRect(f.rect(), f.metrics(), false,
                                                                         ToolEngine::SpriteUi::Widgets::WindowFrameButton::Minimize);
    Check(f.pointerDown(Center(btn)) == minimize, "clique no minimizar devolve Minimize");
    Check(f.minimized(), "depois do clique a janela esta minimizada");
    f.place(1280.f, 800.f);
    Check(f.rect().size.y == f.metrics().headerHeight, "minimizada: so o cabecalho aparece");
    f.pointerDown(Center(ToolEngine::SpriteUi::Widgets::WindowFrameButtonRect(f.rect(), f.metrics(), false,
                                                                             ToolEngine::SpriteUi::Widgets::WindowFrameButton::Minimize)));
    f.place(1280.f, 800.f);
    Check(!f.minimized() && f.rect().size.y == full, "segundo clique: volta a altura cheia");
  }

  std::printf("%s (%d falha(s))\n", gFailures == 0 ? "TODOS OS TESTES PASSARAM" : "HA FALHAS", gFailures);
  return gFailures == 0 ? 0 : 1;
}
