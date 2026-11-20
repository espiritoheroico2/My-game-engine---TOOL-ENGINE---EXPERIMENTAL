// Captura headless da janela "Assets" feita com SpriteUI. Renderiza com o renderizador de software do SDL (sem tela,
// sem GPU), le os pixels e grava um PPM por cenario. PPM -> PNG: scripts/ppm_to_png.py.
//
// Uso: SDL_VIDEODRIVER=offscreen editor_assets_capture <assets/fonts/Xmind-Regular.ttf> <pasta_de_saida>
// Cenarios: 1_icones (pasta com icones), 2_lista (lista, filtro e item selecionado), 3_texto (editor de texto
// aberto), 4_renomear (confirmacao de renomear com o nome digitado).
#include "assets_spriteui.hpp"
#include "spriteui_host.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <string>
#include <vector>

namespace {

namespace SR = ToolEngine::SpriteUi::Rendering;
namespace AM = editor::assets_model;

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

// Desenha um quadro da view no renderizador e grava os pixels. Nao apresenta na tela.
bool Shoot(SDL_Renderer* renderer, editor::sui::SpriteUiSdlRenderer& sdl, editor::assetsui::View& view, int w, int h,
           const std::string& path) {
  editor::assetsui::FrameInput in;
  in.frameId = 1;
  std::vector<editor::assetsui::View::Action> actions;
  const SR::FramePacket packet = view.Frame(static_cast<float>(w), static_cast<float>(h), in, actions);
  SDL_SetRenderDrawColor(renderer, 24, 24, 27, 255);
  SDL_RenderClear(renderer);
  if (!sdl.draw(renderer, packet)) {
    std::fprintf(stderr, "falha ao desenhar: %s\n", SDL_GetError());
    return false;
  }
  SDL_Surface* shot = SDL_RenderReadPixels(renderer, nullptr);
  if (!shot) {
    std::fprintf(stderr, "falha ao ler pixels: %s\n", SDL_GetError());
    return false;
  }
  const bool ok = SavePpm(shot, path);
  SDL_DestroySurface(shot);
  if (ok) std::printf("gravado %s\n", path.c_str());
  return ok;
}

// Itens de exemplo: pastas, modelo, som, cena, texto, imagem e um arquivo de outro tipo.
AM::State SampleState(const std::string& folder) {
  AM::State s;
  s.folder = folder;
  const std::string base = folder + "/";
  auto add = [&](const char* name, bool dir, editor::AssetKind kind, std::uintmax_t size) {
    AM::Item it;
    it.name = name;
    it.rel = base + name;
    it.dir = dir;
    it.kind = kind;
    it.size = size;
    s.items.push_back(it);
  };
  add("Animations", true, editor::AssetKind::Folder, 0);
  add("Models", true, editor::AssetKind::Folder, 0);
  add("Audio", true, editor::AssetKind::Folder, 0);
  add("Cube.fbx", false, editor::AssetKind::Model, 184320);
  add("Jump.wav", false, editor::AssetKind::Audio, 96256);
  add("Main.scene.json", false, editor::AssetKind::Scene, 5120);
  add("notes.txt", false, editor::AssetKind::Text, 812);
  add("hero.png", false, editor::AssetKind::Image, 40960);
  add("readme.md", false, editor::AssetKind::Other, 2048);
  add("Tree.fbx", false, editor::AssetKind::Model, 302080);
  add("Wind.ogg", false, editor::AssetKind::Audio, 55296);
  add("Level2.scene.json", false, editor::AssetKind::Scene, 7168);
  return s;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "uso: editor_assets_capture <fonte.ttf> <pasta_de_saida>\n");
    return 2;
  }
  const std::string fontPath = argv[1];
  const std::string outDir = argv[2];

  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  const int width = 800, height = 720;
  SDL_Window* window = SDL_CreateWindow("Assets", width, height, 0);
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

  bool ok = true;

  // 1. Pasta em icones, sem selecao.
  {
    AM::State s = SampleState("Assets");
    editor::assetsui::View view(s, atlas);
    ok &= Shoot(renderer, sdl, view, width, height, outDir + "/1_icones.ppm");
  }

  // 2. Lista, com filtro "o" e um item selecionado (pasta dentro de Assets/Models).
  {
    AM::State s = SampleState("Assets/Models");
    s.iconView = false;
    s.filter = "o";
    s.selected = "Assets/Models/Cube.fbx";
    editor::assetsui::View view(s, atlas);
    ok &= Shoot(renderer, sdl, view, width, height, outDir + "/2_lista.ppm");
  }

  // 3. Editor de texto aberto no lugar da lista.
  {
    AM::State s = SampleState("Assets");
    s.textOpen = true;
    s.textPath = "Assets/notes.txt";
    s.text = "Lembrar de ajustar a luz do sol.\nTestar o pulo do personagem.\nNivel 2: trocar a trilha.";
    editor::assetsui::View view(s, atlas);
    ok &= Shoot(renderer, sdl, view, width, height, outDir + "/3_texto.ppm");
  }

  // 4. Renomear: campo com o nome digitado e confirmacao.
  {
    AM::State s = SampleState("Assets");
    s.selected = "Assets/Models";
    AM::Begin(s, AM::State::Mode::Rename, "Models_old");
    s.nameFocus = true;
    editor::assetsui::View view(s, atlas);
    ok &= Shoot(renderer, sdl, view, width, height, outDir + "/4_renomear.ppm");
  }

  sdl.destroy();
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return ok ? 0 : 1;
}
