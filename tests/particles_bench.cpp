// Benchmark do nucleo CPU de particulas: ~1 milhao de particulas vivas (emissor de taxa alta, gravidade + arraste +
// cor/tamanho ao longo da vida). Imprime o tempo medio de update por quadro, p95 e a memoria aproximada.
// Uso: tool_particles_bench [particulas=1000000] [quadros=300] [serial=0]
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "tool/particles/system.hpp"

using namespace tool::particles;
using tool::f32;
using tool::u32;

int main(int argc, char** argv) {
  const u32 target = argc > 1 ? static_cast<u32>(std::atoi(argv[1])) : 1000000u;
  const int frames = argc > 2 ? std::atoi(argv[2]) : 300;
  const bool parallel = !(argc > 3 && std::atoi(argv[3]) != 0);
  const f32 dt = 1.0f / 60;
  const f32 life = 4.0f;   // vida media: vivas em regime = taxa * vida

  Effect fx;
  fx.emitter.rate = static_cast<f32>(target) / life;
  fx.emitter.shape = EmitShape::Sphere; fx.emitter.radius = 2; fx.emitter.seed = 1234;
  fx.init.lifeMin = 3.5f; fx.init.lifeMax = 4.5f;
  fx.init.speedMin = 1; fx.init.speedMax = 6; fx.init.coneDeg = 45;
  fx.init.sizeMin = 0.05f; fx.init.sizeMax = 0.15f;
  Module g; g.kind = ModuleKind::Gravity; fx.modules.push_back(g);
  Module d; d.kind = ModuleKind::Drag; d.drag = 0.3f; fx.modules.push_back(d);
  Module co; co.kind = ModuleKind::ColorOverLife; co.c0 = packColor(1, 0.8f, 0.2f, 1); co.c1 = packColor(0.3f, 0, 0, 0);
  fx.modules.push_back(co);
  Module sz; sz.kind = ModuleKind::SizeOverLife; sz.s0 = 0.1f; sz.s1 = 0; fx.modules.push_back(sz);

  ParticleSystem sys(fx, target + target / 4);   // folga de 25% para a variacao da vida
  // Aquecimento: enche ate o regime (5 s simulados).
  for (int i = 0; i < 300; ++i) sys.update(dt, parallel);

  std::vector<double> ms;
  ms.reserve(static_cast<size_t>(frames));
  for (int i = 0; i < frames; ++i) {
    const auto t0 = std::chrono::steady_clock::now();
    sys.update(dt, parallel);
    ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
  }
  double sum = 0;
  for (double v : ms) sum += v;
  std::sort(ms.begin(), ms.end());
  const double p95 = ms[static_cast<size_t>(static_cast<double>(ms.size()) * 0.95)];
  std::printf("particles bench: modo=%s threads=%zu vivas=%u descartadas=%llu\n", parallel ? "paralelo" : "serial",
              tool::ParallelPool::get().threads(), sys.alive(), static_cast<unsigned long long>(sys.stats().dropped));
  std::printf("update por quadro: media %.3f ms, p95 %.3f ms, min %.3f ms, max %.3f ms (%d quadros)\n", sum / frames, p95,
              ms.front(), ms.back(), frames);
  std::printf("memoria: %.1f MB em %u chunks (%.1f B por particula viva)\n",
              static_cast<double>(sys.pool().memoryBytes()) / (1024.0 * 1024.0), sys.pool().chunkCount(),
              static_cast<double>(sys.pool().memoryBytes()) / std::max(1u, sys.alive()));
  return 0;
}
