// Gestos do ponteiro no host do dock (dock_host.hpp): grip arrasta, borda redimensiona, e o gesto se repete.
// Sem janela: o host so usa SDL_Event e geometria; o SDL e carregado so para o destrutor do renderizador.
// Roda no ctest como Editor.DockHost.
#include "dock_host.hpp"

#include <cstdio>

namespace {

int gFailures = 0;  // Quantidade de verificacoes que falharam.
int gChecks = 0;  // Quantidade de verificacoes feitas.

// Imprime ok/FALHOU para a verificacao e conta as falhas.
void Check(bool ok, const char* what) {
  ++gChecks;
  std::printf("%s: %s\n", ok ? "ok" : "FALHOU", what);
  if (!ok) ++gFailures;
}

// Evento sintetico de botao esquerdo do mouse (apertar ou soltar) na posicao dada.
SDL_Event Button(bool down, float x, float y) {
  SDL_Event e{};
  e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
  e.button.button = SDL_BUTTON_LEFT;
  e.button.x = x;
  e.button.y = y;
  return e;
}

// Evento sintetico de movimento do mouse; `held` simula o botao esquerdo apertado.
SDL_Event Motion(float x, float y, bool held) {
  SDL_Event e{};
  e.type = SDL_EVENT_MOUSE_MOTION;
  e.motion.x = x;
  e.motion.y = y;
  e.motion.state = held ? SDL_BUTTON_LMASK : 0;
  return e;
}

}  // namespace

// Roda os testes do host do dock: arraste do grip (flutuar), redimensionar pela borda duas vezes e geometria das abas.
int main() {
  namespace dh = editor::dock_host;
  namespace SU = ToolEngine::SpriteUi;
  namespace wf = editor::window_frame;

  constexpr int kWinW = 1600, kWinH = 900;
  editor::sui::TextAtlasSet text;
  text.load("", 1.f);   // fonte embutida: as abas medem os titulos
  editor::sui::SpriteUiSdlRenderer sdl;
  dh::Host host(text, sdl);

  // Janela de teste: a moldura e a unica fonte de geometria; o host manda a area do dock em setDock.
  wf::Frame frame;
  frame.placeIn(kWinW, kWinH, std::nullopt, 400.f, 300.f, 100.f);
  bool floated = false;
  dh::Window w;
  w.id = dh::idOf(dh::Kind::Hierarchy);
  w.open = [] { return true; };
  w.close = [] {};
  w.frame = [&frame] { return std::optional<SU::Rect>(frame.rect()); };
  w.grip = [&frame] { return frame.gripRect(); };
  w.setDock = [&frame](std::optional<SU::Rect> d) { frame.placeIn(kWinW, kWinH, d, 400.f, 300.f, 100.f); };
  w.floatAt = [&frame, &floated](SU::Vec2 at) {
    frame.undock(at);
    floated = true;
  };
  w.frameState = [&frame] { return &frame; };
  host.add(w);

  const SU::Rect area{{0.f, 0.f}, {static_cast<float>(kWinW), 800.f}};   // faixa de baixo fica fora do dock (barra de status)
  host.beginFrame(area, kWinW, kWinH);
  host.beginFrame(area, kWinW, kWinH);

  // 1. Grip de uma janela no dock: pressionar, arrastar para longe e soltar tira a janela do dock (flutua).
  const SU::Rect grip = frame.gripRect();
  const float gx = grip.position.x + grip.size.x * 0.5f, gy = grip.position.y + grip.size.y * 0.5f;
  Check(grip.size.x > 0.f && grip.size.y > 0.f, "grip existe no quadro");
  Check(host.onEvent(Button(true, gx, gy)), "grip consome o clique");
  // Soltar fora de qualquer folha (abaixo da area do dock) flutua; dentro da folha e so reorganizacao.
  Check(host.onEvent(Motion(700.f, 850.f, true)), "movimento com grip preso e consumido pelo host");
  Check(host.onEvent(Button(false, 700.f, 850.f)), "soltura consumida pelo host");
  Check(floated, "soltar longe do dock flutua a janela");
  host.beginFrame(area, kWinW, kWinH);

  // 2. Redimensionar pela borda direita da janela flutuante, duas vezes seguidas.
  for (int round = 0; round < 2; ++round) {
    const SU::Rect r = frame.rect();
    const float edgeX = r.position.x + r.size.x - 2.f, midY = r.position.y + r.size.y * 0.5f;
    const float before = frame.rect().size.x;
    Check(host.onEvent(Button(true, edgeX, midY)), "borda consome o clique");
    Check(host.onEvent(Motion(edgeX + 60.f, midY, true)), "movimento na borda consumido");
    host.beginFrame(area, kWinW, kWinH);
    Check(frame.rect().size.x > before + 30.f, "largura segue o ponteiro no quadro seguinte");
    Check(host.onEvent(Button(false, edgeX + 60.f, midY)), "soltura da borda consumida");
    host.beginFrame(area, kWinW, kWinH);
    const float after = frame.rect().size.x;
    // Depois de soltar, mover o mouse nao pode mais mudar a janela.
    (void)host.onEvent(Motion(edgeX + 200.f, midY, false));
    host.beginFrame(area, kWinW, kWinH);
    Check(frame.rect().size.x == after, "depois de soltar a janela nao segue mais o mouse");
  }

  // 3. Geometria pura das abas: largura pelo titulo, X sempre com espaco proprio, "..." no lugar de sobrepor.
  {
    const std::vector<float> fit = dh::tabWidths({40.f, 60.f}, 1000.f);
    Check(fit.size() == 2 && fit[0] == dh::kTabOverhead + 40.f && fit[1] == dh::kTabOverhead + 60.f, "aba tem a largura do titulo mais grip e X");
    const std::vector<float> longTitle = dh::tabWidths({900.f}, 1000.f);
    Check(longTitle[0] == dh::kTabMaxWidth, "titulo enorme e limitado a largura maxima da aba");
    const std::vector<float> tight = dh::tabWidths({80.f, 80.f, 80.f}, 300.f);
    float tightSum = 0.f;
    for (float v : tight) tightSum += v;
    Check(tightSum <= 300.f + 0.01f, "abas encolhem juntas para caber na barra");
    bool allMin = true;
    for (float v : tight) allMin = allMin && v >= dh::kTabMinWidth - 0.01f;
    Check(allMin, "aba encolhida mantem espaco para grip, X e um pedaco do nome");
    const std::vector<float> tiny = dh::tabWidths({80.f, 80.f, 80.f, 80.f}, 100.f);
    float tinySum = 0.f;
    for (float v : tiny) tinySum += v;
    Check(tinySum <= 100.01f, "barra minuscula: abas dividem a largura sem passar dela");
    auto measure = [](const std::string& t) { return static_cast<float>(t.size()) * 7.f; };
    Check(dh::fitText("Hierarchy", 200.f, measure) == "Hierarchy", "titulo que cabe fica inteiro");
    const std::string cut = dh::fitText("Hierarchy", 50.f, measure);
    Check(cut.size() >= 3 && cut.substr(cut.size() - 3) == "..." && measure(cut) <= 50.f, "titulo longo vira texto com ... que cabe na faixa");
    Check(dh::fitText("Hierarchy", 0.f, measure).empty(), "faixa de largura 0 nao desenha o nome (nunca sem limite)");
    Check(dh::fitText("Hierarchy", 10.f, measure).empty(), "nem o ... cabe: nada e desenhado");
    const SU::Rect leaf{{100.f, 50.f}, {600.f, 400.f}};
    const SU::Rect help = dh::helpRectOf(leaf);
    Check(dh::tabsStartX(leaf) > help.position.x + help.size.x, "abas comecam depois do ? (sem sobrepor)");
  }

  // 4. Janela solta volta ao dock, mesmo quando esta por cima da faixa de abas de um grupo.
  {
    struct W {
      wf::Frame frame;
      bool open = true;
    };
    W wa, wb, wc;
    dh::Host h2(text, sdl);
    auto reg = [&](W& win, dh::Kind k, const char* title) {
      dh::Window d;
      d.id = dh::idOf(k);
      d.title = [title] { return std::string(title); };
      d.open = [&win] { return win.open; };
      d.close = [&win] { win.open = false; };
      d.frame = [&win] { return std::optional<SU::Rect>(win.frame.rect()); };
      d.grip = [&win] { return win.frame.gripRect(); };
      d.setDock = [&win](std::optional<SU::Rect> r) { win.frame.placeIn(kWinW, kWinH, r, 400.f, 300.f, 100.f); };
      d.floatAt = [&win](SU::Vec2 at) { win.frame.undock(at); };
      d.frameState = [&win] { return &win.frame; };
      h2.add(d);
    };
    reg(wa, dh::Kind::Hierarchy, "Hierarchy");
    reg(wb, dh::Kind::Console, "Console");
    reg(wc, dh::Kind::Assets, "Assets");
    h2.beginFrame(area, kWinW, kWinH);
    h2.beginFrame(area, kWinW, kWinH);
    constexpr float kHeaderHalf = dh::kHeaderH * 0.5f;
    // Arrasta uma janela solta pelo grip ate (x, y) e solta la.
    auto dragTo = [&](W& win, float x, float y) {
      const SU::Rect g = win.frame.gripRect();
      const float gx = g.position.x + g.size.x * 0.5f, gy = g.position.y + g.size.y * 0.5f;
      (void)h2.onEvent(Button(true, gx, gy));
      (void)h2.onEvent(Motion(x, y, true));
      (void)h2.onEvent(Button(false, x, y));
      h2.beginFrame(area, kWinW, kWinH);
    };
    const SU::UIHandle idA = dh::idOf(dh::Kind::Hierarchy), idB = dh::idOf(dh::Kind::Console), idC = dh::idOf(dh::Kind::Assets);
    Check(h2.model().tabsOf(idA).size() == 3, "tres janelas abertas viram abas de uma folha");
    const auto leaves0 = h2.model().leaves();
    const SU::Rect leaf0 = leaves0.front().bounds;
    {
      const auto cells = h2.tabCells(leaves0.front());
      Check(cells.has_value() && cells->w.size() == 3, "barra de abas com tres abas");
      Check(cells.has_value() && cells->x1 <= leaf0.position.x + leaf0.size.x - dh::kCloseReserve + 0.01f, "abas terminam antes da borda direita da folha");
    }
    {
      const auto cells = h2.tabCells(leaves0.front());
      const float tx = cells->x(1) + dh::kTabGripW * 0.5f, ty = leaf0.position.y + dh::kHeaderH * 0.5f;
      Check(h2.onEvent(Button(true, tx, ty)), "grip da aba consome o clique");
      (void)h2.onEvent(Motion(tx + 40.f, 850.f, true));
      (void)h2.onEvent(Button(false, tx + 40.f, 850.f));
      h2.beginFrame(area, kWinW, kWinH);
      Check(h2.floating(idB) && !h2.model().contains(idB), "aba arrastada para fora do dock vira janela solta");
      Check(h2.model().tabsOf(idA).size() == 2, "as outras duas continuam em abas");
    }
    {
      // Janela solta com o grip exatamente sobre o grip da primeira aba (caso que antes pegava a aba de baixo).
      const auto cells = h2.tabCells(h2.model().leaves().front());
      const SU::Rect leaf = h2.model().leaves().front().bounds;
      const float tx = cells->x(0) + dh::kTabGripW * 0.5f, ty = leaf.position.y + dh::kHeaderH * 0.5f;
      const SU::Rect g = wb.frame.gripRect();
      wb.frame.undock({wb.frame.rect().position.x + (tx - (g.position.x + g.size.x * 0.5f)),
                       wb.frame.rect().position.y + (ty - (g.position.y + g.size.y * 0.5f))});
      h2.beginFrame(area, kWinW, kWinH);
      const SU::Rect g2 = wb.frame.gripRect();
      const float px = g2.position.x + g2.size.x * 0.5f, py = g2.position.y + g2.size.y * 0.5f;
      Check(h2.onEvent(Button(true, px, py)), "grip da janela solta consome o clique");
      const float cx = leaf.position.x + leaf.size.x * 0.5f, cy = leaf.position.y + leaf.size.y * 0.5f;
      (void)h2.onEvent(Motion(cx, cy, true));
      (void)h2.onEvent(Button(false, cx, cy));
      h2.beginFrame(area, kWinW, kWinH);
      Check(h2.model().contains(idB), "janela solta, solta no centro do dock, volta como aba");
      Check(h2.model().tabsOf(idA).size() == 3, "a folha volta a ter tres abas");
      Check(h2.model().contains(idA) && h2.model().contains(idC), "as outras abas continuam no dock");
    }
    {
      // Solta numa borda da janela: nao divide o dock nem junta (so titulo, barra de abas e centro juntam).
      const auto cells = h2.tabCells(h2.model().leaves().front());
      const SU::Rect leaf = h2.model().leaves().front().bounds;
      const SU::UIHandle lastTabId = h2.model().tabsOf(idA).back();
      const float tx = cells->x(2) + dh::kTabGripW * 0.5f, ty = leaf.position.y + dh::kHeaderH * 0.5f;
      Check(h2.onEvent(Button(true, tx, ty)), "grip da terceira aba consome o clique");
      (void)h2.onEvent(Motion(tx + 40.f, 850.f, true));
      (void)h2.onEvent(Button(false, tx + 40.f, 850.f));
      h2.beginFrame(area, kWinW, kWinH);
            Check(h2.floating(lastTabId), "outra aba solta tambem flutua");
      const SU::Rect g = (lastTabId == idC ? wc : wb).frame.gripRect();
      const float px = g.position.x + g.size.x * 0.5f, py = g.position.y + g.size.y * 0.5f;
      const SU::Rect lf = h2.model().leaves().front().bounds;
      const float ex = lf.position.x + 6.f, ey = lf.position.y + lf.size.y * 0.5f;
      (void)h2.onEvent(Button(true, px, py));
      (void)h2.onEvent(Motion(ex, ey, true));
      (void)h2.onEvent(Button(false, ex, ey));
      h2.beginFrame(area, kWinW, kWinH);
      Check(!h2.model().contains(lastTabId) && h2.floating(lastTabId), "janela solta, solta na borda de outra, continua solta");
      Check(h2.model().leaves().size() == 1 && h2.model().tabsOf(idA).size() == 2, "borda nao divide o dock nem junta");

      // Solta no titulo (cabecalho) da janela acoplada: entra como aba.
      W& fw = lastTabId == idC ? wc : wb;
      const SU::Rect lf2 = h2.model().leaves().front().bounds;
      dragTo(fw, lf2.position.x + lf2.size.x * 0.9f, lf2.position.y + kHeaderHalf);
      Check(h2.model().contains(lastTabId) && h2.model().tabsOf(idA).size() == 3, "janela solta, solta no titulo de outra, vira aba");
    }
    {
      // Aba acoplada solta na borda do proprio grupo: nada muda (nao divide).
      const auto cells = h2.tabCells(h2.model().leaves().front());
      const SU::Rect lf = h2.model().leaves().front().bounds;
      const float tx = cells->x(1) + dh::kTabGripW * 0.5f, ty = lf.position.y + kHeaderHalf;
      (void)h2.onEvent(Button(true, tx, ty));
      (void)h2.onEvent(Motion(lf.position.x + 6.f, lf.position.y + lf.size.y * 0.5f, true));
      (void)h2.onEvent(Button(false, lf.position.x + 6.f, lf.position.y + lf.size.y * 0.5f));
      h2.beginFrame(area, kWinW, kWinH);
      Check(h2.model().leaves().size() == 1 && h2.model().tabsOf(idA).size() == 3, "aba acoplada solta na borda: nada muda");
    }
    {
      // A barra de abas continua trocando a aba ativa pelo clique no nome.
      const auto cells = h2.tabCells(h2.model().leaves().front());
      const SU::Rect lf = h2.model().leaves().front().bounds;
      const std::vector<SU::UIHandle> tabs = h2.model().tabsOf(idA);
      const float tx = cells->x(0) + dh::kTabPadX + dh::kTabGripW + 4.f, ty = lf.position.y + kHeaderHalf;
      (void)h2.onEvent(Button(true, tx, ty));
      (void)h2.onEvent(Button(false, tx, ty));
      Check(h2.model().active(tabs[0]), "clicar no nome da primeira aba a ativa");
    }
    {
      // Janela solta no centro de uma janela acoplada com abas: junta ao grupo (sem a regra das bordas).
      const SU::Rect lf = h2.model().leaves().front().bounds;
      const SU::UIHandle lastTabId = h2.model().tabsOf(idA).back();
      // Solta de novo a ultima aba, longe, e devolve no centro.
      const auto cells = h2.tabCells(h2.model().leaves().front());
      const float tx = cells->x(2) + dh::kTabGripW * 0.5f, ty = lf.position.y + kHeaderHalf;
      (void)h2.onEvent(Button(true, tx, ty));
      (void)h2.onEvent(Motion(tx + 40.f, 850.f, true));
      (void)h2.onEvent(Button(false, tx + 40.f, 850.f));
      h2.beginFrame(area, kWinW, kWinH);
      Check(h2.floating(lastTabId), "aba arrastada para fora flutua de novo");
      dragTo(lastTabId == idC ? wc : (lastTabId == idB ? wb : wa), lf.position.x + lf.size.x * 0.5f, lf.position.y + lf.size.y * 0.5f);
      Check(h2.model().contains(lastTabId) && h2.model().tabsOf(idA).size() == 3, "solta no centro: volta ao grupo como aba");
    }
  }

  std::printf("\n%d de %d verificacoes passaram\n", gChecks - gFailures, gChecks);
  return gFailures == 0 ? 0 : 1;
}
