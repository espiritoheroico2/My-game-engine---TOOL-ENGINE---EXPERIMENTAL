#pragma once
// Planejador puro de argumentos de desenho indireto (udc.md 8 e 10.2). Converte lotes em DrawIndexedIndirect,
// sem RHI. Se a capacidade do buffer nao couber, ou um contador estourar 32 bits, o plano falha (ok = false) e o
// chamador usa o caminho CPU (um draw por lote). Layout igual ao de D3D12/Vulkan: 5 uint32, 20 bytes por argumento.
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "tool/render/instance_batch.hpp"

namespace tool::render {

// Argumentos de um DrawIndexedIndirect (20 bytes), no layout de D3D12 e Vulkan.
struct DrawIndexedIndirectArgs {
  std::uint32_t indexCount = 0;
  std::uint32_t instanceCount = 0;
  std::uint32_t firstIndex = 0;
  std::int32_t baseVertex = 0;
  std::uint32_t firstInstance = 0;
};
static_assert(sizeof(DrawIndexedIndirectArgs) == 20, "layout de DrawIndexedIndirect");

// Faixa de indices e vertice base de uma malha, usada para preencher o argumento.
struct IndirectMeshRange {
  std::uint32_t indexCount = 0;
  std::uint32_t firstIndex = 0;
  std::int32_t baseVertex = 0;
};

// Plano de desenho indireto: um argumento por lote. ok = false manda o chamador usar o caminho CPU.
struct IndirectPlan {
  std::vector<DrawIndexedIndirectArgs> args;  // um por lote (draws nativos)
  std::uint64_t logicalDraws = 0;             // instancias somadas (draws logicos)
  bool ok = false;                            // false = usar o caminho CPU
};

// lookup(mesh) devolve o intervalo de indices da malha. Falha se args nao couber em maxArgs ou houver overflow.
inline IndirectPlan planIndirectArgs(const std::vector<InstanceBatch>& batches,
                                     const std::function<IndirectMeshRange(std::uint64_t)>& lookup,
                                     std::size_t maxArgs) {
  IndirectPlan plan;
  if (batches.size() > maxArgs) return plan;
  plan.args.reserve(batches.size());
  for (const InstanceBatch& b : batches) {
    if (b.count == 0) return IndirectPlan{};   // lote vazio nao deve existir; falha em vez de desenhar lixo
    const std::uint64_t end = static_cast<std::uint64_t>(b.first) + b.count;
    if (end > 0xFFFFFFFFull) return IndirectPlan{};   // firstInstance + instanceCount estoura 32 bits
    const IndirectMeshRange r = lookup(b.mesh);
    if (r.indexCount == 0) return IndirectPlan{};
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
  return plan;
}

}  // namespace tool::render
