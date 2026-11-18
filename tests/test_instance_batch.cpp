// Agrupamento para instanciamento (P1): ordem estavel, lotes por malha e textura, limite por lote.
#include "test.hpp"
#include "tool/render/instance_batch.hpp"

using namespace tool::render;

namespace {
std::uint32_t total(const BatchPlan& p) {
  std::uint32_t s = 0;
  for (const InstanceBatch& b : p.batches) s += b.count;
  return s;
}
}  // namespace

TEST(instance_batch_empty) {
  const BatchPlan p = planInstanceBatches({});
  CHECK(p.batches.empty());
  CHECK(p.order.empty());
}

TEST(instance_batch_same_mesh_one_draw) {
  std::vector<BatchItem> items(5, BatchItem{7, 3});
  const BatchPlan p = planInstanceBatches(items);
  CHECK(p.batches.size() == 1);
  CHECK(p.batches[0].count == 5);
  CHECK(p.batches[0].first == 0);
  CHECK(total(p) == items.size());
}

TEST(instance_batch_interleaved_meshes_grouped_stable) {
  // A B A B: dois lotes; dentro de cada malha a ordem original fica (0,2 e depois 1,3).
  const std::vector<BatchItem> items = {{1, 0}, {2, 0}, {1, 0}, {2, 0}};
  const BatchPlan p = planInstanceBatches(items);
  CHECK(p.batches.size() == 2);
  CHECK(p.batches[0].mesh == 1 && p.batches[0].count == 2 && p.batches[0].first == 0);
  CHECK(p.batches[1].mesh == 2 && p.batches[1].count == 2 && p.batches[1].first == 2);
  CHECK(p.order[0] == 0 && p.order[1] == 2 && p.order[2] == 1 && p.order[3] == 3);
}

TEST(instance_batch_texture_splits_same_mesh) {
  const std::vector<BatchItem> items = {{1, 10}, {1, 20}, {1, 10}};
  const BatchPlan p = planInstanceBatches(items);
  CHECK(p.batches.size() == 2);
  CHECK(p.batches[0].texture == 10 && p.batches[0].count == 2);
  CHECK(p.batches[1].texture == 20 && p.batches[1].count == 1);
}

TEST(instance_batch_max_per_batch_splits) {
  std::vector<BatchItem> items(7, BatchItem{4, 0});
  const BatchPlan p = planInstanceBatches(items, 3);
  CHECK(p.batches.size() == 3);
  CHECK(p.batches[0].count == 3 && p.batches[0].first == 0);
  CHECK(p.batches[1].count == 3 && p.batches[1].first == 3);
  CHECK(p.batches[2].count == 1 && p.batches[2].first == 6);
  CHECK(total(p) == 7);
}

TEST(instance_batch_order_is_permutation) {
  std::vector<BatchItem> items;
  for (std::uint64_t i = 0; i < 50; ++i) items.push_back({i % 4, i % 3});
  const BatchPlan p = planInstanceBatches(items, 5);
  CHECK(p.order.size() == items.size());
  std::vector<int> seen(items.size(), 0);
  for (std::uint32_t idx : p.order) { CHECK(idx < items.size()); seen[idx]++; }
  for (int s : seen) CHECK(s == 1);
  CHECK(total(p) == items.size());
  // Dentro de cada lote, todos os itens tem a mesma chave.
  for (const InstanceBatch& b : p.batches)
    for (std::uint32_t k = b.first; k < b.first + b.count; ++k) {
      CHECK(items[p.order[k]].mesh == b.mesh);
      CHECK(items[p.order[k]].texture == b.texture);
    }
}

TEST(instance_gpu_layout_matches_object_cb) {
  CHECK(sizeof(InstanceGPU) == 160);
}
