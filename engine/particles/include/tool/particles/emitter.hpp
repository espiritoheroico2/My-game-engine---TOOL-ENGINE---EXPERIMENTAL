// ToolEngine - Particulas: emissor (taxa continua + rajadas + formato) e gerador pseudoaleatorio deterministico.
#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

#include "tool/core/types.hpp"

namespace tool::particles {

// Gerador pseudoaleatorio pequeno (splitmix64). Mesma semente = mesma sequencia, em qualquer plataforma.
struct Rng {
  u64 s;
  // Cria o gerador com a semente dada.
  explicit Rng(u64 seed) : s(seed) {}
  // Mistura de dois valores em uma semente (semente do emissor + indice da particula).
  static u64 hash(u64 seed, u64 index) {
    u64 z = seed * 0x9E3779B97F4A7C15ull + index * 0xD1B54A32D192ED03ull + 0x8CB92BA72F3D8DD7ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
  }
  // Proximo valor de 64 bits.
  u64 next64() {
    s += 0x9E3779B97F4A7C15ull;
    u64 z = s;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
  }
  // Proximo valor de 32 bits.
  u32 next32() { return static_cast<u32>(next64() >> 32); }
  // Float uniforme em [0, 1).
  f32 f01() { return static_cast<f32>(next64() >> 40) * (1.0f / 16777216.0f); }
  // Float uniforme em [a, b).
  f32 range(f32 a, f32 b) { return a + (b - a) * f01(); }
};

// Formato de emissao: de onde nasce a particula.
enum class EmitShape : u8 { Point, Sphere };

// Rajada: `count` particulas de uma vez quando o tempo do emissor passa de `time`.
struct Burst { f32 time = 0; u32 count = 0; };

// Descricao (dado) do emissor.
struct EmitterDesc {
  f32 rate = 0;                 // particulas por segundo (taxa continua)
  std::vector<Burst> bursts;    // rajadas (ordenadas por tempo ao iniciar o sistema)
  EmitShape shape = EmitShape::Point;
  f32 radius = 1;               // raio da esfera
  u64 seed = 1;                 // semente deterministica do emissor
  f32 duration = 0;             // duracao do ciclo em segundos (<= 0: sem fim)
  bool loop = true;             // com duration > 0: repete o ciclo (e as rajadas); senao para de emitir
};

// Estado de execucao do emissor.
struct EmitterState {
  double accum = 0;      // fracao de particula acumulada da taxa continua
  double time = 0;       // tempo dentro do ciclo
  size_t nextBurst = 0;  // proxima rajada a disparar
  u64 spawned = 0;       // total ja emitido (indice global da proxima particula: base do determinismo)
  bool finished = false; // ciclo terminou sem loop

  // Avanca dt segundos e devolve quantas particulas nascem neste passo (taxa acumulada + rajadas vencidas).
  u32 step(const EmitterDesc& d, double dt) {
    if (finished) return 0;
    u64 n = 0;
    if (d.rate > 0) {
      accum += static_cast<double>(d.rate) * dt;
      const u64 whole = static_cast<u64>(accum);
      n += whole;
      accum -= static_cast<double>(whole);
    }
    time += dt;
    while (nextBurst < d.bursts.size() && d.bursts[nextBurst].time < time) n += d.bursts[nextBurst++].count;
    if (d.duration > 0 && time >= d.duration) {
      if (d.loop) { time -= d.duration; nextBurst = 0; } else finished = true;
    }
    return static_cast<u32>(std::min<u64>(n, 0xFFFFFFFFull));
  }
  // Volta ao inicio (tempo, fracao e rajadas; o contador global continua para nao repetir sementes).
  void restart() { accum = 0; time = 0; nextBurst = 0; finished = false; }
};

// Sorteia um ponto dentro do formato (relativo a origem do emissor).
inline void samplePosition(const EmitterDesc& d, Rng& r, f32& x, f32& y, f32& z) {
  if (d.shape == EmitShape::Point) { x = y = z = 0; return; }
  // Esfera cheia: direcao uniforme e raio com raiz cubica (densidade uniforme no volume).
  const f32 cz = r.range(-1.0f, 1.0f);
  const f32 phi = r.range(0.0f, 6.2831853f);
  const f32 sr = std::sqrt(std::max(0.0f, 1.0f - cz * cz));
  const f32 rad = d.radius * std::cbrt(r.f01());
  x = rad * sr * std::cos(phi); y = rad * sr * std::sin(phi); z = rad * cz;
}

} // namespace tool::particles
