// Captura headless da janela "Editor info" feita com SpriteUI. Renderiza com o renderizador de software do SDL
// (sem tela de verdade, sem GPU), le os pixels e grava um PPM por cenario. PPM -> PNG: scripts/ppm_to_png.py.
//
// Uso: SDL_VIDEODRIVER=offscreen editor_info_capture <assets/fonts/Xmind-Regular.ttf> <pasta_de_saida>
// Cenarios: 1_vazio (campos faltando), 2_editando (campo com selecao e caret), 3_salvo (depois de gravar).
#include "editor_info_spriteui.hpp"
#include "project_settings.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <string>
#include <vector>

namespace {

namespace SR = ToolEngine::SpriteUi::Rendering;
using editor::info::Field;

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
bool Shoot(SDL_Renderer* renderer, editor::sui::SpriteUiSdlRenderer& sdl, editor::infoui::View& view, int w, int h,
           const std::string& path) {
  editor::infoui::FrameInput in;
  in.frameId = 1;
  std::vector<editor::infoui::View::Action> actions;
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

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "uso: editor_info_capture <fonte.ttf> <pasta_de_saida>\n");
    return 2;
  }
  const std::string fontPath = argv[1];
  const std::string outDir = argv[2];

  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  const int width = 760, height = 720;
  SDL_Window* window = SDL_CreateWindow("Editor info", width, height, 0);
  SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, "software") : nullptr;
  if (!renderer) {
    std::fprintf(stderr, "janela/renderizador: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }

  ToolEngine::SpriteUi::Text::TruetypeAtlas atlas;
  if (!editor::sui::LoadTextAtlas(fontPath, atlas, 1.f)) {
    std::fprintf(stderr, "fonte nao carregou: %s\n", fontPath.c_str());
    return 1;
  }
  editor::sui::SpriteUiSdlRenderer sdl;
  if (!sdl.uploadAtlas(renderer, atlas)) {
    std::fprintf(stderr, "atlas nao subiu para o renderizador\n");
    return 1;
  }

  bool ok = true;

  // 1. Projeto novo: so a versao preenchida; o resto aparece como faltando.
  {
    editor::ProjectSettings settings;
    settings.projectName.clear();
    settings.author.clear();
    settings.info.version = "1.0.0.0";
    editor::info::Model model(settings);
    editor::infoui::View view(model, atlas);
    ok &= Shoot(renderer, sdl, view, width, height, outDir + "/1_vazio.ppm");
  }

  // 2. Preenchido, com um campo em edicao (texto selecionado e caret visivel).
  {
    editor::ProjectSettings settings;
    settings.projectName = "Meu Jogo";
    settings.author = "João Vitor";
    settings.info.studio = "Estúdio Exemplo";
    settings.info.country = "Brasil";
    settings.info.version = "1.0.0.0";
    settings.info.description = "Um jogo de teste para a janela Editor info.";
    editor::info::Model model(settings);
    model.Focus(Field::Studio);
    model.SelectAll();
    editor::infoui::View view(model, atlas);
    ok &= Shoot(renderer, sdl, view, width, height, outDir + "/2_editando.ppm");
  }

  // 3. Depois de "Save info" bem-sucedido: linha de status verde e sem faltantes.
  {
    editor::ProjectSettings settings;
    settings.projectName = "Meu Jogo";
    settings.author = "João Vitor";
    settings.info.studio = "Estúdio Exemplo";
    settings.info.country = "Brasil";
    settings.info.version = "1.0.0.0";
    settings.info.description = "Um jogo de teste para a janela Editor info.";
    editor::info::Model model(settings);
    model.setSaveResult(true, "Saved.");
    editor::infoui::View view(model, atlas);
    ok &= Shoot(renderer, sdl, view, width, height, outDir + "/3_salvo.ppm");
  }

  sdl.destroy();
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return ok ? 0 : 1;
}
