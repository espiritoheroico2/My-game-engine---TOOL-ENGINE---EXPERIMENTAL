#pragma once
// Planejador puro de batches, sem GPU ou RHI. O GpuRenderer o usa em passes opacos, emissao, prepass e sombras.
// Transparencia usa somente planAdjacentInstanceBatches para preservar a ordenacao de profundidade.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace tool::render {

// Dados por instancia. Mesmo layout de ObjectCB (renderer), 160 bytes, para poder reaproveitar o pacote.
struct InstanceGPU {
  float world[16];                  // coluna-major (como ObjectCB::world)
  float color[4];                   // albedo rgb + alfa
  float metallic, roughness, receiveShadow, pad;
  float texInfo[4];                 // largura, altura, 1 = tem textura
  float emissive[4];
  float fogColor[4];
  float fogParams[4];
};
static_assert(sizeof(InstanceGPU) == 160, "layout de InstanceGPU deve bater com ObjectCB (160 bytes)");

// Chave de agrupamento por malha e textura (0 = sem textura); transparencia exige adjacencia.
struct BatchItem { std::uint64_t mesh; std::uint64_t texture; };

// Um comando de desenho: 'count' instancias a partir de 'first' na ordem de 'BatchPlan::order'.
struct InstanceBatch { std::uint64_t mesh; std::uint64_t texture; std::uint32_t first; std::uint32_t count; };

// Resultado do planejamento: order (permutacao dos itens) e os lotes sobre essa ordem.
struct BatchPlan {
  std::vector<std::uint32_t> order;   // permutacao dos itens de entrada: order[k] = indice do item na posicao k
  std::vector<InstanceBatch> batches; // lotes em ordem; a soma de count == items.size()
};

// Agrupa por (malha, textura) com ordem estavel: dentro de cada grupo, a ordem original e preservada.
// maxPerBatch: limite de instancias por lote (tamanho do buffer de instancias); 0 = sem limite.
inline void planInstanceBatches(const std::vector<BatchItem>& items, BatchPlan& plan, std::uint32_t maxPerBatch = 0) {
  plan.order.clear();
  plan.batches.clear();
  plan.order.reserve(items.size());
  for (std::size_t i = 0; i < items.size(); ++i) plan.order.push_back(static_cast<std::uint32_t>(i));
  std::stable_sort(plan.order.begin(), plan.order.end(), [&](std::uint32_t a, std::uint32_t b) {
    const BatchItem& x = items[a];
    const BatchItem& y = items[b];
    if (x.mesh != y.mesh) return x.mesh < y.mesh;
    return x.texture < y.texture;
  });
  std::uint32_t pos = 0;
  std::size_t k = 0;
  while (k < plan.order.size()) {
    const BatchItem& key = items[plan.order[k]];
    std::size_t end = k + 1;
    while (end < plan.order.size() && items[plan.order[end]].mesh == key.mesh && items[plan.order[end]].texture == key.texture) ++end;
    // Quebra o grupo em lotes de no maximo maxPerBatch.
    std::size_t n = end - k;
    std::size_t done = 0;
    while (done < n) {
      std::size_t take = (maxPerBatch == 0) ? (n - done) : std::min<std::size_t>(maxPerBatch, n - done);
      plan.batches.push_back({key.mesh, key.texture, pos, static_cast<std::uint32_t>(take)});
      pos += static_cast<std::uint32_t>(take);
      done += take;
    }
    k = end;
  }
}

// Versao que devolve o plano pronto (aloca o resultado).
inline BatchPlan planInstanceBatches(const std::vector<BatchItem>& items, std::uint32_t maxPerBatch = 0) {
  BatchPlan plan;
  planInstanceBatches(items, plan, maxPerBatch);
  return plan;
}

// Converte cada elemento da fila em BatchItem com makeBatchItem e depois planeja os lotes.
template <typename QueueItem, typename BatchItemFn>
inline void planQueuedBatches(const std::vector<QueueItem>& queue, std::vector<BatchItem>& items,
                              BatchPlan& plan, BatchItemFn makeBatchItem,
                              std::uint32_t maxPerBatch = 0) {
  items.clear();
  items.reserve(queue.size());
  for (const QueueItem& item : queue) items.push_back(makeBatchItem(item));
  planInstanceBatches(items, plan, maxPerBatch);
}

// Preserva a ordem de entrada e agrupa somente sequencias adjacentes com a mesma chave.
inline void planAdjacentInstanceBatches(const std::vector<BatchItem>& items, BatchPlan& plan,
                                        std::uint32_t maxPerBatch = 0) {
  plan.order.clear();
  plan.batches.clear();
  plan.order.reserve(items.size());
  for (std::size_t i = 0; i < items.size(); ++i) plan.order.push_back(static_cast<std::uint32_t>(i));

  std::size_t begin = 0;
  std::uint32_t first = 0;
  while (begin < items.size()) {
    const BatchItem& key = items[begin];
    std::size_t end = begin + 1;
    while (end < items.size() && items[end].mesh == key.mesh && items[end].texture == key.texture) ++end;
    std::size_t next = begin;
    while (next < end) {
      const std::size_t count = maxPerBatch == 0
          ? end - next
          : std::min<std::size_t>(maxPerBatch, end - next);
      plan.batches.push_back({key.mesh, key.texture, first, static_cast<std::uint32_t>(count)});
      first += static_cast<std::uint32_t>(count);
      next += count;
    }
    begin = end;
  }
}

}  // namespace tool::render
