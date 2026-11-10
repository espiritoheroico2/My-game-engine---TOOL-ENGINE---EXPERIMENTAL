// Teste com GPU do backend SpriteUI (OpenGL 4.3 core via Diligent). Desenha num alvo offscreen de 128x64,
// le os pixels de volta e confere: cor solida, recorte, blend premultiplicado e amostragem do atlas com
// cobertura parcial. No Linux rode com xvfb-run (o Diligent usa o contexto GL criado pelo SDL).
// Uso: spriteui_gpu_test [opengl|vulkan] [saida.ppm]   (padrao: opengl)
#include "tool/rhi/diligent_device.hpp"
#include "SpriteUi/Adapters/ToolEngineRhi.hpp"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#ifndef TOOL_SOURCE_DIR
#error "TOOL_SOURCE_DIR deve ser definido pelo CMake"
#endif

using namespace tool;
namespace R = ToolEngine::SpriteUi::Rendering;

namespace {

int failures = 0;
constexpr u32 kW = 128, kH = 64;

// Confere um pixel contra a cor esperada (0..255) com tolerancia por canal.
void expectPixel(const char* what, const std::vector<u8>& img, u32 x, u32 y, int r, int g, int b, int tol = 4) {
  const std::size_t i = (static_cast<std::size_t>(y) * kW + x) * 4;
  const int got[3] = {img[i], img[i + 1], img[i + 2]};
  const int want[3] = {r, g, b};
  bool ok = true;
  for (int k = 0; k < 3; ++k) ok = ok && std::abs(got[k] - want[k]) <= tol;
  if (!ok) {
    ++failures;
    std::printf("FALHA %s em (%u,%u): obtido (%d,%d,%d), esperado (%d,%d,%d)\n", what, x, y,
                got[0], got[1], got[2], want[0], want[1], want[2]);
  } else {
    std::printf("ok    %s em (%u,%u) = (%d,%d,%d)\n", what, x, y, got[0], got[1], got[2]);
  }
}

// Retangulo de `w` x `h` px na posicao (x, y), com UV dado (u, v, larguraUV, alturaUV) e tint premultiplicada.
R::SpriteInstanceGPU Quad(float x, float y, float w, float h, std::uint32_t slice, std::uint32_t clip,
                          const float uv[4], const float tint[4]) {
  R::SpriteInstanceGPU inst{};
  inst.transformRow0[0] = w; inst.transformRow0[1] = 0.0f; inst.transformRow0[2] = x;
  inst.transformRow1[0] = 0.0f; inst.transformRow1[1] = h; inst.transformRow1[2] = y;
  for (int k = 0; k < 4; ++k) { inst.uvRect[k] = uv[k]; inst.tint[k] = tint[k]; }
  inst.resourceIds[0] = slice;
  inst.resourceIds[2] = clip;
  inst.resourceIds[3] = 0;
  return inst;
}

R::DrawBatch BatchFor(std::uint32_t first, std::uint32_t slice, std::uint32_t clip) {
  return R::DrawBatch{first, 1, slice, 0, clip, 0};
}

} // namespace

int main(int argc, char** argv) {
  // Backend: opengl (padrao, com janela oculta via SDL/X11) ou vulkan (offscreen).
  const bool vulkan = argc > 1 && std::string(argv[1]) == "vulkan";
  const int outArg = (argc > 1 && (std::string(argv[1]) == "vulkan" || std::string(argv[1]) == "opengl")) ? 2 : 1;

  rhi::DeviceInfo info;
  info.width = kW; info.height = kH;
  SDL_Window* win = nullptr;
  if (!vulkan) {
    if (!SDL_Init(SDL_INIT_VIDEO)) { std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    win = SDL_CreateWindow("spriteui", 320, 180, SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL);
    if (!win) { std::fprintf(stderr, "janela: %s\n", SDL_GetError()); return 1; }
    if (!SDL_GL_CreateContext(win)) std::fprintf(stderr, "SDL_GL_CreateContext: %s\n", SDL_GetError());
    const SDL_PropertiesID props = SDL_GetWindowProperties(win);
    info.backend = rhi::Backend::GLES;
    info.nativeWindow = reinterpret_cast<void*>(static_cast<uintptr_t>(SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0)));
    info.nativeDisplay = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
  } else {
    info.backend = rhi::Backend::Vulkan;   // offscreen: sem janela
  }

  std::string err;
  auto dev = rhi::createDiligentDevice(info, &err);
  if (!dev) { std::fprintf(stderr, "FALHA ao criar o dispositivo: %s\n", err.c_str()); return 1; }
  std::printf("backend=%s adaptador=%s\n", rhi::toString(dev->backend()), dev->deviceName().c_str());

  // Alvo offscreen RGBA8 (sem sRGB, para comparar valores diretos).
  const rhi::TextureId target = dev->createTexture({kW, kH, 1, rhi::Format::RGBA8, true}, nullptr);
  if (target.isNull()) { std::fprintf(stderr, "FALHA: alvo offscreen\n"); return 1; }

  // Limpa para cinza 0.1 (26 em 8 bits) antes de desenhar a UI.
  {
    auto cl = dev->beginCommands();
    cl->begin(target, rhi::TextureId{}, 0.1f, 0.1f, 0.1f, 1.0f);
    cl->end();
    dev->submit(*cl);
  }

  // Atlas 8x8: (0,0) branco reservado; (2,2) com cobertura 255; (4,4) com cobertura 128.
  std::vector<std::uint8_t> atlas(8 * 8, 0);
  atlas[2 * 8 + 2] = 255;
  atlas[4 * 8 + 4] = 128;

  ToolEngine::SpriteUi::Adapters::SpriteUiRhiBackend backend(*dev, std::string(TOOL_SOURCE_DIR) + "/assets/shaders", rhi::Format::RGBA8);
  if (!backend.init()) { std::fprintf(stderr, "FALHA init: %s\n", backend.lastError().c_str()); return 1; }
  if (!backend.uploadAtlas(atlas.data(), 8, 8)) { std::fprintf(stderr, "FALHA atlas: %s\n", backend.lastError().c_str()); return 1; }
  if (!backend.setTarget(target, kW, kH)) { std::fprintf(stderr, "FALHA alvo: %s\n", backend.lastError().c_str()); return 1; }

  const float whiteUv[4] = {0.0f, 0.0f, 1.0f, 1.0f};
  const float red[4] = {1, 0, 0, 1}, green[4] = {0, 1, 0, 1}, blendTint[4] = {0, 0, 0.5f, 0.5f}, white[4] = {1, 1, 1, 1};
  const float glyphFull[4] = {2.0f / 8, 2.0f / 8, 1.0f / 8, 1.0f / 8};
  const float glyphHalf[4] = {4.0f / 8, 4.0f / 8, 1.0f / 8, 1.0f / 8};

  R::FramePacket packet;
  packet.frameId = 1;
  packet.clipRects = {R::ClipRectGPU{60.0f, 0.0f, 70.0f, 10.0f}};
  packet.instances = {
    Quad(10, 10, 40, 20, R::WhiteTextureSlice, R::NoClipIndex, whiteUv, red),     // 0: vermelho solido
    Quad(60, 0, 40, 20, R::WhiteTextureSlice, 0, whiteUv, green),                 // 1: verde recortado (y < 10)
    Quad(5, 35, 20, 10, R::WhiteTextureSlice, R::NoClipIndex, whiteUv, blendTint),// 2: blend sobre o cinza
    Quad(100, 0, 10, 10, 1, R::NoClipIndex, glyphFull, white),                    // 3: glifo cobertura 255
    Quad(120, 0, 10, 10, 1, R::NoClipIndex, glyphHalf, white),                    // 4: glifo cobertura 128
  };
  packet.batches = {BatchFor(0, R::WhiteTextureSlice, R::NoClipIndex), BatchFor(1, R::WhiteTextureSlice, 0),
                    BatchFor(2, R::WhiteTextureSlice, R::NoClipIndex), BatchFor(3, 1, R::NoClipIndex),
                    BatchFor(4, 1, R::NoClipIndex)};

  // Dois quadros, cada um sobre um alvo limpo (o blend acumula se o alvo nao for limpo): exercita a
  // reutilizacao de buffers e o caminho de atualizacao.
  for (int frame = 0; frame < 2; ++frame) {
    auto cl = dev->beginCommands();
    cl->begin(target, rhi::TextureId{}, 0.1f, 0.1f, 0.1f, 1.0f);
    cl->end();
    dev->submit(*cl);
    if (!backend.Submit(packet)) { std::fprintf(stderr, "FALHA Submit: %s\n", backend.lastError().c_str()); return 1; }
  }

  std::vector<u8> img;
  u32 w = 0, h = 0;
  if (!dev->readTexture(target, img, w, h) || w != kW || h != kH) { std::fprintf(stderr, "FALHA: leitura do alvo\n"); return 1; }

  expectPixel("vermelho", img, 20, 15, 255, 0, 0);
  expectPixel("verde recortado", img, 65, 5, 0, 255, 0);
  expectPixel("recorte corta abaixo de y=10", img, 65, 15, 26, 26, 26);
  expectPixel("blend premultiplicado", img, 10, 40, 13, 13, 140);
  expectPixel("glifo cobertura 255", img, 105, 5, 255, 255, 255);
  expectPixel("fora do quad", img, 115, 5, 26, 26, 26);
  expectPixel("glifo cobertura 128", img, 125, 5, 141, 141, 141);
  expectPixel("fundo", img, 0, 0, 26, 26, 26);

  if (argc > outArg) {
    if (std::FILE* f = std::fopen(argv[outArg], "wb")) {
      std::fprintf(f, "P6\n%u %u\n255\n", kW, kH);
      for (std::size_t i = 0; i < static_cast<std::size_t>(kW) * kH; ++i) std::fwrite(img.data() + i * 4, 1, 3, f);
      std::fclose(f);
    }
  }

  const auto& s = backend.stats();
  std::printf("frames=%llu quads=%llu draws=%llu clippedRuns=%llu\n", static_cast<unsigned long long>(s.frames),
              static_cast<unsigned long long>(s.quads), static_cast<unsigned long long>(s.draws),
              static_cast<unsigned long long>(s.clippedRuns));
  std::printf(failures == 0 ? "spriteui gpu: todos os pixels conferem\n" : "spriteui gpu: %d falha(s)\n", failures);
  if (win) SDL_DestroyWindow(win);
  if (win) SDL_Quit();
  return failures == 0 ? 0 : 1;
}
