// ToolEngine - Particulas: modulos base. Cada modulo e uma funcao sobre um chunk (laco simples sobre arrays SoA).
// Ordem de execucao por quadro: emissao -> inicializacao (nas particulas novas) -> modulos de velocidade
// (gravidade, arraste) -> integracao (posicao e idade) -> modulos de vida (cor, tamanho) -> compactacao.
#pragma once
#include <algorithm>
#include <cmath>

#include "tool/particles/emitter.hpp"
#include "tool/particles/pool.hpp"

namespace tool::particles {

// Empacota RGBA (0..1) em u32 (R no byte baixo).
inline u32 packColor(f32 r, f32 g, f32 b, f32 a) {
  auto q = [](f32 v) { return static_cast<u32>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
  return q(r) | (q(g) << 8) | (q(b) << 16) | (q(a) << 24);
}

// Parametros do modulo de inicializacao (aplicado uma vez, quando a particula nasce).
struct InitParams {
  f32 lifeMin = 1, lifeMax = 1;       // vida em segundos
  f32 speedMin = 1, speedMax = 1;     // velocidade inicial
  f32 dir[3] = {0, 1, 0};             // eixo do cone de velocidade
  f32 coneDeg = 0;                    // meio angulo do cone em graus (0 = so ao longo do eixo; 180 = esfera)
  f32 sizeMin = 1, sizeMax = 1;       // tamanho inicial
  u32 color = 0xFFFFFFFFu;            // cor inicial RGBA8
};

// Inicializa `count` particulas do chunk a partir de `first`. A particula k usa o indice global `baseIndex + k`:
// a semente de cada uma sai de (semente do emissor, indice), entao o resultado nao depende de threads nem de chunks.
inline void initRange(Chunk& c, u32 first, u32 count, u64 baseIndex, const EmitterDesc& e, const InitParams& p,
                      const f32 origin[3]) {
  // Base ortonormal (t1, t2) perpendicular ao eixo do cone, calculada uma vez por trecho.
  f32 dx = p.dir[0], dy = p.dir[1], dz = p.dir[2];
  const f32 dl = std::sqrt(dx * dx + dy * dy + dz * dz);
  if (dl > 1e-6f) { dx /= dl; dy /= dl; dz /= dl; } else { dx = 0; dy = 1; dz = 0; }
  // Escolhe o eixo menos alinhado com a direcao para o produto vetorial nao degenerar.
  f32 ax = 1, ay = 0, az = 0;
  if (std::fabs(dx) > 0.9f) { ax = 0; ay = 1; }
  f32 t1x = dy * az - dz * ay, t1y = dz * ax - dx * az, t1z = dx * ay - dy * ax;
  const f32 t1l = std::sqrt(t1x * t1x + t1y * t1y + t1z * t1z);
  t1x /= t1l; t1y /= t1l; t1z /= t1l;
  const f32 t2x = dy * t1z - dz * t1y, t2y = dz * t1x - dx * t1z, t2z = dx * t1y - dy * t1x;
  const f32 cosMax = std::cos(std::clamp(p.coneDeg, 0.0f, 180.0f) * 0.017453292f);

  for (u32 k = 0; k < count; ++k) {
    const u32 i = first + k;
    Rng r(Rng::hash(e.seed, baseIndex + k));
    f32 ox, oy, oz;
    samplePosition(e, r, ox, oy, oz);
    c.px[i] = origin[0] + ox; c.py[i] = origin[1] + oy; c.pz[i] = origin[2] + oz;
    // Direcao no cone: cos(theta) uniforme entre cosMax e 1 (distribuicao uniforme na calota).
    const f32 ct = r.range(cosMax, 1.0f);
    const f32 st = std::sqrt(std::max(0.0f, 1.0f - ct * ct));
    const f32 ph = r.range(0.0f, 6.2831853f);
    const f32 cx = st * std::cos(ph), cy = st * std::sin(ph);
    const f32 sp = r.range(p.speedMin, p.speedMax);
    c.vx[i] = sp * (dx * ct + t1x * cx + t2x * cy);
    c.vy[i] = sp * (dy * ct + t1y * cx + t2y * cy);
    c.vz[i] = sp * (dz * ct + t1z * cx + t2z * cy);
    c.age[i] = 0;
    c.life[i] = std::max(1e-4f, r.range(p.lifeMin, p.lifeMax));
    c.size[i] = r.range(p.sizeMin, p.sizeMax);
    c.color[i] = p.color;
    c.seed[i] = r.next32();
  }
}

// Tipos de modulo de atualizacao.
enum class ModuleKind : u8 { Gravity, Drag, ColorOverLife, SizeOverLife };

// Modulo de atualizacao com parametros (os campos usados dependem do tipo).
struct Module {
  ModuleKind kind = ModuleKind::Gravity;
  f32 g[3] = {0, -9.8f, 0};   // Gravity: aceleracao
  f32 drag = 0;               // Drag: coeficiente k (v *= exp(-k dt))
  u32 c0 = 0xFFFFFFFFu;       // ColorOverLife: cor no nascimento
  u32 c1 = 0xFFFFFFFFu;       // ColorOverLife: cor na morte
  f32 s0 = 1, s1 = 1;         // SizeOverLife: tamanho no nascimento e na morte
};

// Gravidade: v += g * dt.
inline void modGravity(Chunk& c, const f32 g[3], f32 dt) {
  const u32 n = c.count;
  const f32 gx = g[0] * dt, gy = g[1] * dt, gz = g[2] * dt;
  for (u32 i = 0; i < n; ++i) { c.vx[i] += gx; c.vy[i] += gy; c.vz[i] += gz; }
}

// Arraste exponencial: v *= exp(-k dt) (estavel para qualquer dt).
inline void modDrag(Chunk& c, f32 k, f32 dt) {
  const u32 n = c.count;
  const f32 f = std::exp(-k * dt);
  for (u32 i = 0; i < n; ++i) { c.vx[i] *= f; c.vy[i] *= f; c.vz[i] *= f; }
}

// Integracao: posicao += v * dt e idade += dt (Euler semi-implicito: a velocidade ja foi atualizada).
inline void modIntegrate(Chunk& c, f32 dt) {
  const u32 n = c.count;
  for (u32 i = 0; i < n; ++i) {
    c.px[i] += c.vx[i] * dt; c.py[i] += c.vy[i] * dt; c.pz[i] += c.vz[i] * dt;
    c.age[i] += dt;
  }
}

// Cor ao longo da vida: interpola cada canal de c0 para c1 com t = idade / vida (aritmetica inteira de 8 bits).
inline void modColorOverLife(Chunk& c, u32 c0, u32 c1) {
  const u32 n = c.count;
  for (u32 i = 0; i < n; ++i) {
    const f32 t = std::min(1.0f, c.age[i] / c.life[i]);
    const i32 ti = static_cast<i32>(t * 256.0f);
    u32 out = 0;
    for (u32 ch = 0; ch < 4; ++ch) {
      const i32 a = static_cast<i32>((c0 >> (ch * 8)) & 0xFFu), b = static_cast<i32>((c1 >> (ch * 8)) & 0xFFu);
      out |= static_cast<u32>(a + (((b - a) * ti) >> 8)) << (ch * 8);
    }
    c.color[i] = out;
  }
}

// Tamanho ao longo da vida: interpola de s0 para s1 com t = idade / vida.
inline void modSizeOverLife(Chunk& c, f32 s0, f32 s1) {
  const u32 n = c.count;
  for (u32 i = 0; i < n; ++i) {
    const f32 t = std::min(1.0f, c.age[i] / c.life[i]);
    c.size[i] = s0 + (s1 - s0) * t;
  }
}

} // namespace tool::particles
