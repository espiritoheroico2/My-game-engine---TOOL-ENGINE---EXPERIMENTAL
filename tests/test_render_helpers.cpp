// Helpers puros de render (udc.md 14.1): cache de bind, argumentos indiretos e anel de timestamps de GPU.
#include <cmath>
#include <limits>

#include "test.hpp"
#include "tool/render/bind_state_cache.hpp"
#include "tool/render/gpu_timing_ring.hpp"
#include "tool/render/indirect_plan.hpp"

using namespace tool::render;

// ---------- State cache ----------
TEST(bind_cache_same_state_does_not_rebind) {
  BindStateCache c(4);
  CHECK(c.needsBind(0, 7));    // primeiro bind
  CHECK(!c.needsBind(0, 7));   // mesmo estado: sem bind
  CHECK(!c.needsBind(0, 7));
  CHECK(c.binds() == 1);
  CHECK(c.skipped() == 2);
}

TEST(bind_cache_change_rebinds_and_slots_are_independent) {
  BindStateCache c(4);
  CHECK(c.needsBind(0, 1));
  CHECK(c.needsBind(0, 2));    // mudou: bind
  CHECK(c.needsBind(1, 2));    // outro slot: bind mesmo com o mesmo id
  CHECK(!c.needsBind(1, 2));
}

TEST(bind_cache_invalidate_forces_next_bind) {
  BindStateCache c(4);
  CHECK(c.needsBind(2, 9));
  c.invalidateSlot(2);
  CHECK(c.needsBind(2, 9));
  CHECK(c.needsBind(3, 5));
  c.invalidateAll();
  CHECK(c.needsBind(3, 5));
}

TEST(bind_cache_out_of_range_and_unknown_always_bind) {
  BindStateCache c(2);
  CHECK(c.needsBind(5, 1));
  CHECK(c.needsBind(5, 1));    // slot fora do cache nunca e pulado
  CHECK(c.needsBind(0, BindStateCache::kUnknown));
  CHECK(c.needsBind(0, BindStateCache::kUnknown));
}

// ---------- Argumentos indiretos ----------
namespace {
IndirectMeshRange rangeOf(std::uint64_t mesh) {
  return IndirectMeshRange{static_cast<std::uint32_t>(10 + mesh), static_cast<std::uint32_t>(100 * mesh), 3};
}
}  // namespace

TEST(indirect_one_arg_per_batch_with_logical_count) {
  std::vector<InstanceBatch> batches{{1, 0, 0, 4}, {2, 0, 4, 6}};
  const IndirectPlan p = planIndirectArgs(batches, rangeOf, 16);
  CHECK(p.ok);
  CHECK(p.args.size() == 2);
  CHECK(p.logicalDraws == 10);
  CHECK(p.args[0].indexCount == 11 && p.args[0].instanceCount == 4 && p.args[0].firstInstance == 0);
  CHECK(p.args[1].indexCount == 12 && p.args[1].instanceCount == 6 && p.args[1].firstInstance == 4);
  CHECK(p.args[1].firstIndex == 200 && p.args[1].baseVertex == 3);
}

TEST(indirect_args_layout_is_aligned_and_packed) {
  CHECK(sizeof(DrawIndexedIndirectArgs) == 20);
  CHECK(alignof(DrawIndexedIndirectArgs) == 4);
  const std::size_t stride = sizeof(DrawIndexedIndirectArgs);
  for (std::size_t i = 0; i < 8; ++i) CHECK((i * stride) % 4 == 0);
}

TEST(indirect_overflow_capacity_and_empty_fall_back_to_cpu) {
  // capacidade: mais lotes do que argumentos cabem
  std::vector<InstanceBatch> many{{1, 0, 0, 1}, {1, 0, 1, 1}, {1, 0, 2, 1}};
  CHECK(!planIndirectArgs(many, rangeOf, 2).ok);
  CHECK(planIndirectArgs(many, rangeOf, 3).ok);
  // overflow de 32 bits em first + count
  std::vector<InstanceBatch> big{{1, 0, 0xFFFFFFF0u, 0x20u}};
  CHECK(!planIndirectArgs(big, rangeOf, 4).ok);
  // lote vazio ou malha sem indices: falha
  std::vector<InstanceBatch> empty{{1, 0, 0, 0}};
  CHECK(!planIndirectArgs(empty, rangeOf, 4).ok);
  std::vector<InstanceBatch> noIdx{{0, 0, 0, 1}};   // rangeOf(0) tem indexCount = 10, troca o lookup
  auto zeroLookup = [](std::uint64_t) { return IndirectMeshRange{0, 0, 0}; };
  CHECK(!planIndirectArgs(noIdx, zeroLookup, 4).ok);
  // sem lotes: plano valido e vazio
  CHECK(planIndirectArgs({}, rangeOf, 0).ok);
}

// ---------- Tempos de GPU ----------
TEST(gpu_timing_ready_only_after_latency) {
  GpuTimingRing r(3);
  r.record(10, 2.5f);
  float ms = -1.f;
  CHECK(!r.query(10, 10, ms));   // mesmo quadro: ainda nao resolve
  CHECK(!r.query(10, 12, ms));   // 2 quadros depois: ainda nao
  CHECK(r.query(10, 13, ms));    // latencia de 3 quadros
  CHECK(ms == 2.5f);
}

TEST(gpu_timing_invalid_values_are_ignored) {
  GpuTimingRing r(2);
  r.record(4, std::numeric_limits<float>::quiet_NaN());
  r.record(4, std::numeric_limits<float>::infinity());
  r.record(4, -1.f);
  float ms = 0.f;
  CHECK(!r.query(4, 100, ms));
}

TEST(gpu_timing_slot_is_overwritten_by_newer_frame) {
  GpuTimingRing r(2);
  r.record(1, 1.f);
  r.record(3, 3.f);             // mesmo slot (1 % 2 == 3 % 2): sobrescreve o quadro 1
  float ms = 0.f;
  CHECK(!r.query(1, 100, ms));  // quadro 1 perdido
  CHECK(r.query(3, 100, ms));
  CHECK(ms == 3.f);
}
