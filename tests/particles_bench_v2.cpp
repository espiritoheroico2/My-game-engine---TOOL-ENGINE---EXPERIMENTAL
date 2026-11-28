// Benchmark da biblioteca de particulas v2: 1 milhao de particulas vivas com turbulencia + empuxo + cor/tamanho/alfa.
// Mede (3 execucoes, mediana): tempo de update por quadro e tempo de geracao de quads (3D e 2D).
// Uso: tool_particles_bench_v2 [particulas=1000000] [serial=0]
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "tool/particles/quads.hpp"

using namespace tool::particles;
using tool::f32;
using tool::u32;

namespace {
using Clock = std::chrono::steady_clock;
double msSince(Clock::time_point t0) { return std::chrono::duration<double, std::milli>(Clock::now() - t0).count(); }
double median3(double a, double b, double c) { double v[3] = {a, b, c}; std::sort(v, v + 3); return v[1]; }
}  // namespace

int main(int argc, char** argv) {
  const u32 target = argc > 1 ? static_cast<u32>(std::atoi(argv[1])) : 1000000u;
  const bool parallel = !(argc > 2 && std::atoi(argv[2]) != 0);
  const f32 dt = 1.0f / 60, life = 4.0f;

  double upd[3], q3[3], q2[3];
  u32 aliveLast = 0;
  for (int run = 0; run < 3; ++run) {
    Effect fx;
    fx.emitter.rate = static_cast<f32>(target) / life;
    fx.emitter.shape = EmitShape::Box; fx.emitter.boxHalf[0] = 3; fx.emitter.boxHalf[1] = 0.5f; fx.emitter.boxHalf[2] = 3;
    fx.emitter.seed = 1234 + static_cast<tool::u64>(run);
    fx.init.lifeMin = 3.5f; fx.init.lifeMax = 4.5f; fx.init.speedMin = 0.5f; fx.init.speedMax = 2;
    fx.init.coneDeg = 30; fx.init.sizeMin = 0.1f; fx.init.sizeMax = 0.3f; fx.init.rotMaxDeg = 360;
    fx.shader = ShaderKind::Bloom; fx.emissive = 1.5f;
    Module t; t.kind = ModuleKind::Turbulence; t.amp = 2; t.freq = 0.4f; t.noiseSeed = 3; fx.modules.push_back(t);
    Module b; b.kind = ModuleKind::Buoyancy; b.amp = 1.5f; b.cooling = 0.4f; fx.modules.push_back(b);
    Module d; d.kind = ModuleKind::Drag; d.drag = 0.4f; fx.modules.push_back(d);
    Module co; co.kind = ModuleKind::ColorOverLife; co.c0 = packColor(1, 0.7f, 0.2f, 1); co.c1 = packColor(0.2f, 0.2f, 0.2f, 0); fx.modules.push_back(co);
    Module sz; sz.kind = ModuleKind::SizeOverLife; sz.s0 = 0.2f; sz.s1 = 0.6f; fx.modules.push_back(sz);
    Module al; al.kind = ModuleKind::AlphaOverLife; fx.modules.push_back(al);

    ParticleSystem sys(fx, target + target / 4);
    for (int i = 0; i < 300; ++i) sys.update(dt, parallel);   // regime
    // update: media de 30 quadros
    auto t0 = Clock::now();
    for (int i = 0; i < 30; ++i) sys.update(dt, parallel);
    upd[run] = msSince(t0) / 30.0;
    aliveLast = sys.alive();

    const f32 right[3] = {1, 0, 0}, up[3] = {0, 1, 0}, fwd[3] = {0, 0, -1};
    QuadBatch batch;
    buildQuads(sys, QuadView::billboard(right, up, fwd), batch, parallel);   // primeira chamada aloca o buffer
    t0 = Clock::now();
    for (int i = 0; i < 10; ++i) buildQuads(sys, QuadView::billboard(right, up, fwd), batch, parallel);
    q3[run] = msSince(t0) / 10.0;
    t0 = Clock::now();
    for (int i = 0; i < 10; ++i) buildQuads(sys, QuadView::plane2D(0), batch, parallel);
    q2[run] = msSince(t0) / 10.0;
    std::printf("execucao %d: vivas=%u update=%.3f ms quads3D=%.3f ms quads2D=%.3f ms\n", run + 1, sys.alive(), upd[run], q3[run], q2[run]);
  }
  std::printf("MEDIANA (modo=%s threads=%zu vivas=%u, QuadInstance=%zu B, buffer=%.1f MB): update %.3f ms, quads3D %.3f ms, quads2D %.3f ms\n",
              parallel ? "paralelo" : "serial", tool::ParallelPool::get().threads(), aliveLast, sizeof(QuadInstance),
              static_cast<double>(aliveLast) * sizeof(QuadInstance) / (1024.0 * 1024.0), median3(upd[0], upd[1], upd[2]),
              median3(q3[0], q3[1], q3[2]), median3(q2[0], q2[1], q2[2]));
  return 0;
}
