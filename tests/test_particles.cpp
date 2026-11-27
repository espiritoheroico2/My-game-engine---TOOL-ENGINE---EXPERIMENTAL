// Testes do nucleo CPU de particulas (pool, emissor, modulos, determinismo, paralelismo).
#include <cmath>
#include <vector>

#include "test.hpp"
#include "tool/particles/system.hpp"

using namespace tool::particles;
using tool::f32;
using tool::u32;

namespace {
// Efeito simples: sem velocidade, vida longa, so a taxa/rajadas pedidas.
Effect baseEffect(f32 rate, f32 life = 100.0f) {
  Effect fx;
  fx.emitter.rate = rate;
  fx.emitter.seed = 42;
  fx.init.lifeMin = fx.init.lifeMax = life;
  fx.init.speedMin = fx.init.speedMax = 0;
  return fx;
}
// Coleta todas as posicoes (x,y,z) das particulas vivas, na ordem dos chunks.
std::vector<f32> positions(const ParticleSystem& s) {
  std::vector<f32> v;
  for (u32 c = 0; c < s.pool().chunkCount(); ++c) {
    const Chunk& ch = s.pool().chunk(c);
    for (u32 i = 0; i < ch.count; ++i) { v.push_back(ch.px[i]); v.push_back(ch.py[i]); v.push_back(ch.pz[i]); }
  }
  return v;
}
}  // namespace

// (a) Alocar e liberar sem vazamento; os slots liberados sao reaproveitados (nenhum chunk novo).
TEST(particles_pool_alloc_free_reuse) {
  ParticlePool pool(3 * kChunkSize);
  std::vector<Span> sp;
  CHECK(pool.acquire(2 * kChunkSize + 10, sp) == 2 * kChunkSize + 10);
  CHECK(pool.alive() == 2 * kChunkSize + 10);
  CHECK(pool.chunkCount() == 3);
  // Libera 100 particulas do chunk 0 (estava cheio) e do chunk 1.
  for (int k = 0; k < 100; ++k) pool.kill(0, 0);
  CHECK(pool.alive() == 2 * kChunkSize + 10 - 100);
  CHECK(pool.chunk(0).count == kChunkSize - 100);
  // Pedir 100 de novo reaproveita o chunk 0, sem criar chunks.
  CHECK(pool.acquire(100, sp) == 100);
  CHECK(sp.size() == 1 && sp[0].chunk == 0);
  CHECK(pool.chunkCount() == 3);
  // Liberar tudo zera o contador (sem vazamento).
  pool.clear();
  CHECK(pool.alive() == 0);
  CHECK(pool.acquire(3 * kChunkSize, sp) == 3 * kChunkSize);
  CHECK(pool.chunkCount() == 3);
}

// (b) Mesma semente = mesma sequencia; semente diferente = sequencia diferente.
TEST(particles_determinism_by_seed) {
  Effect fx = baseEffect(5000);
  fx.emitter.shape = EmitShape::Sphere;
  fx.init.speedMin = 1; fx.init.speedMax = 3; fx.init.coneDeg = 40;
  ParticleSystem a(fx, 100000), b(fx, 100000);
  Effect fx2 = fx; fx2.emitter.seed = 43;
  ParticleSystem c(fx2, 100000);
  for (int i = 0; i < 20; ++i) { a.update(1.0f / 60, false); b.update(1.0f / 60, false); c.update(1.0f / 60, false); }
  CHECK(a.alive() > 0 && a.alive() == b.alive());
  CHECK(positions(a) == positions(b));
  CHECK(positions(a) != positions(c));
}

// (c) A gravidade integra a velocidade: v = g*t (tolerancia numerica).
TEST(particles_gravity_integrates_velocity) {
  Effect fx = baseEffect(0);
  fx.emitter.bursts.push_back({0.0f, 1});
  Module g; g.kind = ModuleKind::Gravity; g.g[0] = 0; g.g[1] = -10; g.g[2] = 0;
  fx.modules.push_back(g);
  ParticleSystem s(fx, 1000);
  const f32 dt = 0.01f;
  for (int i = 0; i < 100; ++i) s.update(dt, false);   // 1 segundo
  CHECK(s.alive() == 1);
  const Chunk& ch = s.pool().chunk(0);
  CHECK(std::fabs(ch.vy[0] - (-10.0f)) < 0.01f);
  // Euler semi-implicito: y = -10 * dt^2 * n(n+1)/2 ~ -5.05
  CHECK(std::fabs(ch.py[0] - (-5.05f)) < 0.05f);
}

// (d) Emissao continua: a taxa esperada em N segundos (tolerancia de 1 particula).
TEST(particles_continuous_rate) {
  ParticleSystem s(baseEffect(1000.5f), 100000);
  const f32 dt = 1.0f / 60;
  for (int i = 0; i < 120; ++i) s.update(dt, false);   // 2 s
  const double expected = 1000.5 * 120 * static_cast<double>(dt);
  CHECK(std::fabs(static_cast<double>(s.stats().spawned) - expected) <= 1.0);
  CHECK(s.alive() == s.stats().spawned);
}

// (e) Rajada: quantidade exata, uma unica vez no instante certo.
TEST(particles_burst_exact) {
  Effect fx = baseEffect(0);
  fx.emitter.bursts.push_back({0.5f, 777});
  fx.emitter.bursts.push_back({0.0f, 23});
  ParticleSystem s(fx, 10000);
  s.update(0.1f, false);
  CHECK(s.alive() == 23);          // so a rajada de tempo 0
  for (int i = 0; i < 20; ++i) s.update(0.1f, false);
  CHECK(s.alive() == 23 + 777);    // a segunda disparou uma vez so
  CHECK(s.stats().dropped == 0);
}

// (f) Pool cheio nao estoura: descarta e conta.
TEST(particles_pool_full_drops) {
  Effect fx = baseEffect(0);
  fx.emitter.bursts.push_back({0.0f, 3 * kChunkSize});
  ParticleSystem s(fx, 2 * kChunkSize);
  s.update(0.01f, false);
  CHECK(s.alive() == 2 * kChunkSize);
  CHECK(s.stats().spawned == 2 * kChunkSize);
  CHECK(s.stats().dropped == kChunkSize);
  s.burst(5);
  s.update(0.01f, false);          // continua cheio: tudo descartado, sem excecao
  CHECK(s.stats().dropped == kChunkSize + 5);
}

// (g) Update paralelo = update serial (mesmas posicoes com a mesma semente), com mortes e modulos.
TEST(particles_parallel_equals_serial) {
  Effect fx = baseEffect(200000);
  fx.emitter.shape = EmitShape::Sphere;
  fx.init.lifeMin = 0.2f; fx.init.lifeMax = 0.6f;
  fx.init.speedMin = 1; fx.init.speedMax = 5; fx.init.coneDeg = 60;
  Module g; g.kind = ModuleKind::Gravity; fx.modules.push_back(g);
  Module d; d.kind = ModuleKind::Drag; d.drag = 0.7f; fx.modules.push_back(d);
  Module co; co.kind = ModuleKind::ColorOverLife; co.c0 = 0xFFFFFFFFu; co.c1 = 0x00000000u; fx.modules.push_back(co);
  Module sz; sz.kind = ModuleKind::SizeOverLife; sz.s0 = 1; sz.s1 = 0; fx.modules.push_back(sz);
  ParticleSystem ser(fx, 400000), par(fx, 400000);
  for (int i = 0; i < 30; ++i) { ser.update(1.0f / 60, false); par.update(1.0f / 60, true); }
  CHECK(ser.alive() > 10000);
  CHECK(ser.alive() == par.alive());
  CHECK(positions(ser) == positions(par));
  // Particulas morrem e o pool nao vaza: vivas < total emitido.
  CHECK(ser.alive() < ser.stats().spawned);
}

// Efeito carregado de JSON gera o que o JSON descreve.
TEST(particles_effect_from_json) {
  auto fx = effectFromText(R"({"name":"t","emitter":{"rate":10,"seed":3,"bursts":[{"time":0,"count":5}]},
    "init":{"life":[1,2],"speed":4,"size":[0.5,1],"color":[1,0,0,1]},
    "modules":[{"type":"gravity","value":[0,-1,0]},{"type":"drag","value":0.3},{"type":"sizeOverLife","from":1,"to":0}]})");
  CHECK(fx.has_value());
  CHECK(fx->emitter.rate == 10 && fx->emitter.bursts.size() == 1 && fx->modules.size() == 3);
  CHECK(fx->init.speedMin == 4 && fx->init.lifeMax == 2);
  CHECK(fx->init.color == 0xFF0000FFu);
  CHECK(!effectFromText(R"({"modules":[{"type":"nao_existe"}]})").has_value());
}
