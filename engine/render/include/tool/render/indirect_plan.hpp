#pragma once
// Planejador puro de argumentos de desenho indireto. Converte lotes em DrawIndexedIndirect,
// sem RHI. Layout igual ao de D3D12/Vulkan: 5 uint32, 20 bytes por argumento.
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "tool/render/instance_batch.hpp"

namespace tool::render {

struct DrawIndexedIndirectArgs {
  std::uint32_t indexCount = 0;
  std::uint32_t instanceCount = 0;
  std::uint32_t firstIndex = 0;
  std::int32_t baseVertex = 0;
  std::uint32_t firstInstance = 0;
};
static_assert(sizeof(DrawIndexedIndirectArgs) == 20, "layout de DrawIndexedIndirect");

struct IndirectMeshRange {
  std::uint32_t indexCount = 0;
  std::uint32_t firstIndex = 0;
  std::int32_t baseVertex = 0;
};

struct IndirectPlan {
  std::vector<DrawIndexedIndirectArgs> args;
  std::uint64_t logicalDraws = 0;
  bool ok = false;
};

// Versao sem alocacoes por frame: reaproveita o vetor interno de `plan`.
template <typename LookupFn>
inline void planIndirectArgs(const std::vector<InstanceBatch>& batches,
                             LookupFn&& lookup,
                             std::size_t maxArgs,
                             IndirectPlan& plan) {
  plan.args.clear();
  plan.logicalDraws = 0;
  plan.ok = false;
  if (batches.size() > maxArgs) return;
  plan.args.reserve(batches.size());
  for (const InstanceBatch& b : batches) {
    if (b.count == 0) {
      plan.args.clear();
      plan.logicalDraws = 0;
      return;
    }
    const std::uint64_t end = static_cast<std::uint64_t>(b.first) + b.count;
    if (end > 0xFFFFFFFFull) {
      plan.args.clear();
      plan.logicalDraws = 0;
      return;
    }
    const IndirectMeshRange r = lookup(b.mesh);
    if (r.indexCount == 0) {
      plan.args.clear();
      plan.logicalDraws = 0;
      return;
    }
    DrawIndexedIndirectArgs a;
    a.indexCount = r.indexCount;
    a.instanceCount = b.count;
    a.firstIndex = r.firstIndex;
    a.baseVertex = r.baseVertex;
    a.firstInstance = b.first;
    plan.args.push_back(a);
    plan.logicalDraws += b.count;
  }
  plan.ok = true;
}

inline IndirectPlan planIndirectArgs(const std::vector<InstanceBatch>& batches,
                                     const std::function<IndirectMeshRange(std::uint64_t)>& lookup,
                                     std::size_t maxArgs) {
  IndirectPlan plan;
  planIndirectArgs(batches, lookup, maxArgs, plan);
  return plan;
}

}  // namespace tool::render