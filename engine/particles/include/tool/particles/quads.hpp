// ToolEngine - Particulas: geracao dos dados de render (sem desenhar). Para cada particula viva sai um quad de billboard
// em um buffer contiguo (QuadInstance, 32 bytes) pronto para upload como instance buffer. Os quads de um chunk ficam
// em um trecho proprio do buffer (offsets por chunk), entao cada chunk pode ser preenchido em paralelo.
// Billboard 3D: todos os quads usam os eixos right/up da camera (ficam paralelos ao plano da imagem).
// Variante 2D: quads no plano XY (eixos fixos, sem camera), com z = planeZ.
#pragma once
#include <cmath>
#include <cstring>
#include <vector>

#include "tool/core/parallel.hpp"
#include "tool/particles/system.hpp"

namespace tool::particles {

// Instancia de um quad: centro no mundo, tamanho (lado), rotacao no plano, cor RGBA8 (alfa ja multiplicado pelo campo
// de alfa), indice do material e temperatura (para shaders de fogo).
struct QuadInstance {
  f32 cx, cy, cz;
  f32 size;
  f32 rot;        // radianos, no plano do quad
  u32 color;      // RGBA8 (R no byte baixo)
  u32 material;   // indice do material do efeito
  f32 temp;
};
static_assert(sizeof(QuadInstance) == 32, "QuadInstance deve ter 32 bytes para upload direto");

// Orientacao do quad: eixos right/up (unitarios) e a direcao para onde a camera olha (forward).
struct QuadView {
  f32 right[3] = {1, 0, 0};
  f32 up[3] = {0, 1, 0};
  f32 forward[3] = {0, 0, -1};
  bool is2D = false;
  f32 planeZ = 0;   // so no modo 2D

  // Billboard voltado para a camera: usa os eixos da camera (right, up e forward devem ser unitarios e ortogonais).
  static QuadView billboard(const f32 right[3], const f32 up[3], const f32 forward[3]) {
    QuadView v;
    for (int k = 0; k < 3; ++k) { v.right[k] = right[k]; v.up[k] = up[k]; v.forward[k] = forward[k]; }
    return v;
  }
  // Quads no plano XY, sem camera.
  static QuadView plane2D(f32 z = 0) { QuadView v; v.is2D = true; v.planeZ = z; return v; }
};

// Resultado: buffer contiguo de quads + parametros do efeito para o renderer.
struct QuadBatch {
  QuadView view;
  ShaderKind shader = ShaderKind::Alpha;
  f32 emissive = 0;
  f32 soft = 0;                          // distancia do fade suave perto de superficies (m); 0 = desligado
  std::vector<QuadInstance> quads;       // um por particula viva, chunks em sequencia
  std::vector<u32> chunkOffset;          // primeiro quad de cada chunk (tamanho = chunks + 1)
};

// Preenche `out` (nao escreve alem de c.count itens) com os quads do chunk.
inline void buildChunkQuads(const Chunk& c, const Effect& fx, const QuadView& view, QuadInstance* out) {
  const u32 n = c.count;
  const f32 z = view.planeZ;
  const bool flat = view.is2D;
  for (u32 i = 0; i < n; ++i) {
    QuadInstance& q = out[i];
    q.cx = c.px[i]; q.cy = c.py[i]; q.cz = flat ? z : c.pz[i];
    q.size = c.size[i];
    q.rot = c.rot[i];
    const u32 col = c.color[i];
    const f32 a = std::min(1.0f, std::max(0.0f, c.alpha[i]));
    const u32 a8 = static_cast<u32>(static_cast<f32>(col >> 24) * a + 0.5f);
    q.color = (col & 0x00FFFFFFu) | (a8 << 24);
    q.material = fx.material;
    q.temp = c.temp[i];
  }
}

// Gera os quads de todas as particulas vivas do sistema. `parallel` preenche os chunks em paralelo.
inline void buildQuads(const ParticleSystem& s, const QuadView& view, QuadBatch& batch, bool parallel = true) {
  const ParticlePool& pool = s.pool();
  const u32 nc = pool.chunkCount();
  batch.view = view;
  batch.shader = s.effect().shader;
  batch.emissive = s.effect().emissive;
  batch.soft = s.effect().soft;
  batch.chunkOffset.assign(static_cast<size_t>(nc) + 1, 0);
  for (u32 c = 0; c < nc; ++c) batch.chunkOffset[c + 1] = batch.chunkOffset[c] + pool.chunk(c).count;
  batch.quads.resize(batch.chunkOffset[nc]);
  auto fill = [&](size_t b, size_t e) {
    for (size_t c = b; c < e; ++c)
      buildChunkQuads(pool.chunk(static_cast<u32>(c)), s.effect(), view, batch.quads.data() + batch.chunkOffset[c]);
  };
  if (parallel && nc > 1) parallelFor(nc, 1, fill);
  else if (nc > 0) fill(size_t{0}, size_t{nc});
}

// Auxiliar da ordenacao por profundidade (reaproveitado entre quadros: sem alocar em regime).
struct QuadSortScratch {
  std::vector<u32> bucket;   // balde de cada quad
  std::vector<u32> count;    // contagem por balde (depois, deslocamento)
};

// Ordena os quads de TRAS para FRENTE (o mais longe primeiro), pela profundidade ao longo de `fwd` a partir de `eye`.
// Ordenacao aproximada em O(n): 1024 baldes lineares entre a menor e a maior profundidade (estavel dentro do balde).
// Serve para o blend alfa (o aditivo nao precisa de ordem). `out` recebe o resultado; `in` nao muda.
inline void sortQuadsBackToFront(const std::vector<QuadInstance>& in, const f32 eye[3], const f32 fwd[3],
                                 std::vector<QuadInstance>& out, QuadSortScratch& sc) {
  constexpr u32 kBuckets = 1024;
  const size_t n = in.size();
  out.resize(n);
  if (n == 0) return;
  sc.bucket.resize(n);
  f32 lo = 1e30f, hi = -1e30f;
  for (size_t i = 0; i < n; ++i) {
    const QuadInstance& q = in[i];
    const f32 d = (q.cx - eye[0]) * fwd[0] + (q.cy - eye[1]) * fwd[1] + (q.cz - eye[2]) * fwd[2];
    // Guarda a profundidade temporariamente como bits no vetor de baldes (reinterpretacao sem aliasing: memcpy).
    std::memcpy(&sc.bucket[i], &d, sizeof(f32));
    lo = std::min(lo, d); hi = std::max(hi, d);
  }
  const f32 span = hi - lo;
  const f32 scale = span > 1e-6f ? static_cast<f32>(kBuckets - 1) / span : 0.0f;
  sc.count.assign(kBuckets + 1, 0);
  for (size_t i = 0; i < n; ++i) {
    f32 d; std::memcpy(&d, &sc.bucket[i], sizeof(f32));
    const u32 b = static_cast<u32>((hi - d) * scale);   // mais longe (d maior) = balde menor = desenha primeiro
    sc.bucket[i] = std::min(b, kBuckets - 1);
    ++sc.count[sc.bucket[i] + 1];
  }
  for (u32 b = 0; b < kBuckets; ++b) sc.count[b + 1] += sc.count[b];
  for (size_t i = 0; i < n; ++i) out[sc.count[sc.bucket[i]]++] = in[i];
}

// Cantos do quad no mundo (ordem: inferior-esquerdo, inferior-direito, superior-direito, superior-esquerdo), com a
// rotacao aplicada. Util para renderer em CPU e para testes.
inline void quadCorners(const QuadView& v, const QuadInstance& q, f32 out[4][3]) {
  const f32 h = q.size * 0.5f, cs = std::cos(q.rot), sn = std::sin(q.rot);
  f32 r[3], u[3];
  for (int k = 0; k < 3; ++k) {
    r[k] = (v.right[k] * cs + v.up[k] * sn) * h;
    u[k] = (-v.right[k] * sn + v.up[k] * cs) * h;
  }
  const f32 sx[4] = {-1, 1, 1, -1}, sy[4] = {-1, -1, 1, 1};
  const f32 c[3] = {q.cx, q.cy, q.cz};
  for (int i = 0; i < 4; ++i)
    for (int k = 0; k < 3; ++k) out[i][k] = c[k] + sx[i] * r[k] + sy[i] * u[k];
}

} // namespace tool::particles
