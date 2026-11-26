// Gestos do ponteiro no host do dock (dock_host.hpp): grip arrasta, borda redimensiona, e o gesto se repete.
// Sem janela: o host so usa SDL_Event e geometria; o SDL e carregado so para o destrutor do renderizador.
// Roda no ctest como Editor.DockHost.
#include "dock_host.hpp"

#include <cstdio>

namespace {

int gFailures = 0;
int gChecks = 0;

void Check(bool ok, const char* what) {
  ++gChecks;
  std::printf("%s: %s\n", ok ? "ok" : "FALHOU", what);
  if (!ok) ++gFailures;
}

SDL_Event Button(bool down, float x, float y) {
  SDL_Event e{};
  e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
  e.button.button = SDL_BUTTON_LEFT;
  e.button.x = x;
  e.button.y = y;
  return e;
}

SDL_Event Motion(float x, float y, bool held) {
  SDL_Event e{};
  e.type = SDL_EVENT_MOUSE_MOTION;
  e.motion.x = x;
  e.motion.y = y;
  e.motion.state = held ? SDL_BUTTON_LMASK : 0;
  return e;
}

}  // namespace

int main() {
  namespace dh = editor::dock_host;
  namespace SU = ToolEngine::SpriteUi;
  namespace wf = editor::window_frame;

  constexpr int kWinW = 1600, kWinH = 900;
  editor::sui::TextAtlasSet text;
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

  std::printf("\n%d de %d verificacoes passaram\n", gChecks - gFailures, gChecks);
  return gFailures == 0 ? 0 : 1;
}
