// Newtoon Physics - JobSystem: pool de threads PERSISTENTE + ConstraintColorGraph (Graph Coloring para solver paral
// ParallelFor divide [0,n) em blocos; abaixo do `grain` roda no proprio thread (sem custo de sincronizacao).
#pragma once
#include "newtoon_types.hpp"
#include "tool/core/parallel.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <vector>

namespace Newtoon {

namespace detail {

class ParallelBatch {
public:
explicit ParallelBatch(size_t pending) : remaining_(pending) {}

void Done() {
 if (remaining_.fetch_sub(1) == 1) { std::lock_guard<std::mutex> l(doneM_); doneCv_.notify_all(); }
}

void Forget(size_t count) {
 if (count == 0) return;
 if (remaining_.fetch_sub(count) == count) { std::lock_guard<std::mutex> l(doneM_); doneCv_.notify_all(); }
}

void RecordError() {
 std::lock_guard<std::mutex> l(errM_);
 if (!firstError_) firstError_ = std::current_exception();
}

void Wait() {
 std::unique_lock<std::mutex> l(doneM_);
 doneCv_.wait(l, [&] { return remaining_.load() == 0; });
}

void RethrowFirst() {
 std::exception_ptr e;
 { std::lock_guard<std::mutex> l(errM_); e = firstError_; }
 if (e) std::rethrow_exception(e);
}

private:
std::atomic<size_t> remaining_;
std::mutex doneM_; std::condition_variable doneCv_;
std::mutex errM_; std::exception_ptr firstError_;
};

struct BatchDoneGuard {
ParallelBatch& batch;
~BatchDoneGuard() { batch.Done(); }
};

} // namespace detail

// PARTICIONAMENTO POR CORES (Graph Coloring - inspirado no Box2D v3)
// Motivo: Garante que contatos na mesma cor nunca compartilhem o mesmo corpo dinamico,
// permitindo que o solver execute em paralelo via JobSystem sem mutexes nem data races.
class ConstraintColorGraph {
public:
static constexpr uint32_t kMaxColors = 16;
std::vector<uint32_t> buckets[kMaxColors];

template <typename ContactT>
void Build(const std::vector<ContactT>& contacts, const std::vector<Rigidbody>& rb, size_t bodyCount) {
 const size_t wordsPerColor = (bodyCount + 63) / 64;
 bitsets_.assign(kMaxColors * wordsPerColor, 0ULL);
 for (uint32_t c = 0; c < kMaxColors; ++c) {
   buckets[c].clear();
 }

   const uint32_t contactCount = static_cast<uint32_t>(contacts.size());
   for (uint32_t i = 0; i < contactCount; ++i) {
     const Entity a = contacts[i].a;
     const Entity b = contacts[i].b;

       // Corpos estaticos/cinematicos nao sofrem alteracao de velocidade por impulso,
       // logo nao geram conflito de escrita e nao precisam marcar bits no grafo.
       const bool dynA = (a < bodyCount) && rb[a].IsDynamic();
       const bool dynB = (b < bodyCount) && rb[b].IsDynamic();

       uint32_t assigned = kMaxColors - 1; // Ultimo balde serve como fallback sequencial se saturar
       for (uint32_t c = 0; c < kMaxColors - 1; ++c) {
         uint64_t* bits = &bitsets_[c * wordsPerColor];
         const bool conflictA = dynA && ((bits[a >> 6] & (1ULL << (a & 63))) != 0);
         const bool conflictB = dynB && ((bits[b >> 6] & (1ULL << (b & 63))) != 0);

         if (!conflictA && !conflictB) {
           if (dynA) bits[a >> 6] |= (1ULL << (a & 63));
           if (dynB) bits[b >> 6] |= (1ULL << (b & 63));
           assigned = c;
           break;
         }
       }
       buckets[assigned].push_back(i);
   }
}

private:
std::vector<uint64_t> bitsets_;
};

// Sistema de jobs da fisica: ParallelFor reparte um laco entre o pool global de threads (tool::ParallelPool).
// Nao cria um segundo conjunto de threads: a fisica e o resto da engine dividem os mesmos nucleos.
class JobSystem {
 public:
  static JobSystem& Get() { static JobSystem js; return js; }
  // Threads que trabalham em um ParallelFor (workers + chamador).
  size_t WorkerCount() const { return tool::ParallelPool::get().threads() - 1; }

  template <class F>
  void ParallelFor(size_t n, size_t grain, F&& fn) {
    if (n == 0) return;
    if (grain == 0) throw std::invalid_argument("JobSystem::ParallelFor: grain deve ser > 0");
    tool::parallelFor(n, grain, std::forward<F>(fn));
  }

  JobSystem(const JobSystem&) = delete; JobSystem& operator=(const JobSystem&) = delete;

 private:
  JobSystem() = default;
};

} // namespace Newtoon