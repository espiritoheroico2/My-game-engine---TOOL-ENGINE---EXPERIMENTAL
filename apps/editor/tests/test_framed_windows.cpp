// Input Map, Export, Help e Test map na moldura padrao e no dock: registro no host, cabecalho de 30 px, grip,
// borda que redimensiona, "?" e X. Sem janela: so geometria e SDL_Event; o SDL carrega so para o renderizador.
// Roda no ctest como Editor.FramedWindows.
#include "dock_host.hpp"
#include "export_panel.hpp"
#include "help_panel.hpp"
#include "input_map_panel.hpp"
#include "test_map_panel.hpp"

#include <cstdio>
#include <set>

namespace {

int gFailures = 0;  // Quantidade de verificacoes que falharam.

// Imprime ok/FALHOU para a verificacao e conta as falhas.
void Check(bool ok, const char* what) {
  std::printf("%s: %s\n", ok ? "ok" : "FALHOU", what);
  if (!ok) ++gFailures;
}

// Clique esquerdo sintetico no ponto dado.
SDL_Event Click(ToolEngine::SpriteUi::Vec2 p) {
  SDL_Event e{};
  e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
  e.button.button = SDL_BUTTON_LEFT;
  e.button.x = p.x;
  e.button.y = p.y;
  return e;
}

// Centro de um retangulo.
ToolEngine::SpriteUi::Vec2 Center(ToolEngine::SpriteUi::Rect r) {
  return {r.position.x + r.size.x * 0.5f, r.position.y + r.size.y * 0.5f};
}

// Verifica uma janela: a moldura padrao (cabecalho, grip, "?", X, borda) e o registro no host do dock.
void CheckWindow(const char* name, editor::dock_host::Host& host, editor::dock_host::Kind kind, editor::framed::Chrome& chrome,
                 bool& open, float w, float h) {
  namespace dh = editor::dock_host;
  namespace SU = ToolEngine::SpriteUi;
  namespace SW = ToolEngine::SpriteUi::Widgets;
  constexpr int kWinW = 1600, kWinH = 900;
  const std::string n = name;

  dh::Window win;
  win.id = dh::idOf(kind);
  win.title = [n] { return n; };
  win.open = [&open] { return open; };
  win.close = [&open] { open = false; };
  win.frame = [&chrome]() -> std::optional<SU::Rect> { return chrome.frameRect(); };
  win.grip = [&chrome] { return chrome.gripRect(); };
  win.setDock = [&chrome](std::optional<SU::Rect> d) { chrome.setDock(d); };
  win.floatAt = [&chrome](SU::Vec2 at) { chrome.floatAt(at); };
  win.frameState = [&chrome] { return chrome.frameState(); };
  host.add(win);

  const SU::Rect area{{0.f, 0.f}, {static_cast<float>(kWinW), static_cast<float>(kWinH)}};
  host.beginFrame(area, kWinW, kWinH);
  const editor::sui::Box body = chrome.prepare(kWinW, kWinH, w, h);   // o painel faz isto em draw()
  host.beginFrame(area, kWinW, kWinH);

  Check(body.w > 0.f && body.h > 0.f, (n + ": corpo existe abaixo do cabecalho").c_str());
  Check(chrome.visible() && chrome.frameState() != nullptr, (n + ": tem moldura (chrome) visivel").c_str());
  Check(chrome.frameState()->header().size.y == 30.f, (n + ": cabecalho padrao de 30 px").c_str());
  Check(body.y >= chrome.frameRect().position.y + 30.f, (n + ": corpo comeca abaixo do cabecalho").c_str());
  Check(chrome.gripRect().size.x > 0.f && chrome.gripRect().size.y > 0.f, (n + ": grip existe").c_str());
  Check(host.visible(dh::idOf(kind), open), (n + ": registrada e visivel no host").c_str());

  const SDL_Event help = Click(Center(chrome.frameState()->helpRect()));
  Check(chrome.handleEvent(help) && chrome.consumeHelp(), (n + ": \"?\" pede a ajuda").c_str());

  const SU::Rect close = SW::WindowFrameButtonRect(chrome.frameRect(), chrome.frameState()->metrics(), false, SW::WindowFrameButton::Close);
  Check(chrome.handleEvent(Click(Center(close))) && chrome.consumeClose(), (n + ": X pede fechar").c_str());

  chrome.floatAt({200.f, 100.f});   // flutua como o host faz ao soltar o grip
  chrome.prepare(kWinW, kWinH, w, h);
  Check(!chrome.docked(), (n + ": flutua depois de floatAt").c_str());
  const SU::Rect fr = chrome.frameRect();
  Check(chrome.frameState()->beginResize({fr.position.x + fr.size.x - 1.f, fr.position.y + fr.size.y * 0.5f}), (n + ": borda direita redimensiona").c_str());
  chrome.frameState()->pointerMove({fr.position.x + fr.size.x + 60.f, fr.position.y + fr.size.y * 0.5f});
  chrome.frameState()->pointerUp();
  Check(chrome.frameRect().size.x > fr.size.x, (n + ": a largura cresce ao arrastar a borda").c_str());
}

}  // namespace

// Roda os testes das quatro janelas na moldura padrao.
int main() {
  namespace dh = editor::dock_host;
  using Kind = dh::Kind;

  // Ids do dock: unicos entre si e em relacao a todas as janelas.
  std::set<std::uint32_t> ids;
  for (Kind k : {Kind::Hierarchy, Kind::Scene, Kind::Game, Kind::Scenes, Kind::Assets, Kind::Console, Kind::Profiler, Kind::Inspector,
                 Kind::Settings, Kind::Coding, Kind::Narrative, Kind::InputMap, Kind::Export, Kind::Help, Kind::TestMap}) {
    ids.insert(dh::idOf(k).index);
  }
  Check(ids.size() == 15, "ids do dock sao unicos (inclui Input Map, Export, Help e Test map)");

  // Altura da moldura para um corpo: cabecalho padrao mais a folga das bordas.
  Check(editor::framed::frameHeightFor(100.f) == 100.f + editor::framed::kHeaderH + editor::framed::kBodySlack, "frameHeightFor soma cabecalho e folga");

  editor::sui::TextAtlasSet text;
  text.load("", 1.f);
  editor::sui::SpriteUiSdlRenderer sdl;
  dh::Host host(text, sdl);

  editor::inputui::Panel input(text, sdl);
  editor::exportui::Panel exportPanel(text, sdl);
  editor::help::HelpPanel help(text, sdl);
  editor::testmap::TestMapPanel testMap(text, sdl);
  bool inputOpen = true, exportOpen = true, helpOpen = true, testMapOpen = true;

  CheckWindow("Input Map", host, Kind::InputMap, input.chrome(), inputOpen, editor::inputui::kWidth, editor::inputui::kHeight);
  CheckWindow("Export game", host, Kind::Export, exportPanel.chrome(), exportOpen, editor::exportui::kWidth, 500.f);
  CheckWindow("Help", host, Kind::Help, help.chrome(), helpOpen, editor::help::kStartWidth, editor::help::kStartHeight);
  CheckWindow("Test map", host, Kind::TestMap, testMap.chrome(), testMapOpen, editor::testmap::kWidth, 380.f);

  std::printf("%s (%d falhas)\n", gFailures == 0 ? "TODOS OS TESTES PASSARAM" : "HOUVE FALHAS", gFailures);
  return gFailures == 0 ? 0 : 1;
}
