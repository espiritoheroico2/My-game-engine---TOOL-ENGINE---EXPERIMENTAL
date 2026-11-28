// Testes do nucleo CPU de particulas (pool, emissor, modulos, determinismo, paralelismo).
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "test.hpp"
#include "tool/particles/presets.hpp"
#include "tool/particles/quads.hpp"
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

// ---------------------------------------------------------------------------------------------------------------
// Biblioteca v2: formas volumetricas, turbulencia, empuxo, quads de render, JSON com shader.
// ---------------------------------------------------------------------------------------------------------------
namespace {
// Efeito com uma rajada unica de n particulas no tempo 0, vida longa e velocidade 0.
Effect burstEffect(u32 n, EmitShape shape) {
  Effect fx = baseEffect(0);
  fx.emitter.bursts.push_back({0.0f, n});
  fx.emitter.shape = shape;
  return fx;
}
}  // namespace

// (a) Caixa: todas as posicoes iniciais dentro do volume (e o volume e realmente preenchido nos 3 eixos).
TEST(particles_shape_box_inside) {
  Effect fx = burstEffect(5000, EmitShape::Box);
  fx.emitter.boxHalf[0] = 1; fx.emitter.boxHalf[1] = 2; fx.emitter.boxHalf[2] = 3;
  ParticleSystem s(fx, 10000);
  s.setOrigin(10, 20, 30);
  s.update(0.1f, false);
  CHECK(s.alive() == 5000);
  float mn[3] = {1e9f, 1e9f, 1e9f}, mx[3] = {-1e9f, -1e9f, -1e9f};
  for (u32 c = 0; c < s.pool().chunkCount(); ++c) {
    const Chunk& ch = s.pool().chunk(c);
    for (u32 i = 0; i < ch.count; ++i) {
      const f32 p[3] = {ch.px[i] - 10, ch.py[i] - 20, ch.pz[i] - 30};
      for (int k = 0; k < 3; ++k) { mn[k] = std::fmin(mn[k], p[k]); mx[k] = std::fmax(mx[k], p[k]); }
    }
  }
  CHECK(mn[0] >= -1.0001f && mx[0] <= 1.0001f && mn[1] >= -2.0001f && mx[1] <= 2.0001f && mn[2] >= -3.0001f && mx[2] <= 3.0001f);
  CHECK(mx[0] > 0.9f && mn[1] < -1.8f && mx[2] > 2.7f);
}

// (b) Esfera: dentro do raio.
TEST(particles_shape_sphere_inside) {
  Effect fx = burstEffect(5000, EmitShape::Sphere);
  fx.emitter.radius = 2;
  ParticleSystem s(fx, 10000);
  s.update(0.1f, false);
  f32 maxd = 0;
  for (u32 c = 0; c < s.pool().chunkCount(); ++c) {
    const Chunk& ch = s.pool().chunk(c);
    for (u32 i = 0; i < ch.count; ++i)
      maxd = std::fmax(maxd, std::sqrt(ch.px[i] * ch.px[i] + ch.py[i] * ch.py[i] + ch.pz[i] * ch.pz[i]));
  }
  CHECK(s.alive() == 5000 && maxd <= 2.0f + 1e-4f && maxd > 1.8f);
}

// (c) Cone: o angulo maximo da velocidade em relacao ao eixo e respeitado.
TEST(particles_shape_cone_angle) {
  Effect fx = burstEffect(5000, EmitShape::Cone);
  fx.emitter.radius = 0.5f;
  fx.init.dir[0] = 1; fx.init.dir[1] = 2; fx.init.dir[2] = 2;
  fx.init.coneDeg = 25; fx.init.speedMin = 1; fx.init.speedMax = 2;
  ParticleSystem s(fx, 10000);
  s.update(0.1f, false);
  const f32 cosMax = std::cos(25.0f * 0.017453292f);
  f32 minCos = 1;
  for (u32 c = 0; c < s.pool().chunkCount(); ++c) {
    const Chunk& ch = s.pool().chunk(c);
    for (u32 i = 0; i < ch.count; ++i) {
      const f32 v = std::sqrt(ch.vx[i] * ch.vx[i] + ch.vy[i] * ch.vy[i] + ch.vz[i] * ch.vz[i]);
      const f32 d = (ch.vx[i] * 1 + ch.vy[i] * 2 + ch.vz[i] * 2) / (3.0f * v);
      minCos = std::fmin(minCos, d);
    }
  }
  CHECK(s.alive() == 5000 && minCos >= cosMax - 1e-3f && minCos < cosMax + 0.02f);   // respeita e usa quase todo o angulo
}

// Formas disco/linha e velocidade radial.
TEST(particles_shape_disc_line_radial) {
  Effect d = burstEffect(2000, EmitShape::Disc);
  d.emitter.radius = 3; d.init.dir[0] = 0; d.init.dir[1] = 1; d.init.dir[2] = 0;
  ParticleSystem sd(d, 4000);
  sd.update(0.1f, false);
  const Chunk& cd = sd.pool().chunk(0);
  for (u32 i = 0; i < cd.count; ++i) {
    CHECK(std::fabs(cd.py[i]) < 1e-4f);
    CHECK(cd.px[i] * cd.px[i] + cd.pz[i] * cd.pz[i] <= 9.0f + 1e-3f);
  }
  Effect l = burstEffect(2000, EmitShape::Line);
  l.emitter.radius = 2;
  ParticleSystem sl(l, 4000);
  sl.update(0.1f, false);
  const Chunk& cl = sl.pool().chunk(0);
  for (u32 i = 0; i < cl.count; ++i) {
    const f32 len = std::sqrt(cl.px[i] * cl.px[i] + cl.py[i] * cl.py[i] + cl.pz[i] * cl.pz[i]);
    CHECK(len <= 2.0f + 1e-4f);
  }
  Effect r = burstEffect(2000, EmitShape::Sphere);
  r.emitter.radius = 1; r.init.velMode = VelMode::Radial; r.init.speedMin = r.init.speedMax = 2;
  ParticleSystem sr(r, 4000);
  sr.update(0.0001f, false);
  const Chunk& cr = sr.pool().chunk(0);
  for (u32 i = 0; i < cr.count; ++i) {
    const f32 p = std::sqrt(cr.px[i] * cr.px[i] + cr.py[i] * cr.py[i] + cr.pz[i] * cr.pz[i]);
    const f32 dot = cr.vx[i] * cr.px[i] + cr.vy[i] * cr.py[i] + cr.vz[i] * cr.pz[i];
    CHECK(dot > 0 && std::fabs(dot / p - 2.0f) < 1e-2f);   // velocidade paralela a posicao, modulo 2
  }
}

// (d) Turbulencia deterministica: mesma semente = mesmas posicoes; semente diferente ou sem turbulencia = diferente.
TEST(particles_turbulence_deterministic) {
  auto make = [](u32 noiseSeed, bool turb) {
    Effect fx = burstEffect(3000, EmitShape::Sphere);
    fx.emitter.radius = 2;
    if (turb) {
      Module m; m.kind = ModuleKind::Turbulence; m.amp = 3; m.freq = 0.7f; m.noiseSeed = noiseSeed;
      fx.modules.push_back(m);
    }
    return fx;
  };
  ParticleSystem a(make(5, true), 10000), b(make(5, true), 10000), c(make(6, true), 10000), n(make(5, false), 10000);
  for (int i = 0; i < 30; ++i) { a.update(1.0f / 60, false); b.update(1.0f / 60, true); c.update(1.0f / 60, false); n.update(1.0f / 60, false); }
  CHECK(positions(a) == positions(b));   // serial = paralelo = repetivel
  CHECK(positions(a) != positions(c));
  CHECK(positions(a) != positions(n));
  // O ruido e limitado e tem media proxima de zero.
  f32 mn = 1e9f, mx = -1e9f; double sum = 0; int cnt = 0;
  for (int i = 0; i < 4000; ++i) {
    const f32 v = gradNoise3(0.37f * static_cast<f32>(i), 0.11f * static_cast<f32>(i), 0.23f * static_cast<f32>(i), 9);
    mn = std::fmin(mn, v); mx = std::fmax(mx, v); sum += v; ++cnt;
  }
  CHECK(mn >= -1.5f && mx <= 1.5f && mx > 0.2f && mn < -0.2f && std::fabs(sum / cnt) < 0.1);
}

// (e) Empuxo: a particula quente sobe; resfriamento reduz a subida.
TEST(particles_buoyancy_rises) {
  auto make = [](f32 cooling) {
    Effect fx = burstEffect(1, EmitShape::Point);
    Module b; b.kind = ModuleKind::Buoyancy; b.amp = 4; b.cooling = cooling; fx.modules.push_back(b);
    return fx;
  };
  ParticleSystem hot(make(0), 10), cool(make(3), 10);
  for (int i = 0; i < 100; ++i) { hot.update(0.01f, false); cool.update(0.01f, false); }
  const Chunk& h = hot.pool().chunk(0);
  const Chunk& c = cool.pool().chunk(0);
  CHECK(h.vy[0] > 3.5f && h.py[0] > 1.5f);       // ~ v = 4, y ~ 2
  CHECK(c.py[0] > 0 && c.py[0] < h.py[0]);       // resfriou: subiu menos
  CHECK(c.temp[0] < 0.1f);
}

// (f) Quads 3D: um por particula viva, tamanho e cor corretos, billboard paralelo a camera.
TEST(particles_quads_billboard) {
  Effect fx = burstEffect(300, EmitShape::Box);
  fx.init.sizeMin = fx.init.sizeMax = 0.5f;
  fx.init.color = packColor(1, 0, 0, 1);
  fx.init.rotMinDeg = fx.init.rotMaxDeg = 30;
  fx.shader = ShaderKind::Bloom; fx.emissive = 2.0f; fx.material = 7;
  ParticleSystem s(fx, 1000);
  s.update(0.1f, false);
  const f32 right[3] = {0, 0, 1}, up[3] = {0, 1, 0}, fwd[3] = {-1, 0, 0};
  QuadBatch b;
  s.pool().chunk(0).alpha[0] = 0.5f;
  buildQuads(s, QuadView::billboard(right, up, fwd), b, false);
  CHECK(b.quads.size() == s.alive() && b.quads.size() == 300);
  CHECK(b.shader == ShaderKind::Bloom && b.emissive == 2.0f);
  CHECK(b.chunkOffset.size() == s.pool().chunkCount() + 1 && b.chunkOffset.back() == 300);
  bool ok = true;
  for (size_t i = 0; i < b.quads.size(); ++i) {
    const QuadInstance& q = b.quads[i];
    ok = ok && q.size == 0.5f && q.material == 7 && (q.color & 0x00FFFFFFu) == 0x0000FFu;
    ok = ok && std::fabs(q.rot - 30.0f * 0.017453292f) < 1e-5f;
    f32 cn[4][3];
    quadCorners(b.view, q, cn);
    const f32 cd = q.cx * fwd[0] + q.cy * fwd[1] + q.cz * fwd[2];
    for (int k = 0; k < 4; ++k)
      ok = ok && std::fabs(cn[k][0] * fwd[0] + cn[k][1] * fwd[1] + cn[k][2] * fwd[2] - cd) < 1e-4f;   // plano de frente para a camera
    const f32 ex = cn[1][0] - cn[0][0], ey = cn[1][1] - cn[0][1], ez = cn[1][2] - cn[0][2];
    ok = ok && std::fabs(std::sqrt(ex * ex + ey * ey + ez * ez) - 0.5f) < 1e-4f;
  }
  CHECK(ok);
  CHECK((b.quads[0].color >> 24) == 128);   // alfa 0.5 do campo de alfa multiplicado
  CHECK((b.quads[1].color >> 24) == 255);
  // Paralelo = serial.
  QuadBatch p;
  buildQuads(s, QuadView::billboard(right, up, fwd), p, true);
  CHECK(p.quads.size() == b.quads.size() && std::memcmp(p.quads.data(), b.quads.data(), b.quads.size() * sizeof(QuadInstance)) == 0);
}

// (g) Variante 2D: quads no plano (z = planeZ), eixos fixos, sem camera.
TEST(particles_quads_2d_plane) {
  Effect fx = burstEffect(100, EmitShape::Sphere);
  fx.init.sizeMin = fx.init.sizeMax = 1.0f;
  ParticleSystem s(fx, 1000);
  s.setOrigin(0, 0, 5);
  s.update(0.1f, false);
  QuadBatch b;
  buildQuads(s, QuadView::plane2D(0.25f), b, false);
  CHECK(b.quads.size() == 100);
  bool ok = true;
  for (const QuadInstance& q : b.quads) {
    ok = ok && q.cz == 0.25f;
    f32 cn[4][3];
    quadCorners(b.view, q, cn);
    for (int k = 0; k < 4; ++k) ok = ok && cn[k][2] == 0.25f;
    ok = ok && std::fabs(cn[1][0] - cn[0][0] - 1.0f) < 1e-5f && std::fabs(cn[2][1] - cn[1][1] - 1.0f) < 1e-5f;   // quad 1x1 alinhado aos eixos
  }
  CHECK(ok);
}

// (h) JSON com formas, turbulencia, empuxo e shader; valores invalidos sao rejeitados.
TEST(particles_json_v2) {
  auto fx = effectFromText(R"({"name":"fumaca","shader":"bloom","emissive":2.5,"material":3,
    "emitter":{"shape":"box","box":[1,2,3],"rate":10},
    "init":{"velocity":"radial","temperature":0.8,"rotation":[0,90],"direction":[0,1,0],"coneAngle":20},
    "modules":[{"type":"turbulence","amplitude":2,"frequency":0.4,"scroll":0.1,"seed":9},
               {"type":"buoyancy","value":3,"cooling":0.5},{"type":"alphaOverLife","from":1,"to":0}]})");
  CHECK(fx.has_value());
  CHECK(fx->shader == ShaderKind::Bloom && fx->emissive == 2.5f && fx->material == 3);
  CHECK(fx->emitter.shape == EmitShape::Box && fx->emitter.boxHalf[1] == 2 && fx->emitter.boxHalf[2] == 3);
  CHECK(fx->init.velMode == VelMode::Radial && fx->init.temp0 == 0.8f && fx->init.rotMaxDeg == 90);
  CHECK(fx->modules.size() == 3 && fx->modules[0].kind == ModuleKind::Turbulence && fx->modules[0].noiseSeed == 9);
  CHECK(fx->modules[1].kind == ModuleKind::Buoyancy && fx->modules[1].cooling == 0.5f);
  CHECK(fx->modules[2].kind == ModuleKind::AlphaOverLife);
  for (const char* sh : {"alpha", "additive", "bloom"})
    CHECK(effectFromText(std::string("{\"shader\":\"") + sh + "\"}").has_value());
  for (const char* sp : {"point", "sphere", "box", "cone", "disc", "line"})
    CHECK(effectFromText(std::string("{\"emitter\":{\"shape\":\"") + sp + "\"}}").has_value());
  CHECK(!effectFromText(R"({"shader":"xyz"})").has_value());
  CHECK(!effectFromText(R"({"emitter":{"shape":"cubo"}})").has_value());
  CHECK(!effectFromText(R"({"emissive":-1})").has_value());
  CHECK(!effectFromText(R"({"emitter":{"box":[-1,1,1]}})").has_value());
  CHECK(!effectFromText(R"({"init":{"velocity":"torto"}})").has_value());
  CHECK(!effectFromText(R"({"modules":[{"type":"turbulence","frequency":0}]})").has_value());
  CHECK(!effectFromText(R"({"modules":[{"type":"buoyancy","cooling":-2}]})").has_value());
}

// ---- P-B: presets, ida e volta do JSON, taxa de amostragem da turbulencia, soft particles e ordenacao de quads ----
TEST(particles_presets_load_and_roundtrip) {
  for (const char* name : kPresetNames) {
    auto fx = effectFromText(presetJson(name));
    CHECK(fx.has_value());
    // ida e volta: serializa e le de novo, o texto serializado precisa ser identico
    const std::string a = effectToJson(*fx).dump();
    auto again = effectFromText(a);
    CHECK(again.has_value());
    CHECK(effectToJson(*again).dump() == a);
  }
  CHECK(presetJson("nao_existe") == nullptr);
  auto smoke = effectFromText(kPresetSmoke);
  CHECK(smoke->shader == ShaderKind::Alpha && smoke->soft > 0 && smoke->emitter.shape == EmitShape::Box);
  auto fire = effectFromText(kPresetFire);
  CHECK(fire->shader == ShaderKind::Bloom && fire->emissive > 0);
}

TEST(particles_soft_and_every_json) {
  auto fx = effectFromText(R"({"soft":0.5,"modules":[{"type":"turbulence","every":4}]})");
  CHECK(fx.has_value() && fx->soft == 0.5f && fx->modules[0].every == 4);
  auto def = effectFromText(R"({"modules":[{"type":"turbulence"}]})");
  CHECK(def.has_value() && def->modules[0].every == 1);   // padrao: todo quadro
  CHECK(!effectFromText(R"({"soft":-1})").has_value());
  CHECK(!effectFromText(R"({"soft":1000})").has_value());
  CHECK(!effectFromText(R"({"modules":[{"type":"turbulence","every":0}]})").has_value());
  CHECK(!effectFromText(R"({"modules":[{"type":"turbulence","every":100}]})").has_value());
}

TEST(particles_turbulence_every_default_is_unchanged) {
  // every = 1 (padrao) tem de dar o MESMO resultado que antes da existencia do parametro (todo quadro).
  Effect fx = baseEffect(2000.0f, 10.0f);
  fx.init.speedMin = fx.init.speedMax = 1.0f;
  fx.init.coneDeg = 180;
  Module t; t.kind = ModuleKind::Turbulence; t.amp = 2.0f; t.freq = 0.5f;
  fx.modules.push_back(t);
  Effect fx1 = fx; fx1.modules[0].every = 1;
  ParticleSystem a(fx, 5000), b(fx1, 5000);
  for (int i = 0; i < 30; ++i) { a.update(1.0f / 30.0f, false); b.update(1.0f / 30.0f, false); }
  CHECK(positions(a) == positions(b));
  Effect fx4 = fx; fx4.modules[0].every = 4;
  ParticleSystem c(fx4, 5000);
  for (int i = 0; i < 30; ++i) c.update(1.0f / 30.0f, false);
  CHECK(positions(a) != positions(c));
  CHECK(a.alive() == c.alive());
}

TEST(particles_sort_quads_back_to_front) {
  std::vector<QuadInstance> in(5), out;
  const f32 zs[5] = {-3.f, -10.f, -1.f, -7.f, -5.f};
  for (int i = 0; i < 5; ++i) { in[static_cast<size_t>(i)] = {}; in[static_cast<size_t>(i)].cz = zs[i]; }
  const f32 eye[3] = {0, 0, 0}, fwd[3] = {0, 0, -1};
  QuadSortScratch sc;
  sortQuadsBackToFront(in, eye, fwd, out, sc);
  CHECK(out.size() == 5);
  for (size_t i = 1; i < out.size(); ++i) CHECK(out[i - 1].cz <= out[i].cz);   // do mais longe (-10) ao mais perto (-1)
  std::vector<QuadInstance> none, o2;
  sortQuadsBackToFront(none, eye, fwd, o2, sc);
  CHECK(o2.empty());
}
