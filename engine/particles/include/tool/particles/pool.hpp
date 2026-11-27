// ToolEngine - Particulas: pool SoA em chunks de 4096 particulas.
// Cada campo da particula e um array proprio (SoA), para os lacos de atualizacao serem simples e auto-vetorizaveis.
// As particulas vivas de um chunk ficam sempre densas em [0, count). Uma particula que morre e removida trocando-a pela
// ultima viva (swap-remove), entao o espaco liberado e reaproveitado pela proxima emissao sem realocar nada.
// A "lista de livres" e a lista de chunks que ainda tem espaco (ordenada: os chunks de menor indice enchem primeiro).
#pragma once
#include <algorithm>
#include <memory>
#include <vector>

#include "tool/core/types.hpp"

namespace tool::particles {

// Particulas por chunk (potencia de 2; um chunk e a unidade de paralelismo).
constexpr u32 kChunkSize = 4096;

// Um chunk: arrays SoA com ate kChunkSize particulas; as `count` primeiras estao vivas.
struct Chunk {
  u32 count = 0;                          // particulas vivas (densas em [0, count))
  alignas(64) f32 px[kChunkSize];         // posicao
  alignas(64) f32 py[kChunkSize];
  alignas(64) f32 pz[kChunkSize];
  alignas(64) f32 vx[kChunkSize];         // velocidade
  alignas(64) f32 vy[kChunkSize];
  alignas(64) f32 vz[kChunkSize];
  alignas(64) f32 age[kChunkSize];        // idade em segundos
  alignas(64) f32 life[kChunkSize];       // vida total em segundos
  alignas(64) f32 size[kChunkSize];       // tamanho (billboard)
  alignas(64) u32 color[kChunkSize];      // cor RGBA8 (R no byte baixo)
  alignas(64) u32 seed[kChunkSize];       // semente por particula (variacao em modulos futuros)

  // Copia todos os campos da particula `src` para `dst` (usado na remocao por troca).
  void copy(u32 dst, u32 src) {
    px[dst] = px[src]; py[dst] = py[src]; pz[dst] = pz[src];
    vx[dst] = vx[src]; vy[dst] = vy[src]; vz[dst] = vz[src];
    age[dst] = age[src]; life[dst] = life[src]; size[dst] = size[src];
    color[dst] = color[src]; seed[dst] = seed[src];
  }
  // Remove a particula `i` trocando-a pela ultima viva. Nao mexe nos contadores do pool.
  void removeAt(u32 i) { if (i + 1 != count) copy(i, count - 1); --count; }
  // Remove todas as particulas com idade >= vida (laco de compactacao). Devolve quantas saiu.
  u32 compactDead() {
    u32 dead = 0;
    for (u32 i = 0; i < count; ++i) dead += (age[i] >= life[i]) ? 1u : 0u;   // pre-contagem vetorizavel: sem mortas, sai logo
    if (dead == 0) return 0;
    u32 i = 0;
    while (i < count) {
      if (age[i] >= life[i]) removeAt(i);   // a ultima viva ocupa o lugar e e reavaliada
      else ++i;
    }
    return dead;
  }
};

// Trecho contiguo de slots recem-reservados dentro de um chunk.
struct Span { u32 chunk = 0, first = 0, count = 0; };

// Pool de particulas: ate `maxChunks` chunks, criados sob demanda (o vetor de chunks e reservado, nunca realoca).
class ParticlePool {
public:
  // Cria o pool com capacidade para `maxParticles` (arredondada para cima em chunks).
  explicit ParticlePool(u32 maxParticles) : maxChunks_((maxParticles + kChunkSize - 1) / kChunkSize) {
    chunks_.reserve(maxChunks_);
  }

  // Capacidade total em particulas.
  u32 capacity() const { return maxChunks_ * kChunkSize; }
  // Particulas vivas agora.
  u32 alive() const { return alive_; }
  // Chunks ja criados.
  u32 chunkCount() const { return static_cast<u32>(chunks_.size()); }
  // Acesso ao chunk i.
  Chunk& chunk(u32 i) { return *chunks_[i]; }
  const Chunk& chunk(u32 i) const { return *chunks_[i]; }
  // Memoria aproximada dos chunks criados, em bytes.
  size_t memoryBytes() const { return chunks_.size() * sizeof(Chunk); }

  // Reserva ate `n` slots, preenchendo primeiro os chunks com espaco. Os trechos reservados vao para `out`
  // (o contador de vivas dos chunks ja inclui os novos slots; o chamador deve preenche-los). Devolve quantos
  // slots foram concedidos: se for menor que `n`, o pool esta cheio e o resto deve ser descartado.
  u32 acquire(u32 n, std::vector<Span>& out) {
    out.clear();
    u32 got = 0;
    size_t used = 0;   // entradas do inicio da lista de livres que ficaram cheias
    for (; used < free_.size() && got < n; ++used) {
      Chunk& c = *chunks_[free_[used]];
      const u32 take = std::min(n - got, kChunkSize - c.count);
      if (take == 0) continue;
      out.push_back({free_[used], c.count, take});
      c.count += take; got += take;
      if (c.count < kChunkSize) break;   // ainda tem espaco: e o ultimo usado e continua na lista
    }
    // Remove da lista os chunks que encheram (todos antes de `used`, mais o atual se encheu).
    size_t drop = 0;
    while (drop < free_.size() && chunks_[free_[drop]]->count >= kChunkSize) ++drop;
    free_.erase(free_.begin(), free_.begin() + static_cast<std::ptrdiff_t>(drop));
    // Cria chunks novos enquanto faltar e couber.
    while (got < n && chunks_.size() < maxChunks_) {
      chunks_.push_back(std::make_unique<Chunk>());
      const u32 idx = static_cast<u32>(chunks_.size() - 1);
      Chunk& c = *chunks_[idx];
      const u32 take = std::min(n - got, kChunkSize);
      out.push_back({idx, 0, take});
      c.count = take; got += take;
      if (c.count < kChunkSize) free_.push_back(idx);
    }
    alive_ += got;
    return got;
  }

  // Libera uma particula (swap-remove). O slot volta para a lista de livres e sera reaproveitado.
  void kill(u32 chunkIdx, u32 i) {
    Chunk& c = *chunks_[chunkIdx];
    const bool wasFull = c.count >= kChunkSize;
    c.removeAt(i);
    --alive_;
    if (wasFull) free_.insert(std::lower_bound(free_.begin(), free_.end(), chunkIdx), chunkIdx);
  }

  // Recalcula a lista de livres e o contador de vivas (chame depois de compactar chunks diretamente).
  void rebuildFreeList() {
    free_.clear();
    alive_ = 0;
    for (u32 i = 0; i < chunks_.size(); ++i) {
      alive_ += chunks_[i]->count;
      if (chunks_[i]->count < kChunkSize) free_.push_back(i);
    }
  }

  // Mata todas as particulas (os chunks continuam alocados para reuso).
  void clear() {
    for (auto& c : chunks_) c->count = 0;
    rebuildFreeList();
  }

private:
  u32 maxChunks_;
  u32 alive_ = 0;
  std::vector<std::unique_ptr<Chunk>> chunks_;   // reservado: sem realocacao do vetor
  std::vector<u32> free_;                        // chunks com espaco, em ordem crescente
};

} // namespace tool::particles
