// Captura headless da janela "Narrative Controller" (SpriteUI). Renderiza com o renderizador de software do SDL
// (sem tela, sem GPU), le os pixels e grava um PPM. Nao depende de clique: mostra um grafo de exemplo.
//
// Uso: SDL_VIDEODRIVER=offscreen editor_narrative_capture <assets/fonts/Xmind-Regular.ttf> <arquivo_de_saida.ppm>
#include "narrative_panel.hpp"
#include "spriteui_host.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <string>

namespace {

// Grava o pixel RGB do SDL_Surface em P6 (PPM binario). Sem zlib e sem dependencias extras.
bool SavePpm(SDL_Surface* surface, const std::string& path) {
  SDL_Surface* rgb = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGB24);
  if (!rgb) return false;
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) {
    SDL_DestroySurface(rgb);
    return false;
  }
  std::fprintf(f, "P6\n%d %d\n255\n", rgb->w, rgb->h);
  const auto* base = static_cast<const unsigned char*>(rgb->pixels);
  const std::size_t row = static_cast<std::size_t>(rgb->w) * 3;
  for (int y = 0; y < rgb->h; ++y) {
    std::fwrite(base + static_cast<std::size_t>(y) * static_cast<std::size_t>(rgb->pitch), 1, row, f);
  }
  std::fclose(f);
  SDL_DestroySurface(rgb);
  return true;
}

// Grafo de exemplo: inicio da cena, uma variavel "score" e um teste de ramificacao.
void buildSample(editor::NarrativeModel& m) {
  m.setScene("Fase 1");
  m.addVariable("score");
  m.setVariable("score", 10);
  m.addNode("start", 20, 150);
  const int set = m.addNode("set", 220, 150);
  m.addNode("compare", 20, 300);
  m.addNode("branch", 220, 300);
  m.setNodeString(static_cast<std::size_t>(set), "var", "score");
  m.setNodeNumber(static_cast<std::size_t>(set), "value", 100);
  m.addLink("scene", "out", "start1");
  m.addLink("start1", "out", "set1");
  m.addLink("set1", "out", "branch1");
}

} // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "uso: editor_narrative_capture <fonte.ttf> <saida.ppm>\n");
    return 2;
  }
  const std::string fontPath = argv[1];
  const std::string outPath = argv[2];

  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  const int width = 1000, height = 640;
  SDL_Window* window = SDL_CreateWindow("Narrative", width, height, 0);
  SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, "software") : nullptr;
  if (!renderer) {
    std::fprintf(stderr, "janela/renderizador: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }

  editor::sui::TextAtlasSet atlas;   // um atlas de glifos por tamanho de texto
  if (!atlas.load(fontPath, 1.f)) {
    std::fprintf(stderr, "fonte nao carregou: %s\n", fontPath.c_str());
    return 1;
  }
  editor::sui::SpriteUiSdlRenderer sdl;
  sdl.setTextSet(&atlas);

  editor::NarrativeModel model;
  buildSample(model);
  editor::NarrativePanel panel(atlas, sdl, model);

  // Quadro e leitura dos pixels. Cenario 2: clique no titulo de "set1" (pelo caminho de eventos) e novo quadro.
  auto shoot = [&](const std::string& path) {
    SDL_SetRenderDrawColor(renderer, 24, 24, 27, 255);
    SDL_RenderClear(renderer);
    if (!panel.draw(renderer, static_cast<float>(width), static_cast<float>(height), true)) return false;
    SDL_Surface* shot = SDL_RenderReadPixels(renderer, nullptr);
    if (!shot) return false;
    const bool ok = SavePpm(shot, path);
    SDL_DestroySurface(shot);
    if (ok) std::printf("gravado %s\n", path.c_str());
    return ok;
  };
  bool ok = shoot(outPath);
  auto click = [&](float x, float y) {
    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    e.button.button = SDL_BUTTON_LEFT;
    e.button.x = x;
    e.button.y = y;
    panel.handleEvent(e);
    e.type = SDL_EVENT_MOUSE_BUTTON_UP;
    panel.handleEvent(e);
  };
  click(540.f, 296.f);   // titulo de set1 (coordenadas da janela)
  ok = shoot(outPath + ".selecionado.ppm") && ok;

  sdl.destroy();
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return ok ? 0 : 1;
}
