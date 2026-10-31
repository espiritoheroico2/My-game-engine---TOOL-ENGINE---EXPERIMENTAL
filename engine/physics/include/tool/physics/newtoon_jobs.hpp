// Newtoon Physics - JobSystem: pool de threads PERSISTENTE (o original criava threads por chamada via std::async).
// ParallelFor divide [0,n) em blocos; abaixo do `grain` roda no proprio thread (sem custo de sincronizacao).
#pragma once
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace Newtoon {

class JobSystem {
 public:
  static JobSystem& Get() { static JobSystem js; return js; }
  size_t WorkerCount() const { return workers_.size(); }

  // fn(begin, end) e chamada para blocos disjuntos. Retorna quando TODOS terminarem.
  template <class F>
  void ParallelFor(size_t n, size_t grain, F&& fn) {
    const size_t threads = workers_.size() + 1;
    if (n == 0) return;
    if (threads == 1 || n < grain * 2) { fn(size_t{0}, n); return; }
    const size_t chunks = std::min(threads, (n + grain - 1) / grain);
    const size_t per = (n + chunks - 1) / chunks;
    std::atomic<size_t> remaining{chunks - 1};
    std::mutex doneM; std::condition_variable doneCv;
    for (size_t c = 1; c < chunks; ++c) {
      const size_t b = c * per, e = std::min(n, b + per);
      Push([&, b, e] { if (b < e) fn(b, e); if (remaining.fetch_sub(1) == 1) { std::lock_guard<std::mutex> l(doneM); doneCv.notify_all(); } });
    }
    fn(size_t{0}, std::min(n, per)); // o thread chamador tambem trabalha
    std::unique_lock<std::mutex> l(doneM);
    doneCv.wait(l, [&] { return remaining.load() == 0; });
  }

  JobSystem(const JobSystem&) = delete; JobSystem& operator=(const JobSystem&) = delete;

 private:
  JobSystem() {
    const unsigned hc = std::thread::hardware_concurrency();
    const unsigned n = hc > 1 ? std::min(hc - 1, 7u) : 0u; // no maximo 7 trabalhadores: fisica nao precisa de mais
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
