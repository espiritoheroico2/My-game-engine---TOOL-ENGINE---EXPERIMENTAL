// ToolEngine - parallelFor com pool de threads PERSISTENTE (nao cria threads por chamada: criar/juntar threads a cada quadro custa ~0,1-0,5 ms por thread, principalmente no Windows).
// fn(begin, end) e chamada para blocos disjuntos de [0, n); retorna quando todos terminarem. Abaixo de `grain` roda no thread chamador.
#pragma once
#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace tool {

class ParallelPool {
public:
  static ParallelPool& get() { static ParallelPool p; return p; }
  size_t threads() const { return workers_.size() + 1; }

  template <class F>
  void run(size_t n, size_t grain, F&& fn) {
    if (n == 0) return;
    const size_t chunks = std::min(threads(), std::max<size_t>(1, n / std::max<size_t>(1, grain)));
    if (chunks <= 1) { fn(size_t{0}, n); return; }
    std::lock_guard<std::mutex> serial(callM_); // uma chamada por vez (o editor chama so da thread principal)
    const size_t per = (n + chunks - 1) / chunks;
    std::function<void(size_t)> job = [&](size_t c) { const size_t b = c * per, e = std::min(n, b + per); if (b < e) fn(b, e); };
    { std::lock_guard<std::mutex> l(m_); job_ = &job; total_ = chunks; next_ = 1; pending_ = chunks - 1; }
    cv_.notify_all();
    job(0); // o thread chamador tambem trabalha
    std::unique_lock<std::mutex> l(m_);
    doneCv_.wait(l, [&] { return pending_ == 0; });
    job_ = nullptr; // so aqui: nenhum worker ainda segura o ponteiro (cada bloco e reivindicado e concluido sob o mutex)
  }

  ParallelPool(const ParallelPool&) = delete; ParallelPool& operator=(const ParallelPool&) = delete;
  ~ParallelPool() { { std::lock_guard<std::mutex> l(m_); stop_ = true; } cv_.notify_all(); for (auto& t : workers_) t.join(); }

private:
  ParallelPool() {
    const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
    const unsigned n = std::min(hw, 8u) - 1;
    for (unsigned i = 0; i < n; ++i) workers_.emplace_back([this] { loop(); });
  }
  void loop() {
    std::unique_lock<std::mutex> l(m_);
    for (;;) {
      cv_.wait(l, [&] { return stop_ || (job_ && next_ < total_); });
      if (stop_) return;
      const size_t c = next_++; std::function<void(size_t)>* job = job_;
      l.unlock(); (*job)(c); l.lock();
      if (--pending_ == 0) doneCv_.notify_all();
    }
  }

  std::vector<std::thread> workers_;
  std::mutex m_, callM_; std::condition_variable cv_, doneCv_;
  std::function<void(size_t)>* job_ = nullptr; size_t total_ = 0, next_ = 0, pending_ = 0; bool stop_ = false;
};

template <class F> inline void parallelFor(size_t n, size_t grain, F&& fn) { ParallelPool::get().run(n, grain, std::forward<F>(fn)); }

} // namespace tool
