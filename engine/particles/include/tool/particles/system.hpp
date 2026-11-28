// ToolEngine - Particulas: sistema = efeito + pool + estado do emissor. update(dt) emite e atualiza os chunks
// em paralelo com tool::parallelFor (pool persistente da engine).
#pragma once
#include <vector>

#include "tool/core/parallel.hpp"
#include "tool/particles/effect.hpp"

namespace tool::particles {

// Contadores do sistema.
struct SystemStats {
  u64 spawned = 0;   // particulas emitidas com sucesso
  u64 dropped = 0;   // particulas descartadas por pool cheio
};

// Sistema de particulas de um emissor.
class ParticleSystem {
public:
  // Cria o sistema para o efeito, com capacidade maxima de `maxParticles`.
  ParticleSystem(Effect fx, u32 maxParticles) : fx_(std::move(fx)), pool_(maxParticles) {
    std::stable_sort(fx_.emitter.bursts.begin(), fx_.emitter.bursts.end(),
                     [](const Burst& a, const Burst& b) { return a.time < b.time; });
  }

  // Posicao do emissor no mundo (as particulas novas nascem relativas a ela).
  void setOrigin(f32 x, f32 y, f32 z) { origin_[0] = x; origin_[1] = y; origin_[2] = z; }
  // Pede `count` particulas extras no proximo update (rajada manual).
  void burst(u32 count) { pending_ += count; }
  // Reinicia o emissor e mata todas as particulas.
  void reset() { state_.restart(); pool_.clear(); pending_ = 0; simTime_ = 0; frame_ = 0; }
  // Troca o efeito em execucao (edicao ao vivo no editor): as particulas vivas continuam, os modulos novos valem do
  // proximo quadro. A capacidade do pool nao muda (para isso, crie outro sistema).
  void setEffect(const Effect& fx) {
    fx_ = fx;
    std::stable_sort(fx_.emitter.bursts.begin(), fx_.emitter.bursts.end(),
                     [](const Burst& a, const Burst& b) { return a.time < b.time; });
  }
  // Origem atual do emissor (x, y, z).
  const f32* origin() const { return origin_; }

  // Avanca dt segundos: emite (taxa + rajadas) e atualiza todos os chunks. `parallel=false` roda tudo na thread
  // chamadora (mesmo resultado, util para teste e comparacao).
  void update(f32 dt, bool parallel = true) {
    // 1) Emissao: quantas nascem e onde cabem (serial e barato: so contas por chunk).
    u32 want = state_.step(fx_.emitter, static_cast<double>(dt)) + pending_;
    pending_ = 0;
    if (want > 0) {
      const u32 got = pool_.acquire(want, spans_);
      stats_.dropped += want - got;
      // 2) Inicializacao dos slots reservados. A semente de cada particula vem do indice global: independe de threads.
      std::vector<u64>& base = bases_;
      base.resize(spans_.size());
      u64 idx = state_.spawned;
      for (size_t s = 0; s < spans_.size(); ++s) { base[s] = idx; idx += spans_[s].count; }
      auto initSpan = [&](size_t s) {
        const Span& sp = spans_[s];
        initRange(pool_.chunk(sp.chunk), sp.first, sp.count, base[s], fx_.emitter, fx_.init, origin_);
      };
      runRange(spans_.size(), parallel, [&](size_t b, size_t e) { for (size_t s = b; s < e; ++s) initSpan(s); });
      state_.spawned += got;
      stats_.spawned += got;
    }
    // 3) Atualizacao por chunk (cada chunk e independente).
    runRange(pool_.chunkCount(), parallel, [&](size_t b, size_t e) {
      for (size_t c = b; c < e; ++c) updateChunk(pool_.chunk(static_cast<u32>(c)), dt, static_cast<u32>(c));
    });
    simTime_ += dt;
    ++frame_;
    // 4) Os chunks compactaram: refaz a lista de livres e o contador de vivas.
    pool_.rebuildFreeList();
  }

  // Acesso ao pool (leitura para desenho/testes).
  ParticlePool& pool() { return pool_; }
  const ParticlePool& pool() const { return pool_; }
  const Effect& effect() const { return fx_; }
  const SystemStats& stats() const { return stats_; }
  // Particulas vivas.
  u32 alive() const { return pool_.alive(); }

private:
  // Executa fn(begin, end) sobre [0, n): em paralelo (1 chunk por bloco minimo) ou serial.
  template <class F> void runRange(size_t n, bool parallel, F&& fn) {
    if (parallel && n > 1) parallelFor(n, 1, fn);
    else if (n > 0) fn(size_t{0}, n);
  }

  // Atualiza um chunk: modulos de velocidade, integracao, modulos de vida e compactacao dos mortos.
  void updateChunk(Chunk& c, f32 dt, u32 chunkIndex) const {
    if (c.count == 0) return;
    for (const Module& m : fx_.modules) {
      if (m.kind == ModuleKind::Gravity) modGravity(c, m.g, dt);
      else if (m.kind == ModuleKind::Drag) modDrag(c, m.drag, dt);
      else if (m.kind == ModuleKind::Turbulence) {
        // Taxa de amostragem: com every = N o ruido so e avaliado a cada N quadros (com o dt acumulado das N), e cada
        // chunk tem uma fase propria ((quadro + indice) % N) para o custo ficar espalhado. N = 1 (padrao): todo quadro.
        if (m.every <= 1) modTurbulence(c, m, simTime_, dt);
        else if ((frame_ + chunkIndex) % m.every == 0) modTurbulence(c, m, simTime_, dt * static_cast<f32>(m.every));
      }
      else if (m.kind == ModuleKind::Buoyancy) modBuoyancy(c, m.amp, m.cooling, dt);
    }
    modIntegrate(c, dt);
    for (const Module& m : fx_.modules) {
      if (m.kind == ModuleKind::ColorOverLife) modColorOverLife(c, m.c0, m.c1);
      else if (m.kind == ModuleKind::SizeOverLife) modSizeOverLife(c, m.s0, m.s1);
      else if (m.kind == ModuleKind::AlphaOverLife) modAlphaOverLife(c, m.a0, m.a1);
    }
    c.compactDead();
  }

  Effect fx_;
  ParticlePool pool_;
  EmitterState state_;
  SystemStats stats_;
  f32 origin_[3] = {0, 0, 0};
  u32 pending_ = 0;
  f32 simTime_ = 0;            // tempo simulado acumulado (anima o campo de turbulencia)
  u64 frame_ = 0;              // quadros simulados (fase da taxa de amostragem da turbulencia)
  std::vector<Span> spans_;    // reutilizados a cada quadro (sem alocar)
  std::vector<u64> bases_;
};

} // namespace tool::particles
