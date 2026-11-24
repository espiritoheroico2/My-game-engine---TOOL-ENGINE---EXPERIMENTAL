// ToolEngine - ECS embutido (documento, secao ECS).
// Armazenamento esparso/denso por componente; entidades sao handles com geracao.
// Otimizado para iteracao densa contigua e execucao paralela sem alocacoes por quadro.
#pragma once
#include "tool/core/handle.hpp"
#include "tool/core/parallel.hpp"
#include "tool/core/tags.hpp"
#include "tool/debug/debug.hpp"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <deque>
#include <memory>
#include <stdexcept>
#include <typeinfo>
#include <utility>
#include <vector>

namespace tool {

using Entity = Handle;
using ComponentId = u32;

namespace detail {
inline ComponentId nextComponentId() {
  static std::atomic<ComponentId> n{0};
  const ComponentId id = n.fetch_add(1, std::memory_order_relaxed);
  if (id == static_cast<ComponentId>(-1)) std::abort();
  return id;
}
} // namespace detail

template <class T> ComponentId componentId() {
  static const ComponentId id = detail::nextComponentId();
  return id;
}

class World {
public:
  Entity create() {
    Entity e = ids_.create();
    ensure(e.index);
    tags_[e.index] = {};
    dormant_[e.index] = 0;
    sim_[e.index] = 1;
    return e;
  }

  bool alive(Entity e) const { return ids_.alive(e); }

  bool destroy(Entity e) {
    if (!ids_.alive(e)) {
      debug().report(diag::kStaleHandle, Severity::Warning, "destroy() com handle obsoleto");
      return false;
    }
    for (auto& s : storages_)
      if (s) s->remove(e.index);
    tags_[e.index] = {};
    dormant_[e.index] = 0;
    sim_[e.index] = 1;
    return ids_.destroy(e);
  }

  // --- componentes ------------------------------------------------------------
  template <class T, class... Args> T& add(Entity e, Args&&... args) {
    if (!alive(e)) throw std::invalid_argument("World::add: entidade invalida ou obsoleta");
    return storage<T>().set(e.index, T{std::forward<Args>(args)...});
  }

  template <class T, class... Args> T* tryAdd(Entity e, Args&&... args) {
    if (!alive(e)) return nullptr;
    return &storage<T>().set(e.index, T{std::forward<Args>(args)...});
  }

  template <class T> bool has(Entity e) const {
    const auto* st = findStorage<T>();
    return alive(e) && st && st->has(e.index);
  }

  template <class T> T* get(Entity e) {
    auto* st = findStorage<T>();
    return (alive(e) && st) ? st->get(e.index) : nullptr;
  }

  template <class T> const T* get(Entity e) const {
    const auto* st = findStorage<T>();
    return (alive(e) && st) ? st->get(e.index) : nullptr;
  }

  template <class T> void remove(Entity e) {
    if (auto* st = findStorage<T>(); st && alive(e)) st->remove(e.index);
  }

  // Acesso direto por indice interno (para hot-loops validados previamente)
  template <class T> T* getByIndex(u32 index) {
    auto* st = findStorage<T>();
    return st ? st->get(index) : nullptr;
  }
  template <class T> const T* getByIndex(u32 index) const {
    const auto* st = findStorage<T>();
    return st ? st->get(index) : nullptr;
  }
  template <class T> bool hasByIndex(u32 index) const {
    const auto* st = findStorage<T>();
    return st && st->has(index);
  }

  // --- tags -------------------------------------------------------------------
  void addTag(Entity e, TagId t) {
    if (!alive(e)) return;
    if (t == kInvalidIndex) {
      debug().report(diag::kTagOverflow, Severity::Error, "limite de 128 tags excedido");
      return;
    }
    tags_[e.index].set(t);
  }

  void removeTag(Entity e, TagId t) {
    if (alive(e) && t != kInvalidIndex) tags_[e.index].clear(t);
  }

  bool hasTag(Entity e, TagId t) const { return alive(e) && t != kInvalidIndex && tags_[e.index].test(t); }
  TagMask tagMask(Entity e) const { return alive(e) ? tags_[e.index] : TagMask{}; }

  // --- iteracao -----------------------------------------------------------------
  template <class... Ts, class F> void each(F&& fn) {
    static_assert(sizeof...(Ts) > 0);
    if (((findStorage<Ts>() == nullptr) || ...)) return;
    ScratchLease L = lease();
    const auto& src = smallestIndices<Ts...>();
    L.v = src;
    for (u32 i : L.v) {
      if (dormant_[i] || !ids_.aliveIndex(i) || !matchAll<Ts...>(i)) continue;
      fn(Entity{i, ids_.generationOf(i)}, *findStorage<Ts>()->get(i)...);
    }
  }

  // Varre um componente T direto no vetor denso (memoria contigua, sem copia de indices e sem busca esparsa).
  template <class T, class F> void forEachDense(F&& fn) {
    auto* st = findStorage<T>();
    if (!st) return;
    const size_t n = st->data.size();
    for (size_t p = 0; p < n; ++p) {
      const u32 i = st->denseIdx[p];
      if (dormant_[i] || !ids_.aliveIndex(i)) continue;
      fn(Entity{i, ids_.generationOf(i)}, st->data[p]);
    }
  }

  // Varre um componente T em paralelo diretamente sobre o array denso contiguo (zero alocacao, cache-friendly).
  template <class T, class F> void parallelEachDense(F&& fn, size_t grain = 512) {
    auto* st = findStorage<T>();
    if (!st || st->data.empty()) return;
    const size_t n = st->data.size();
    tool::parallelFor(n, grain, [&](size_t b, size_t e) {
      for (size_t p = b; p < e; ++p) {
        const u32 i = st->denseIdx[p];
        if (dormant_[i] || !ids_.aliveIndex(i)) continue;
        fn(Entity{i, ids_.generationOf(i)}, st->data[p]);
      }
    });
  }

  // Igual a each, mas dividido em blocos entre os nucleos (pool persistente).
  template <class... Ts, class F> void parallelEach(F&& fn, size_t grain = 512) {
    static_assert(sizeof...(Ts) > 0);
    if (((findStorage<Ts>() == nullptr) || ...)) return;
    const auto& idx = smallestIndices<Ts...>();
    if (idx.empty()) return;
    // Copia local apenas se houver risco de mutacao estrutural; para hot-loops paralelos, le direto de idx.
    tool::parallelFor(idx.size(), grain, [&](size_t b, size_t e) {
      for (size_t k = b; k < e; ++k) {
        const u32 i = idx[k];
        if (dormant_[i] || !ids_.aliveIndex(i) || !matchAll<Ts...>(i)) continue;
        fn(Entity{i, ids_.generationOf(i)}, *findStorage<Ts>()->get(i)...);
      }
    });
  }

  template <class T> size_t storageCount() const {
    auto* st = findStorage<T>();
    return st ? st->size() : 0;
  }

  template <class T> size_t activeCount() const {
    auto* st = findStorage<T>();
    if (!st) return 0;
    size_t n = 0;
    for (u32 i : st->indices()) if (ids_.aliveIndex(i) && !dormant_[i]) ++n;
    return n;
  }

  template <class T> size_t count() const { return activeCount<T>(); }

  void setSimulating(Entity e, bool v) {
    if (alive(e)) sim_[e.index] = v ? 1 : 0;
  }
  bool isSimulating(Entity e) const { return alive(e) && sim_[e.index] != 0; }

  template <class F> void eachTagged(const TagMask& mask, F&& fn) {
    for (u32 i = 0; i < tags_.size(); ++i) {
      if (!ids_.aliveIndex(i) || dormant_[i] || !tags_[i].containsAll(mask)) continue;
      fn(Entity{i, ids_.generationOf(i)});
    }
  }

  template <class F> void eachEntity(F&& fn) const {
    for (u32 i = 0; i < tags_.size(); ++i)
      if (ids_.aliveIndex(i) && !dormant_[i]) fn(Entity{i, ids_.generationOf(i)});
  }

  // --- suporte ao DeleteAndReuse ----------------------------------------------------
  void setDormant(Entity e, bool v) {
    if (alive(e)) dormant_[e.index] = v ? 1 : 0;
  }
  bool isDormant(Entity e) const { return alive(e) && dormant_[e.index] != 0; }
  Entity renew(Entity e) { return ids_.renew(e); }

  void resetTo(Entity e, const std::vector<ComponentId>& keep, const TagMask& mask) {
    if (!alive(e)) return;
    keepScratch_.assign(storages_.size(), 0);
    for (ComponentId k : keep) if (k < keepScratch_.size()) keepScratch_[k] = 1;
    for (ComponentId id = 0; id < storages_.size(); ++id)
      if (storages_[id] && !keepScratch_[id]) storages_[id]->remove(e.index);
    tags_[e.index] = mask;
  }

  size_t liveCount() const { return ids_.liveCount(); }

private:
  struct IStorage {
    virtual ~IStorage() = default;
    virtual void remove(u32 index) = 0;
  };

  template <class T> struct Storage final : IStorage {
    std::vector<u32> sparse; // indice da entidade -> posicao densa
    std::vector<u32> denseIdx;
    std::vector<T> data;

    bool has(u32 i) const { return i < sparse.size() && sparse[i] != kInvalidIndex; }
    T* get(u32 i) { return has(i) ? &data[sparse[i]] : nullptr; }
    const T* get(u32 i) const { return has(i) ? &data[sparse[i]] : nullptr; }
    size_t size() const { return data.size(); }
    const std::vector<u32>& indices() const { return denseIdx; }

    T& set(u32 i, T&& v) {
      if (i >= sparse.size()) sparse.resize(i + 1, kInvalidIndex);
      if (sparse[i] != kInvalidIndex) return data[sparse[i]] = std::move(v);
      sparse[i] = static_cast<u32>(data.size());
      denseIdx.push_back(i);
      data.push_back(std::move(v));
      return data.back();
    }

    void remove(u32 i) override {
      if (!has(i)) return;
      const u32 pos = sparse[i];
      const u32 last = denseIdx.back();
      if (pos != denseIdx.size() - 1) {
        data[pos] = std::move(data.back());
        denseIdx[pos] = last;
        sparse[last] = pos;
      }
      data.pop_back();
      denseIdx.pop_back();
      sparse[i] = kInvalidIndex;
    }
  };

  template <class... Ts> bool matchAll(u32 i) const {
    bool ok = true;
    ((ok = ok && findStorage<Ts>() && findStorage<Ts>()->has(i)), ...);
    return ok;
  }

  template <class... Ts> const std::vector<u32>& smallestIndices() const {
    const std::vector<u32>* best = nullptr;
    size_t n = static_cast<size_t>(-1);
    ((findStorage<Ts>()->size() < n ? (n = findStorage<Ts>()->size(), best = &findStorage<Ts>()->indices()) : nullptr), ...);
    return *best;
  }

  template <class T> Storage<T>& storage() {
    const ComponentId id = componentId<T>();
    if (id >= storages_.size()) storages_.resize(id + 1);
    if (!storages_[id]) storages_[id] = std::make_unique<Storage<T>>();
    return static_cast<Storage<T>&>(*storages_[id]);
  }

  template <class T> Storage<T>* findStorage() const {
    const ComponentId id = componentId<T>();
    return id < storages_.size() ? static_cast<Storage<T>*>(storages_[id].get()) : nullptr;
  }

  void ensure(u32 index) {
    if (index >= tags_.size()) {
      tags_.resize(index + 1);
      dormant_.resize(index + 1, 0); // uint8_t em vez de std::vector<bool> (bit-packed lento e nao thread-safe)
      sim_.resize(index + 1, 1);
    }
  }

  struct ScratchLease {
    World& w;
    std::vector<u32>& v;
    ~ScratchLease() { --w.scratchDepth_; }
  };

  ScratchLease lease() {
    if (scratchDepth_ == scratch_.size()) scratch_.emplace_back();
    std::vector<u32>& v = scratch_[scratchDepth_++];
    v.clear();
    return ScratchLease{*this, v};
  }

  HandleAllocator ids_;
  std::vector<std::unique_ptr<IStorage>> storages_;
  std::vector<TagMask> tags_;
  std::vector<uint8_t> dormant_;                // 1 byte por entidade (thread-safe; std::vector<bool> era bit-packed)
  std::vector<char> sim_;                       // 1 = simulando (padrao), 0 = fora da simulacao
  std::vector<char> keepScratch_;               // reserva de resetTo (evita alocar a cada reuso)
  mutable std::deque<std::vector<u32>> scratch_; // reserva de vetores de indices (ver lease)
  size_t scratchDepth_ = 0;
};

} // namespace tool
