// Newtoon Physics - JobSystem: pool de threads PERSISTENTE (o original criava threads por chamada via std::async).
// ParallelFor divide [0,n) em blocos; abaixo do `grain` roda no proprio thread (sem custo de sincronizacao).
#pragma once
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <functional>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <vector>

namespace Newtoon {

namespace detail {

// Estado de UMA chamada de ParallelFor. Vive na pilha do chamador, que so sai depois de Wait():
// assim os blocos enfileirados nunca usam referencias que ja morreram.
class ParallelBatch {
 public:
  // Cria o lote que espera `pending` blocos.
  explicit ParallelBatch(size_t pending) : remaining_(pending) {}

  // Um bloco terminou (com ou sem excecao). Acorda o chamador quando o ultimo termina.
  void Done() {
    if (remaining_.fetch_sub(1) == 1) { std::lock_guard<std::mutex> l(doneM_); doneCv_.notify_all(); }
  }

  // Retira `count` blocos que nunca foram enfileirados (falha ao empilhar), para o Wait() nao travar.
  void Forget(size_t count) {
    if (count == 0) return;
    if (remaining_.fetch_sub(count) == count) { std::lock_guard<std::mutex> l(doneM_); doneCv_.notify_all(); }
  }

  // Guarda a PRIMEIRA excecao. Chamado de dentro de catch(...), em qualquer thread.
  void RecordError() {
    std::lock_guard<std::mutex> l(errM_);
    if (!firstError_) firstError_ = std::current_exception();
  }

  // Espera TODOS os blocos enfileirados, sem exceder o escopo.
  void Wait() {
    std::unique_lock<std::mutex> l(doneM_);
    doneCv_.wait(l, [&] { return remaining_.load() == 0; });
  }

  // Relanca a primeira excecao registrada, se houver. So deve ser chamado depois de Wait().
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

// RAII: chama Done() ao sair do escopo, inclusive por excecao. Impede que um bloco que falhou
// deixe o contador pendente e trave o chamador para sempre.
struct BatchDoneGuard {
  ParallelBatch& batch;
  ~BatchDoneGuard() { batch.Done(); }
};

} // namespace detail

// Sistema de jobs da fisica: ParallelFor reparte um laco entre o pool persistente de threads.
class JobSystem {
 public:
  // Instancia unica do sistema de jobs (singleton).
  static JobSystem& Get() { static JobSystem js; return js; }
  // Numero de threads de trabalho criadas (sem contar o chamador).
  size_t WorkerCount() const { return workers_.size(); }

  // fn(begin, end) e chamada para blocos disjuntos. Retorna quando TODOS terminarem.
  // Contrato (F-04):
  //   - n == 0: nao faz nada;
  //   - grain == 0: lanca std::invalid_argument (antes era divisao por zero);
  //   - se fn lancar, em QUALQUER bloco (inclusive o do thread chamador), ParallelFor espera todos
  //     os blocos terminarem e so entao relanca a PRIMEIRA excecao. Nunca retorna antes disso, para
  //     que as referencias capturadas pelos blocos continuem validas.
  // Atenção: chamar ParallelFor de dentro de fn pode travar (o worker fica esperando blocos sem thread livre).
  template <class F>
  void ParallelFor(size_t n, size_t grain, F&& fn) {
    if (n == 0) return;
    if (grain == 0) throw std::invalid_argument("JobSystem::ParallelFor: grain deve ser > 0");
    const size_t threads = workers_.size() + 1;
    // "n < 2 * grain" escrito como "n / 2 < grain": evita estouro quando grain e enorme.
    if (threads == 1 || n / 2 < grain) { fn(size_t{0}, n); return; }

    // Numero de blocos sem overflow: ceil(n / grain) = 1 + (n - 1) / grain (n >= 1 aqui).
    const size_t requiredChunks = 1 + (n - 1) / grain;
    const size_t chunks = std::min(threads, requiredChunks);
    const size_t per = 1 + (n - 1) / chunks; // ceil(n / chunks)

    detail::ParallelBatch batch(chunks - 1);   // o chamador faz o bloco 0; os demais vao para workers
    size_t queued = 0;
    try {
      for (size_t c = 1; c < chunks; ++c) {
        const size_t b = std::min(n, c * per), e = std::min(n, b + per);
        Push([&, b, e] {
          detail::BatchDoneGuard done{batch};  // Done() ocorre mesmo se fn lancar
          try { if (b < e) fn(b, e); } catch (...) { batch.RecordError(); }
        });
        ++queued;
      }
    } catch (...) {
      // Falha ao enfileirar: os blocos restantes nunca vao rodar. Registra a falha e retira-os do contador.
      batch.RecordError();
      batch.Forget(chunks - 1 - queued);
    }

    // O thread chamador trabalha no bloco 0, se todos os demais foram enfileirados.
    if (queued == chunks - 1) {
      try { fn(size_t{0}, std::min(n, per)); } catch (...) { batch.RecordError(); }
    }
    batch.Wait();          // espera TODOS, mesmo depois de excecao
    batch.RethrowFirst();  // so entao propaga a primeira excecao
  }

  // Nao pode ser copiado.
  JobSystem(const JobSystem&) = delete; JobSystem& operator=(const JobSystem&) = delete;

 private:
  // Cria os workers: hardware_concurrency - 1, no maximo 7. Sem workers, tudo roda no chamador.
  JobSystem() {
    const unsigned hc = std::thread::hardware_concurrency();
    const unsigned n = hc > 1 ? std::min(hc - 1, 7u) : 0u; // no maximo 7 trabalhadores: fisica nao precisa de mais
    for (unsigned i = 0; i < n; ++i) workers_.emplace_back([this] { Loop(); });
  }
  // Encerra os workers, esperando as threads terminarem.
  ~JobSystem() { { std::lock_guard<std::mutex> l(m_); stop_ = true; } cv_.notify_all(); for (auto& t : workers_) t.join(); }
  // Enfileira uma tarefa e acorda um worker.
  void Push(std::function<void()> f) { { std::lock_guard<std::mutex> l(m_); q_.push(std::move(f)); } cv_.notify_one(); }
  // Laco de cada worker: pega tarefas da fila e as executa ate o encerramento.
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
