// Newtoon Physics - JobSystem: pool de threads PERSISTENTE + ConstraintColorGraph (Graph Coloring para solver paral
// ParallelFor divide [0,n) em blocos; abaixo do `grain` roda no proprio thread (sem custo de sincronizacao).
#pragma once
#include "newtoon_types.hpp"

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

// Sistema de jobs da fisica: ParallelFor reparte um laco entre o pool persistente de threads.
// Sistema de jobs da fisica: ParallelFor reparte um laco entre o pool persistente de threads.
class JobSystem {
 public:
  static JobSystem& Get() { static JobSystem js; return js; }
  size_t WorkerCount() const { return workers_.size(); }

  template <class F>
  void ParallelFor(size_t n, size_t grain, F&& fn) {
    if (n == 0) return;
    if (grain == 0) throw std::invalid_argument("JobSystem::ParallelFor: grain deve ser > 0");
    const size_t threads = workers_.size() + 1;
    if (threads == 1 || n / 2 < grain) { fn(size_t{0}, n); return; }

    const size_t requiredChunks = 1 + (n - 1) / grain;
    const size_t chunks = std::min(threads, requiredChunks);
    const size_t per = 1 + (n - 1) / chunks;

    detail::ParallelBatch batch(chunks - 1);
    size_t queued = 0;
    try {
      for (size_t c = 1; c < chunks; ++c) {
        const size_t b = std::min(n, c * per), e = std::min(n, b + per);
        Push([&, b, e] {
          detail::BatchDoneGuard done{batch};
          try { if (b < e) fn(b, e); } catch (...) { batch.RecordError(); }
        });
        ++queued;
      }
    } catch (...) {
      batch.RecordError();
      batch.Forget(chunks - 1 - queued);
    }

    if (queued == chunks - 1) {
      try { fn(size_t{0}, std::min(n, per)); } catch (...) { batch.RecordError(); }
    }
    batch.Wait();
    batch.RethrowFirst();
  }

  JobSystem(const JobSystem&) = delete; JobSystem& operator=(const JobSystem&) = delete;

 private:
  JobSystem() {
    const unsigned hc = std::thread::hardware_concurrency();
    const unsigned n = hc > 1 ? std::min(hc - 1, 7u) : 0u;
    for (unsigned i = 0; i < n; ++i) workers_.emplace_back([this] { Loop(); });
  }
  ~JobSystem() { { std::lock_guard<std::mutex> l(m_); stop_ = true; } cv_.notify_all(); for (auto& t : workers_) t.join(); }
  void Push(std::function<void()> f) { { std::lock_guard<std::mutex> l(m_); q_.push(std::move(f)); } cv_.notify_one(); }
  void Loop() {
    for (;;) {
      std::function<void()> f;
      { std::unique_lock<std::mutex> l(m_); cv_.wait(l, [&] { return stop_ || !q_.empty(); }); if (stop_ && q_.empty()) return; f = std::move(q_.front()); q_.pop(); }
      f();
    }
  }
  std::vector<std::thread> workers_;
  std::queue<std::function<void()>> q_;
  std::mutex m_; std::condition_variable cv_; bool stop_ = false;
};

} // namespace Newtoon