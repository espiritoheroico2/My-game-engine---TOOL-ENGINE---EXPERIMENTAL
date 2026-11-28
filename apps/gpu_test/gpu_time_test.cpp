// Teste do tempo de GPU por zona (timestamps do RHI).
// Uso: gpu_time_test [vulkan|null] [bench]
//   null   : o NullDevice devolve 0 em todas as zonas e gpuTimersEnabled() e falso.
//   vulkan : liga a flag, desenha varios quadros de uma cena com sombras e confere zonas > 0 (opaque e shadow).
//   bench  : (so vulkan) mede o tempo de CPU por quadro com a flag desligada e ligada (3 rodadas de cada).
#include "tool/render/gpu_renderer.hpp"
#include "tool/rhi/diligent_device.hpp"
#include "tool/rhi/null_device.hpp"

#include "LightPrism/Backends/DiligentDevice.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

using namespace tool;

namespace {
int failures = 0;
// Registra uma verificacao; imprime e conta falhas.
void check(bool ok, const char* what) { std::printf("%s %s\n", ok ? "[ok]  " : "[FALHA]", what); if (!ok) ++failures; }
}

int main(int argc, char** argv) {
  const std::string which = argc > 1 ? argv[1] : "vulkan";
  const bool bench = argc > 2 && std::strcmp(argv[2], "bench") == 0;

  if (which == "null") {
    auto dev = rhi::createNullDevice();
    dev->setGpuTimers(true);
    auto cl = dev->beginCommands();
    dev->beginFrame();
    cl->beginGpuZone("opaque"); cl->endGpuZone();
    dev->endFrame();
    check(!dev->gpuTimersSupported() && !dev->gpuTimersEnabled(), "null: sem suporte e desligado");
    check(dev->gpuZoneMs("opaque") == 0.0 && dev->gpuZones().empty(), "null: zonas devolvem 0");
    return failures ? 1 : 0;
  }

  rhi::DeviceInfo info;
  info.backend = rhi::Backend::Vulkan; info.width = 640; info.height = 360;
  info.gpuTimers = true;
  std::string err;
  auto dev = rhi::createDiligentDevice(info, &err);
  if (!dev) { std::fprintf(stderr, "FALHA ao criar Vulkan: %s\n", err.c_str()); return 1; }
  std::printf("backend=%s adaptador=%s timestamps=%d\n", rhi::toString(dev->backend()), dev->deviceName().c_str(), dev->gpuTimersSupported() ? 1 : 0);

  GpuRenderer gpu;
  if (!gpu.init(*dev, rhi::createLightPrismDevice(*dev), std::string(TOOL_SOURCE_DIR) + "/assets/shaders", 640, 360)) { std::fprintf(stderr, "FALHA: init do renderizador\n"); return 1; }

  LightRig rig;
  Camera cam; cam.eye = {3.5f, 2.5f, 5.5f}; cam.target = {0, 0.4f, 0};
  Light sun; sun.type = LightType::Directional; sun.direction = {-0.5f, -1.f, -0.4f}; sun.intensity = 2.f; sun.castShadow = true;
  Light lamp; lamp.type = LightType::Point; lamp.position = {-1.5f, 2.5f, 1.5f}; lamp.range = 14.f; lamp.intensity = 16.f;
  const std::vector<Light> lights = {sun, lamp};
  const Mesh cube = Mesh::cube(0.5f), sphere = Mesh::sphere(0.5f), ground = Mesh::plane(5.f);
  struct Obj { const Mesh* m; Mat4 w; Material mat; };
  std::vector<Obj> objs = {{&sphere, Mat4::translation({0.2f, 0.5f, 0.3f}), {{0.2f, 0.6f, 0.9f}, 0.3f, 0.f}},
                           {&ground, Mat4::translation({0, 0, 0}), {{0.7f, 0.7f, 0.7f}, 0.9f, 0.f}}};
  for (int i = 0; i < 400; ++i) objs.push_back({&cube, Mat4::translation({-4.f + 0.4f * static_cast<f32>(i % 20), 0.5f, -3.f + 0.3f * static_cast<f32>(i / 20)}), {{0.9f, 0.3f, 0.2f}, 0.5f, 0.f}});

  auto frame = [&]() -> bool {
    const Mat4 v = Mat4::lookAt(cam.eye, cam.target, cam.up), p = cam.projection(640.f, 360.f);
    rig.build(lights, {0.03f, 0.03f, 0.04f}, cam.eye, v, p.m[0][0], p.m[1][1], cam.nearZ, cam.farZ);
    dev->beginFrame();
    gpu.begin(cam, {0.02f, 0.025f, 0.035f});
    if (!gpu.setLighting(rig, v, p)) return false;
    for (auto& o : objs) gpu.draw(*o.m, o.w, o.mat);
    gpu.end();
    dev->endFrame();
    return true;
  };

  if (!bench) {
    check(dev->gpuTimersSupported() && dev->gpuTimersEnabled(), "vulkan: timestamps suportados e ligados");
    std::map<std::string, double> sum; int n = 0;
    for (int f = 0; f < 40; ++f) {
      if (!frame()) { std::fprintf(stderr, "FALHA: luz da GPU: %s\n", gpu.lastError().c_str()); return 1; }
      if (f >= 10) { for (const auto& z : dev->gpuZones()) sum[z.first] += z.second; ++n; }
    }
    for (const auto& s : sum) std::printf("zona %-12s media %.4f ms (%d quadros)\n", s.first.c_str(), s.second / n, n);
    check(dev->gpuZoneMs("opaque") > 0.0, "vulkan: zona 'opaque' > 0");
    check(dev->gpuZoneMs("shadow") > 0.0, "vulkan: zona 'shadow' > 0");
    check(dev->gpuZoneMs("inexistente") == 0.0, "zona inexistente devolve 0");
    dev->setGpuTimers(false);
    frame(); frame();
    check(!dev->gpuTimersEnabled(), "flag desligada em tempo de execucao");
    return failures ? 1 : 0;
  }

  // ---- bench: tempo de CPU por quadro (inclui a espera da GPU quando o anel enche), flag desligada x ligada ----
  const int kWarm = 30, kFrames = 300;
  for (int round = 0; round < 3; ++round) {
    for (int on = 0; on <= 1; ++on) {
      dev->setGpuTimers(on == 1);
      for (int f = 0; f < kWarm; ++f) frame();
      std::vector<u8> px; u32 w = 0, h = 0; gpu.readback(px, w, h); // esvazia a GPU antes de medir
      const auto t0 = std::chrono::steady_clock::now();
      for (int f = 0; f < kFrames; ++f) frame();
      gpu.readback(px, w, h); // espera a GPU terminar: mede CPU + GPU de verdade
      const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / kFrames;
      std::printf("rodada %d flag=%s: %.4f ms/quadro", round + 1, on ? "ligada " : "desligada", ms);
      if (on) { std::printf("  |"); for (const auto& z : dev->gpuZones()) std::printf(" %s %.4f", z.first.c_str(), z.second); }
      std::printf("\n");
    }
  }
  return 0;
}
