// ToolEngine - parallelFor com pool de threads PERSISTENTE (nao cria threads por chamada: criar/juntar threads a cada quadro custa ~0,1-0,5 ms por thread, principalmente no Windows).
// fn(begin, end) e chamada para blocos disjuntos de [0, n); retorna quando todos terminarem. Abaixo de `grain` roda no thread chamador.
// Escala com os nucleos: a carga vira ate kOversub blocos por thread e cada thread pega o proximo bloco livre,
// entao um bloco lento nao deixa os outros nucleos parados. Excecoes sao capturadas e relancadas no chamador.
#pragma once
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace tool {

// Pool de threads persistente (singleton) usado pelo parallelFor.
// Usa hardware_concurrency - 1 workers, limitado a kMaxThreads threads no total (contando a thread chamadora).
class ParallelPool {
public:
  // Teto de threads no total (chamador + workers).
  static constexpr unsigned kMaxThreads = 64;
  // Blocos por thread: mais blocos que threads permite balancear carga irregular.
  static constexpr size_t kOversub = 4;

  // Instancia unica do pool.
  static ParallelPool& get() { static ParallelPool p; return p; }
  // Numero de threads que trabalham em um parallelFor (workers + thread chamadora).
  size_t threads() const { return workers_.size() + 1; }

  // Divide [0, n) em blocos e executa fn(begin, end) em paralelo. Abaixo de `grain` roda tudo na thread chamadora.
  // Nao chame run/parallelFor de dentro de fn: a chamada aninhada espera o pool e trava.
  template <class F>
  void run(size_t n, size_t grain, F&& fn) {
    if (n == 0) return;
    const size_t maxChunks = std::max<size_t>(1, n / std::max<size_t>(1, grain));
    const size_t chunks = std::min(threads() * kOversub, maxChunks);
    if (threads() == 1 || chunks <= 1) { fn(size_t{0}, n); return; }
    std::lock_guard<std::mutex> serial(callM_); // uma chamada por vez (o editor chama so da thread principal)
    const size_t per = (n + chunks - 1) / chunks;
    std::function<void(size_t)> body = [&](size_t c) {
      const size_t b = c * per, e = std::min(n, b + per);
      if (b < e) fn(b, e);
    };
    Job job(&body, chunks);
    { std::lock_guard<std::mutex> l(m_); job_ = &job; }
    cv_.notify_all();
    drain(job); // o thread chamador tambem pega blocos
    std::exception_ptr err;
    {
      std::unique_lock<std::mutex> l(m_);
      // Espera todos os blocos E todos os workers que entraram no job (para o job poder sair da pilha com seguranca).
      doneCv_.wait(l, [&] { return job.done == job.total && job.active == 0; });
      job_ = nullptr;
      err = job.err;
    }
    if (err) std::rethrow_exception(err);
  }

  // Pool nao pode ser copiado.
  ParallelPool(const ParallelPool&) = delete; ParallelPool& operator=(const ParallelPool&) = delete;
  // Encerra os workers e aguarda suas threads terminarem.
  ~ParallelPool() { { std::lock_guard<std::mutex> l(m_); stop_ = true; } cv_.notify_all(); for (auto& t : workers_) t.join(); }

private:
  // Estado de uma chamada de run. Campos marcados (m_) so sao lidos/escritos com o mutex m_ travado.
  struct Job {
    Job(std::function<void(size_t)>* f, size_t t) : fn(f), total(t) {}
    std::function<void(size_t)>* fn;
    size_t total;
    std::atomic<size_t> next{0};   // proximo bloco a reivindicar (sem lock)
    size_t done = 0;               // (m_) blocos concluidos
    size_t active = 0;             // (m_) workers dentro de drain() deste job
    std::exception_ptr err;        // (m_) primeira excecao lancada por um bloco
  };

  // Cria os workers (hardware_concurrency - 1, limitado a kMaxThreads - 1).
  ParallelPool() {
    const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
    const unsigned n = std::min(hw, kMaxThreads) - 1;
    for (unsigned i = 0; i < n; ++i) workers_.emplace_back([this] { loop(); });
  }

  // Reivindica blocos do job ate acabarem. Uma excecao de bloco e guardada e o job segue, para todos terminarem antes do relancamento.
  void drain(Job& j) {
    for (;;) {
      const size_t c = j.next.fetch_add(1, std::memory_order_relaxed);
      if (c >= j.total) return;
      std::exception_ptr ex;
      try { (*j.fn)(c); } catch (...) { ex = std::current_exception(); }
      std::lock_guard<std::mutex> l(m_);
      if (ex && !j.err) j.err = ex;
      if (++j.done == j.total) doneCv_.notify_all();
    }
  }

  // Laco de cada worker: espera um job com blocos livres, ajuda a drena-lo e volta a esperar.
  void loop() {
    std::unique_lock<std::mutex> l(m_);
    for (;;) {
      cv_.wait(l, [&] { return stop_ || (job_ && job_->next.load() < job_->total); });
      if (stop_) return;
      Job* j = job_;
      ++j->active;
      l.unlock();
      drain(*j);
      l.lock();
      if (--j->active == 0 && j->done == j->total) doneCv_.notify_all();
    }
  }

  std::vector<std::thread> workers_;
  std::mutex m_, callM_;
  std::condition_variable cv_, doneCv_;
  Job* job_ = nullptr;   // (m_) job atual, ou nullptr
  bool stop_ = false;    // (m_)
};

// Atalho: executa fn(begin, end) em paralelo sobre [0, n) usando o ParallelPool global. Mesma semantica de ParallelPool::run.
template <class F> inline void parallelFor(size_t n, size_t grain, F&& fn) { ParallelPool::get().run(n, grain, std::forward<F>(fn)); }

} // namespace tool
