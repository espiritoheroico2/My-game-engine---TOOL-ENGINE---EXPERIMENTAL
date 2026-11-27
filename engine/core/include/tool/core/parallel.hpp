// ToolEngine - parallelFor com pool de threads PERSISTENTE (nao cria threads por chamada: criar/juntar threads a cada quadro custa ~0,1-0,5 ms por thread, principalmente no Windows).
// fn(begin, end) e chamada para blocos disjuntos de [0, n); retorna quando todos terminarem. Abaixo de `grain` roda no thread chamador.
// Escala com os nucleos: a carga vira ate kOversub blocos por thread e cada thread pega o proximo bloco livre,
// entao um bloco lento nao deixa os outros nucleos parados. Excecoes sao capturadas e relancadas no chamador.
//
// Custo por chamada: o job fica num slot fixo do pool, identificado por uma epoca. Os workers entram e saem do job
// so com atomicos (sem mutex); o mutex serve apenas para dormir/acordar. Antes, cada worker travava o mesmo mutex
// para pegar o job e cada bloco travava para contar: com 20 threads, uma chamada VAZIA custava 100-400 us, e o
// solver da fisica faz ~140 chamadas por passo (medido em 2026-11-26).
#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#endif

namespace tool {

namespace detail {
// Pausa curta dentro de uma espera ativa (avisa a CPU que a thread esta girando).
inline void cpuRelax() {
#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
  _mm_pause();
#else
  std::this_thread::yield();
#endif
}
// Gira ate pred() ficar verdadeiro ou o prazo acabar. Devolve pred() no fim.
template <class P> inline bool spinFor(std::chrono::microseconds limit, P&& pred) {
  const auto end = std::chrono::steady_clock::now() + limit;
  for (unsigned i = 0;; ++i) {
    if (pred()) return true;
    if ((i & 63) == 63 && std::chrono::steady_clock::now() >= end) return pred();
    cpuRelax();
  }
}
} // namespace detail

// Pool de threads persistente (singleton) usado pelo parallelFor.
// Usa hardware_concurrency - 1 workers, limitado a kMaxThreads threads no total (contando a thread chamadora).
class ParallelPool {
public:
  // Teto de threads no total (chamador + workers).
  static constexpr unsigned kMaxThreads = 64;
  // Blocos por thread: mais blocos que threads permite balancear carga irregular.
  static constexpr size_t kOversub = 4;
  // Espera ativa antes de dormir. Lacos como o solver da fisica fazem centenas de chamadas seguidas; dormir na
  // condition_variable entre elas custa dezenas de us por chamada (acordar a thread pelo sistema operacional).
  // So vale em rajada longa: os workers giram apenas depois de kBurstCalls chamadas seguidas, cada uma comecando
  // menos de kBurstGap depois do fim da anterior. A janela precisa cobrir uma chamada inteira (quem termina cedo
  // espera o bloco mais lento). Girando sempre, o editor parado gastava ~165% de um nucleo; com rajadas de qualquer
  // tamanho, ~63% (medido em 2026-11-26; pool antigo: 28%).
  static constexpr std::chrono::microseconds kSpin{100};
  static constexpr std::chrono::microseconds kBurstGap{100};
  static constexpr unsigned kBurstCalls = 8;

  // Instancia unica do pool.
  static ParallelPool& get() { static ParallelPool p; return p; }
  // Numero de threads que trabalham em um parallelFor (workers + thread chamadora).
  size_t threads() const { return workers_.size() + 1; }

  // Divide [0, n) em blocos e executa fn(begin, end) em paralelo. Abaixo de `grain` roda tudo na thread chamadora.
  // Nao chame run/parallelFor de dentro de fn: a chamada aninhada espera o pool e trava.
  template <class F>
  void run(size_t n, size_t grain, F&& fn) {
    if (n == 0) return;
    const size_t safeGrain = std::max<size_t>(1, grain);
    const size_t maxChunks = n / safeGrain + (n % safeGrain != 0 ? 1 : 0);   // ceil(n / grain), sem overflow em n perto do maximo
    const size_t chunks = std::min(threads() * kOversub, maxChunks);
    if (threads() == 1 || chunks <= 1) { fn(size_t{0}, n); return; }
    std::lock_guard<std::mutex> serial(callM_); // uma chamada por vez (o editor chama so da thread principal)
    const size_t per = (n + chunks - 1) / chunks;
    std::function<void(size_t)> body = [&](size_t c) {
      const size_t b = c * per, e = std::min(n, b + per);
      if (b < e) fn(b, e);
    };
    // Prepara o slot (nenhum worker esta dentro: a chamada anterior esperou active_ == 0) e abre a epoca (impar).
    fn_ = &body;
    total_ = chunks;
    err_ = nullptr;
    next_.store(0, std::memory_order_relaxed);
    done_.store(0, std::memory_order_relaxed);
    burstLen_ = std::chrono::steady_clock::now() - lastEnd_ < kBurstGap ? burstLen_ + 1 : 0;
    burst_.store(burstLen_ >= kBurstCalls, std::memory_order_relaxed);
    epoch_.fetch_add(1, std::memory_order_seq_cst);
    if (sleepers_.load(std::memory_order_seq_cst) > 0) { std::lock_guard<std::mutex> l(m_); cv_.notify_all(); }
    drain(); // o thread chamador tambem pega blocos
    // Espera os blocos dos workers: gira um pouco, depois dorme no doneCv_.
    auto allDone = [&] { return done_.load(std::memory_order_acquire) == total_; };
    if (!detail::spinFor(kSpin, allDone)) {
      std::unique_lock<std::mutex> l(m_);
      doneCv_.wait(l, allDone);
    }
    // Fecha a epoca (par) e espera sair quem ainda estiver entrando/saindo, para o slot e `body` poderem ser reusados.
    // Worker: active_++ e depois confere epoch_; aqui: epoch_++ e depois confere active_ (seq_cst nos dois lados):
    // ou o chamador ve o worker em active_, ou o worker ve a epoca fechada e sai sem tocar no slot.
    epoch_.fetch_add(1, std::memory_order_seq_cst);
    // (Um worker preempcionado entre active_++ e a conferencia pode demorar: depois de um tempo, cede a CPU.)
    for (unsigned i = 0; active_.load(std::memory_order_seq_cst) != 0; ++i) {
      if (i < 4096) detail::cpuRelax(); else std::this_thread::yield();
    }
    lastEnd_ = std::chrono::steady_clock::now();
    std::exception_ptr err;
    { std::lock_guard<std::mutex> l(errM_); err = err_; err_ = nullptr; }
    if (err) std::rethrow_exception(err);
  }

  // Pool nao pode ser copiado.
  ParallelPool(const ParallelPool&) = delete; ParallelPool& operator=(const ParallelPool&) = delete;
  // Encerra os workers e aguarda suas threads terminarem.
  ~ParallelPool() {
    { std::lock_guard<std::mutex> l(m_); stop_.store(true, std::memory_order_seq_cst); }
    cv_.notify_all();
    for (auto& t : workers_) t.join();
  }

private:
  // Cria os workers (hardware_concurrency - 1, limitado a kMaxThreads - 1).
  ParallelPool() {
    const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
    const unsigned n = std::min(hw, kMaxThreads) - 1;
    for (unsigned i = 0; i < n; ++i) workers_.emplace_back([this] { loop(); });
  }

  // Reivindica blocos do job aberto ate acabarem. Uma excecao de bloco e guardada e o job segue, para todos terminarem antes do relancamento.
  void drain() {
    const size_t total = total_;
    for (;;) {
      const size_t c = next_.fetch_add(1, std::memory_order_relaxed);
      if (c >= total) return;
      try { (*fn_)(c); } catch (...) {
        std::lock_guard<std::mutex> l(errM_);
        if (!err_) err_ = std::current_exception();
      }
      if (done_.fetch_add(1, std::memory_order_acq_rel) + 1 == total) {
        std::lock_guard<std::mutex> l(m_);   // o chamador pode estar dormindo no doneCv_
        doneCv_.notify_all();
      }
    }
  }

  // Laco de cada worker: entra em cada epoca aberta uma vez, ajuda a drena-la; sem trabalho, gira kSpin e dorme.
  void loop() {
    std::uint64_t lastEpoch = 0;   // ultima epoca aberta em que este worker entrou
    for (;;) {
      if (stop_.load(std::memory_order_acquire)) return;
      const std::uint64_t e = epoch_.load(std::memory_order_acquire);
      if ((e & 1u) && e != lastEpoch) {
        lastEpoch = e;
        active_.fetch_add(1, std::memory_order_seq_cst);
        if (epoch_.load(std::memory_order_seq_cst) == e) drain();   // epoca ainda aberta: o slot e valido
        active_.fetch_sub(1, std::memory_order_seq_cst);
        continue;
      }
      // Sem job novo: em rajada, gira um pouco esperando a proxima epoca; depois (ou fora de rajada) dorme.
      auto changed = [&] { return stop_.load(std::memory_order_acquire) || epoch_.load(std::memory_order_acquire) != e; };
      if (burst_.load(std::memory_order_relaxed) && detail::spinFor(kSpin, changed)) continue;
      std::unique_lock<std::mutex> l(m_);
      sleepers_.fetch_add(1, std::memory_order_seq_cst);   // o chamador le sleepers_ depois de abrir a epoca
      cv_.wait(l, [&] { return stop_.load(std::memory_order_seq_cst) || epoch_.load(std::memory_order_seq_cst) != e; });
      sleepers_.fetch_sub(1, std::memory_order_relaxed);
    }
  }

  std::vector<std::thread> workers_;
  std::mutex m_, callM_, errM_;
  std::condition_variable cv_, doneCv_;
  // Slot do job atual: escrito pelo chamador com a epoca fechada (nenhum worker dentro), lido pelos workers com ela aberta.
  std::function<void(size_t)>* fn_ = nullptr;
  size_t total_ = 0;
  std::exception_ptr err_;                       // (errM_) primeira excecao lancada por um bloco
  std::atomic<size_t> next_{0};                  // proximo bloco a reivindicar
  std::atomic<size_t> done_{0};                  // blocos concluidos
  std::atomic<size_t> active_{0};                // workers entre a entrada e a saida de uma epoca
  std::atomic<std::uint64_t> epoch_{0};          // impar = job aberto, par = fechado
  std::atomic<unsigned> sleepers_{0};            // workers dormindo (ou indo dormir) no cv_
  std::atomic<bool> stop_{false};
  std::atomic<bool> burst_{false};               // a chamada atual veio logo depois da anterior (workers podem girar)
  std::chrono::steady_clock::time_point lastEnd_{};    // (callM_) quando a ultima chamada terminou
  unsigned burstLen_ = 0;                        // (callM_) chamadas seguidas com intervalo curto
};

// Atalho: executa fn(begin, end) em paralelo sobre [0, n) usando o ParallelPool global. Mesma semantica de ParallelPool::run.
template <class F> inline void parallelFor(size_t n, size_t grain, F&& fn) { ParallelPool::get().run(n, grain, std::forward<F>(fn)); }

} // namespace tool
