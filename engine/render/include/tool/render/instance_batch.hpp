#pragma once
// Planejador puro de batches, sem GPU ou RHI. O GpuRenderer o usa em passes opacos, emissao, prepass e sombras.
// Transparencia usa somente planAdjacentInstanceBatches para preservar a ordenacao de profundidade.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <vector>

namespace tool::render {

struct InstanceGPU {
  float world[16];
  float color[4];
  float metallic, roughness, receiveShadow, pad;
  float texInfo[4];
  float emissive[4];
  float fogColor[4];
  float fogParams[4];
};
static_assert(sizeof(InstanceGPU) == 160, "layout de InstanceGPU deve bater com ObjectCB (160 bytes)");

struct BatchItem { std::uint64_t mesh; std::uint64_t texture; };
struct InstanceBatch { std::uint64_t mesh; std::uint64_t texture; std::uint32_t first; std::uint32_t count; };

struct BatchPlan {
  std::vector<std::uint32_t> order;
  std::vector<InstanceBatch> batches;
};

inline void planInstanceBatches(const std::vector<BatchItem>& items, BatchPlan& plan, std::uint32_t maxPerBatch = 0) {
  plan.order.resize(items.size());
  plan.batches.clear();
  std::iota(plan.order.begin(), plan.order.end(), std::uint32_t{0});
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

inline BatchPlan planInstanceBatches(const std::vector<BatchItem>& items, std::uint32_t maxPerBatch = 0) {
  BatchPlan plan;
  planInstanceBatches(items, plan, maxPerBatch);
  return plan;
}

template <typename QueueItem, typename BatchItemFn>
inline void planQueuedBatches(const std::vector<QueueItem>& queue, std::vector<BatchItem>& items,
                              BatchPlan& plan, BatchItemFn makeBatchItem,
                              std::uint32_t maxPerBatch = 0) {
  items.clear();
  items.reserve(queue.size());
  for (const QueueItem& item : queue) items.push_back(makeBatchItem(item));
  planInstanceBatches(items, plan, maxPerBatch);
}

inline void planAdjacentInstanceBatches(const std::vector<BatchItem>& items, BatchPlan& plan,
                                        std::uint32_t maxPerBatch = 0) {
  plan.order.resize(items.size());
  plan.batches.clear();
  std::iota(plan.order.begin(), plan.order.end(), std::uint32_t{0});

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