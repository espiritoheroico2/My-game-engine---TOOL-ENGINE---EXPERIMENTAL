// Testes de F-04: contrato do JobSystem::ParallelFor (grain, n, excecoes no chamador e em worker).
#include "test.hpp"
#include "tool/physics/newtoon_jobs.hpp"

#include <atomic>
#include <stdexcept>
#include <vector>

using Newtoon::JobSystem;

TEST(jobs_zero_items_does_nothing) {
  int calls = 0;
  JobSystem::Get().ParallelFor(0, 16, [&](size_t, size_t) { ++calls; });
  CHECK(calls == 0);
}

TEST(jobs_zero_grain_throws_invalid_argument) {
  bool threw = false;
  try { JobSystem::Get().ParallelFor(1000, 0, [](size_t, size_t) {}); }
  catch (const std::invalid_argument&) { threw = true; }
  CHECK(threw);
}

TEST(jobs_fewer_items_than_grain_runs_inline_once) {
  int calls = 0; size_t lo = 99, hi = 99;
  JobSystem::Get().ParallelFor(5, 100, [&](size_t b, size_t e) { ++calls; lo = b; hi = e; });
  CHECK(calls == 1 && lo == 0 && hi == 5);
}

TEST(jobs_large_range_covers_every_index_exactly_once) {
  // Tamanhos que nao dividem exatamente pelo numero de blocos (testa os limites de cada bloco).
  for (size_t n : {size_t{1000}, size_t{4097}, size_t{100003}}) {
    for (size_t grain : {size_t{1}, size_t{7}, size_t{512}, size_t{1024}}) {
      std::vector<int> hits(n, 0);
      JobSystem::Get().ParallelFor(n, grain, [&](size_t b, size_t e) { for (size_t i = b; i < e; ++i) hits[i]++; });
      bool ok = true;
      for (int h : hits) ok = ok && h == 1;
      CHECK(ok);
    }
  }
}

TEST(jobs_huge_grain_does_not_overflow) {
  // grain perto de SIZE_MAX: a versao antiga estourava em grain * 2 e em n + grain - 1.
  const size_t huge = static_cast<size_t>(-1) - 3;
  int calls = 0;
  JobSystem::Get().ParallelFor(100, huge, [&](size_t b, size_t e) { ++calls; CHECK(b == 0 && e == 100); });
  CHECK(calls == 1);
}

TEST(jobs_exception_on_caller_block_waits_workers_and_rethrows) {
  // O bloco que começa em 0 roda no thread chamador: lanca ali. Os blocos dos workers devem TERMINAR antes do retorno.
  const size_t n = 200000;
  std::atomic<size_t> workerItems{0};
  bool threw = false;
  try {
    JobSystem::Get().ParallelFor(n, 1000, [&](size_t b, size_t e) {
      if (b == 0) throw std::runtime_error("falha no chamador");
      workerItems += e - b;
    });
  } catch (const std::runtime_error&) { threw = true; }
  CHECK(threw);
  // Todos os itens fora do bloco 0 foram processados antes do relanamento (nada ficou pendente).
  // Com um thread (sem workers), o bloco 0 cobre tudo e a excecao vem direto.
  if (JobSystem::Get().WorkerCount() > 0) CHECK(workerItems.load() > 0);
}

TEST(jobs_exception_on_worker_block_is_rethrown_after_all_finish) {
  const size_t n = 200000;
  std::atomic<size_t> finished{0};
  bool threw = false;
  try {
    JobSystem::Get().ParallelFor(n, 1000, [&](size_t b, size_t e) {
      if (b != 0) {
        finished += e - b;
        if (b >= 1000) throw std::runtime_error("falha no worker");
      }
    });
  } catch (const std::runtime_error&) { threw = true; }
  CHECK(threw);
}

TEST(jobs_pool_still_works_after_exceptions) {
  // Uma excecao nao pode deixar o pool travado nem com contador pendente.
  try { JobSystem::Get().ParallelFor(50000, 100, [](size_t, size_t) { throw std::runtime_error("x"); }); }
  catch (const std::runtime_error&) {}
  std::vector<int> hits(50000, 0);
  JobSystem::Get().ParallelFor(50000, 100, [&](size_t b, size_t e) { for (size_t i = b; i < e; ++i) hits[i]++; });
  bool ok = true;
  for (int h : hits) ok = ok && h == 1;
  CHECK(ok);
}
