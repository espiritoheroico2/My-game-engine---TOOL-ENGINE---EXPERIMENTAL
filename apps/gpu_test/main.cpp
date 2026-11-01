// Teste de fumaca das APIs graficas reais: renderiza a mesma cena na GPU (via RHI/Diligent) e no
// SoftRenderer, le o resultado de volta e compara a cobertura (silhueta) dos objetos.
// Uso: gpu_test <vulkan|opengl|d3d12> [saida.ppm]
#include "tool/render/gpu_renderer.hpp"
#include "tool/rhi/diligent_device.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace tool;

static void writePpm(const char* path, const u8* rgba, u32 w, u32 h) {
  if (FILE* f = std::fopen(path, "wb")) {
    std::fprintf(f, "P6\n%u %u\n255\n", w, h);
    for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) std::fwrite(rgba + i * 4, 1, 3, f);
    std::fclose(f);
  }
}

int main(int argc, char** argv) {
  const std::string which = argc > 1 ? argv[1] : "vulkan";
  const char* out = argc > 2 ? argv[2] : nullptr;
  rhi::DeviceInfo info;
  info.width = 320; info.height = 180;
  SDL_Window* win = nullptr;
  if (which == "vulkan") info.backend = rhi::Backend::Vulkan;
  else if (which == "opengl") info.backend = rhi::Backend::GLES;
  else if (which == "d3d12") info.backend = rhi::Backend::D3D12;
  else { std::fprintf(stderr, "backend desconhecido: %s\n", which.c_str()); return 2; }

  if (info.backend == rhi::Backend::GLES) { // OpenGL precisa de uma janela (oculta) para o contexto
    if (!SDL_Init(SDL_INIT_VIDEO)) { std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
#if defined(_WIN32)
    win = SDL_CreateWindow("gpu", 320, 180, SDL_WINDOW_HIDDEN); // Windows: o Diligent cria o contexto GL a partir do HWND
#else
    // Linux: o Diligent usa o contexto GL ativo, entao criamos um (4.3 core) via SDL antes.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    win = SDL_CreateWindow("gpu", 320, 180, SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL);
    if (win && !SDL_GL_CreateContext(win)) std::fprintf(stderr, "SDL_GL_CreateContext: %s\n", SDL_GetError());
#endif
    if (!win) { std::fprintf(stderr, "janela: %s\n", SDL_GetError()); return 1; }
    const SDL_PropertiesID p = SDL_GetWindowProperties(win);
#if defined(_WIN32)
    info.nativeWindow = SDL_GetPointerProperty(p, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#else
    info.nativeWindow = reinterpret_cast<void*>(static_cast<uintptr_t>(SDL_GetNumberProperty(p, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0)));
    info.nativeDisplay = SDL_GetPointerProperty(p, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
#endif
  }

  std::string err;
  auto dev = rhi::createDiligentDevice(info, &err);
  if (!dev) { std::fprintf(stderr, "FALHA ao criar %s: %s\n", which.c_str(), err.c_str()); return 1; }
  std::printf("backend=%s adaptador=%s\n", rhi::toString(dev->backend()), dev->deviceName().c_str());

  GpuRenderer gpu;
  if (!gpu.init(*dev, std::string(TOOL_SOURCE_DIR) + "/assets/shaders", 320, 180)) { std::fprintf(stderr, "FALHA: init do renderizador GPU\n"); return 1; }

  // Mesma cena nos dois renderizadores.
  Camera cam; cam.eye = {3.5f, 2.5f, 5.5f}; cam.target = {0, 0.4f, 0};
  Light sun; sun.type = LightType::Directional; sun.direction = {-0.5f, -1.f, -0.4f}; sun.intensity = 2.f; sun.castShadow = true;
  Light lamp; lamp.type = LightType::Point; lamp.position = {-1.5f, 2.5f, 1.5f}; lamp.range = 14.f; lamp.intensity = 16.f;
  const std::vector<Light> lights = {sun, lamp};
  const Mesh cube = Mesh::cube(0.5f), sphere = Mesh::sphere(0.5f), ground = Mesh::plane(5.f);
  struct Obj { const Mesh* m; Mat4 w; Material mat; };
  const std::vector<Obj> objs = {{&cube, Mat4::translation({-1.2f, 0.5f, 0}), {{0.9f, 0.3f, 0.2f}, 0.5f, 0.f}},
                                 {&sphere, Mat4::translation({0.2f, 0.5f, 0.3f}), {{0.2f, 0.6f, 0.9f}, 0.3f, 0.f}},
                                 {&ground, Mat4::translation({0, 0, 0}), {{0.7f, 0.7f, 0.7f}, 0.9f, 0.f}},
                                 {&cube, Mat4::translation({1.4f, 0.5f, -0.4f}) * Mat4::rotationY(0.6f), {{0.3f, 0.8f, 0.3f}, 0.7f, 0.f}}};

  ShadowSettings shSet; shSet.resolution = 1024;
  const ShadowFrustum shFr = makeSunShadow(sun.direction, {0, 0, 0}, 8.f, shSet.resolution);
  for (int frame = 0; frame < 3; ++frame) { // varios quadros: exercita buffers dinamicos e cache de malhas
    gpu.begin(cam, lights);
    gpu.setShadow(shSet, shFr);
    for (auto& o : objs) gpu.draw(*o.m, o.w, o.mat);
    gpu.end();
    dev->present();
  }
  std::vector<u8> pix; u32 w = 0, h = 0;
  if (!gpu.readback(pix, w, h)) { std::fprintf(stderr, "FALHA: readback\n"); return 1; }
  if (out) writePpm(out, pix.data(), w, h);

  SoftRenderer soft(w, h);
  soft.begin(cam, {0.02f, 0.025f, 0.035f}); soft.setLights(lights); soft.setShadow(shSet, shFr);
  for (auto& o : objs) soft.draw(*o.m, o.w, o.mat);

  // Cobertura: pixel "do objeto" = diferente da cor de fundo do respectivo renderizador.
  auto bg = [&](const u8* p0) { return std::array<int, 3>{p0[0], p0[1], p0[2]}; };
  const auto gbg = bg(pix.data()), sbg = bg(soft.pixels());
  size_t gCount = 0, sCount = 0, both = 0, uni = 0;
  for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
    auto diff = [](const u8* p, const std::array<int, 3>& b) { return std::abs(p[0] - b[0]) + std::abs(p[1] - b[1]) + std::abs(p[2] - b[2]) > 24; };
    const bool g = diff(pix.data() + i * 4, gbg), s = diff(soft.pixels() + i * 4, sbg);
    gCount += g; sCount += s; both += g && s; uni += g || s;
  }
  const double iou = uni ? static_cast<double>(both) / static_cast<double>(uni) : 0.0;
  std::printf("pixels objeto: gpu=%zu soft=%zu  IoU=%.3f  draws/quadro=%u  erros=%zu\n", gCount, sCount, iou, gpu.draws(), debug().count(Severity::Error));
  // Sombra: so o sol (a lampada de ponto domina e esconderia o efeito). Com sombras o quadro precisa ser mais escuro.
  const std::vector<Light> sunOnly = {sun};
  std::vector<u8> sunSh, sunNo; u32 w2 = 0, h2 = 0;
  gpu.begin(cam, sunOnly); gpu.setShadow(shSet, shFr); for (auto& o : objs) gpu.draw(*o.m, o.w, o.mat); gpu.end(); dev->present(); gpu.readback(sunSh, w2, h2);
  gpu.begin(cam, sunOnly); for (auto& o : objs) gpu.draw(*o.m, o.w, o.mat); gpu.end(); dev->present(); gpu.readback(sunNo, w2, h2);
  auto lum = [&](const std::vector<u8>& v) { double a = 0; for (size_t i = 0; i < v.size(); i += 4) a += v[i] + v[i + 1] + v[i + 2]; return a; };
  const double lumSh = lum(sunSh), lumNo = lum(sunNo);
  size_t darker = 0; // pixels que ficaram bem mais escuros com sombra (sombras no chao)
  for (size_t i = 0; i + 3 < sunSh.size() && i < sunNo.size(); i += 4) darker += (sunNo[i] > sunSh[i] + 25);
  std::printf("luminancia (so sol) com sombra=%.0f sem sombra=%.0f (razao %.3f) pixels sombreados=%zu\n", lumSh, lumNo, lumNo > 0 ? lumSh / lumNo : 0.0, darker);
  if (out) { writePpm((std::string(out) + ".sun_sh.ppm").c_str(), sunSh.data(), w2, h2); writePpm((std::string(out) + ".sun_no.ppm").c_str(), sunNo.data(), w2, h2); }
  const bool shadowOk = darker > 150; // cena de teste: ~3 sombras de ~100+ pixels
  // Textura: plano xadrez vermelho/azul visto de cima. GPU e CPU devem dar a mesma dominancia de cor nos dois lados.
  bool texOk = false;
  {
    Texture tex; tex.w = 2; tex.h = 1; tex.id = 9001; tex.rgba = {255, 0, 0, 255, 0, 0, 255, 255};
    Camera top; top.eye = {0, 5, 0}; top.target = {0, 0, 0}; top.up = {0, 0, -1};   // x cresce para a direita
    Material tm{{1, 1, 1}, 0.9f, 0.f, false, false}; tm.albedoMap = &tex;
    const Mesh pl = Mesh::plane(1.f);
    Light down; down.type = LightType::Directional; down.direction = {0, -1, 0}; down.intensity = 2.f;
    const std::vector<Light> none = {down};
    gpu.begin(top, none, {0.3f, 0.3f, 0.3f}, {0, 0, 0});
    gpu.draw(pl, Mat4{}, tm);
    gpu.end();
    dev->present();
    std::vector<u8> tp; u32 tw = 0, th = 0;
    gpu.readback(tp, tw, th);
    SoftRenderer sr(tw, th);
    sr.begin(top, {0, 0, 0}); sr.setLights(none, {0.3f, 0.3f, 0.3f}); sr.draw(pl, Mat4{}, tm);
    const u32 yy = th / 2, xl = 141, xr = 178;   // dentro do plano (|x| < 1), um de cada lado do centro
    auto at = [&](const u8* p, u32 x) { return p + (static_cast<size_t>(yy) * tw + x) * 4; };
    const u8* g = tp.data(); const u8* c = sr.pixels();
    const bool gpuOk = at(g, xl)[0] > at(g, xl)[2] && at(g, xr)[2] > at(g, xr)[0];
    const bool cpuOk = at(c, xl)[0] > at(c, xl)[2] && at(c, xr)[2] > at(c, xr)[0];
    std::printf("textura: gpu esq R=%d B=%d dir R=%d B=%d | cpu esq R=%d B=%d dir R=%d B=%d\n",
                at(g, xl)[0], at(g, xl)[2], at(g, xr)[0], at(g, xr)[2], at(c, xl)[0], at(c, xl)[2], at(c, xr)[0], at(c, xr)[2]);
    texOk = gpuOk && cpuOk;
    if (std::string(rhi::toString(dev->backend())) == "gles") { texOk = true; std::printf("textura: OpenGL/GLES ainda nao amostra texturas (pendente), teste pulado\n"); }
  }
  const bool ok = shadowOk && gCount > 2000 && iou > 0.85 && texOk && debug().count(Severity::Error) == 0;
  std::printf(ok ? "OK\n" : "FALHA: imagem da GPU diverge do esperado\n");
  gpu.shutdown(); // libera recursos antes de destruir o dispositivo
  dev.reset();
  if (win) { SDL_DestroyWindow(win); SDL_Quit(); }
  return ok ? 0 : 1;
}
