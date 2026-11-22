// ToolEngine - DebugTool: diagnosticos com codigo, zonas de desempenho,
// orcamentos de tempo, watchdog e macros por nivel (TOOL_DEBUG_LEVEL 0/1/2).
#pragma once
#include "tool/core/log.hpp"
#include "tool/core/types.hpp"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <functional>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace tool {

// Gravidade de um diagnostico: Info, Warning (aviso), Error (erro) ou Fatal (aborta, se configurado).
enum class Severity : u8 { Info, Warning, Error, Fatal };

// Codigos estaveis: E = erro, W = aviso. Documentados em docs/diagnostics.md.
namespace diag {
inline constexpr const char* kExpectFailed = "TOOL-E001";
inline constexpr const char* kStaleHandle = "TOOL-E002";
inline constexpr const char* kEventLoop = "TOOL-E003";
inline constexpr const char* kEventDepth = "TOOL-E004";
inline constexpr const char* kWatchdog = "TOOL-E005";
inline constexpr const char* kTagOverflow = "TOOL-E006";
inline constexpr const char* kBudgetFrame = "TOOL-W101";
inline constexpr const char* kBudgetZone = "TOOL-W102";
inline constexpr const char* kEventConflict = "TOOL-W103";
inline constexpr const char* kEventDup = "TOOL-W104";
inline constexpr const char* kPoolPressure = "TOOL-W105";
} // namespace diag

// Diagnostico registrado: codigo estavel, gravidade, mensagem e onde ocorreu (arquivo, linha, quadro).
struct Diagnostic {
  std::string code;
  Severity severity = Severity::Info;
  std::string message;
  const char* file = "";
  int line = 0;
  u64 frame = 0;
};

// Estatisticas de uma zona de desempenho: chamadas, tempos (ms) e quantas vezes passou do orcamento.
struct ZoneStats {
  u64 calls = 0;
  double totalMs = 0, maxMs = 0, lastMs = 0;
  double budgetMs = 0; // 0 = sem orcamento
  u64 overBudget = 0;
};

// Ferramenta de depuracao (singleton): registra diagnosticos, mede zonas e quadros, e roda um watchdog.
class DebugTool {
public:
  using Handler = std::function<void(const Diagnostic&)>;

  // Instancia unica da ferramenta.
  static DebugTool& get() {
    static DebugTool inst;
    return inst;
  }

  // Para o watchdog ao destruir a ferramenta.
  ~DebugTool() { stopWatchdog(); }

  // --- diagnosticos ---------------------------------------------------------
  // Registra um diagnostico (historico, contadores, log e handler). Severity::Fatal aborta o programa se setFatalAborts estiver ligado. file/line normalmente vem das macros.
  void report(const char* code, Severity sev, std::string msg, const char* file = "", int line = 0) {
    Diagnostic d{code, sev, std::move(msg), file, line, frame_.load()};
    {
      std::lock_guard lk(mu_);
      ++counts_[static_cast<int>(sev)];
      history_.push_back(d);
      if (history_.size() > kHistory) history_.pop_front();
    }
    const LogLevel ll = sev == Severity::Info ? LogLevel::Info : sev == Severity::Warning ? LogLevel::Warn : LogLevel::Error;
    Logger::get().log(ll, d.code + " " + d.message);
    if (handler_) handler_(d);
    if (sev == Severity::Fatal && fatalIsAbort_) std::abort();
  }

  // Define a funcao chamada a cada diagnostico (ex.: para mostrar no editor). Passe vazio para remover.
  void setHandler(Handler h) { handler_ = std::move(h); }
  // Liga ou desliga o abort em diagnosticos Fatal (ligado por padrao).
  void setFatalAborts(bool v) { fatalIsAbort_ = v; }
  // Quantidade de diagnosticos registrados com a gravidade s. Atenção: le o contador sem o mutex.
  size_t count(Severity s) const { return counts_[static_cast<int>(s)]; }
  // Copia do historico recente de diagnosticos (limite de 512).
  std::vector<Diagnostic> history() const {
    std::lock_guard lk(mu_);
    return {history_.begin(), history_.end()};
  }
  // True se algum diagnostico no historico recente tem o codigo dado.
  bool hasCode(std::string_view code) const {
    std::lock_guard lk(mu_);
    for (auto& d : history_)
      if (d.code == code) return true;
    return false;
  }
  // Limpa historico, zonas e contadores.
  void reset() {
    std::lock_guard lk(mu_);
    history_.clear();
    zones_.clear();
    for (auto& c : counts_) c = 0;
  }

  // --- quadros e orcamentos ----------------------------------------------------
  // Orcamento do quadro em ms; 0 desliga o aviso.
  void setFrameBudgetMs(double ms) { frameBudgetMs_ = ms; }
  // Orcamento em ms da zona com o nome dado; 0 desliga.
  void setZoneBudgetMs(const std::string& zone, double ms) {
    std::lock_guard lk(mu_);
    zones_[zone].budgetMs = ms;
  }

  // Marca o inicio de um quadro (tambem atualiza o heartbeat do watchdog).
  void beginFrame() {
    frameStart_ = Clock::now();
    heartbeat();
  }
  // Marca o fim do quadro: mede o tempo, avisa se passou do orcamento e incrementa o numero do quadro.
  void endFrame() {
    const double ms = std::chrono::duration<double, std::milli>(Clock::now() - frameStart_).count();
    lastFrameMs_ = ms;
    if (frameBudgetMs_ > 0 && ms > frameBudgetMs_)
      report(diag::kBudgetFrame, Severity::Warning, "quadro levou " + std::to_string(ms) + " ms (orcamento " + std::to_string(frameBudgetMs_) + " ms)");
    ++frame_;
  }
  // Duracao do ultimo quadro, em ms.
  double lastFrameMs() const { return lastFrameMs_; }
  // Numero do quadro atual.
  u64 frame() const { return frame_.load(); }

  // Registra uma medicao de ms para a zona name e avisa se passou do orcamento.
  void recordZone(const std::string& name, double ms) {
    double budget = 0;
    {
      std::lock_guard lk(mu_);
      auto& z = zones_[name];
      ++z.calls;
      z.totalMs += ms;
      z.lastMs = ms;
      if (ms > z.maxMs) z.maxMs = ms;
      budget = z.budgetMs;
      if (budget > 0 && ms > budget) ++z.overBudget;
    }
    if (budget > 0 && ms > budget)
      report(diag::kBudgetZone, Severity::Warning, "zona '" + name + "' levou " + std::to_string(ms) + " ms (orcamento " + std::to_string(budget) + " ms)");
  }
  // Estatisticas da zona name (copia). Zona inexistente devolve zeros.
  ZoneStats zone(const std::string& name) const {
    std::lock_guard lk(mu_);
    auto it = zones_.find(name);
    return it == zones_.end() ? ZoneStats{} : it->second;
  }

  // --- watchdog ---------------------------------------------------------------
  // Marca que o laco principal esta vivo (o watchdog so dispara se isso parar).
  void heartbeat() { lastBeat_.store(Clock::now().time_since_epoch().count()); }

  // Dispara TOOL-E005 se o laço principal ficar parado alem de `timeoutMs`.
  void startWatchdog(int timeoutMs) {
    stopWatchdog();
    heartbeat();
    stop_ = false;
    wd_ = std::thread([this, timeoutMs] {
      std::unique_lock lk(wdMu_);
      bool fired = false;
      while (!stop_) {
        wdCv_.wait_for(lk, std::chrono::milliseconds((std::max)(1, timeoutMs / 4)));
        if (stop_) break;
        const auto last = Clock::time_point(Clock::duration(lastBeat_.load()));
        const auto idle = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - last).count();
        if (idle > timeoutMs && !fired) {
          fired = true;
          report(diag::kWatchdog, Severity::Error, "laco principal parado ha " + std::to_string(idle) + " ms");
        } else if (idle <= timeoutMs) {
          fired = false;
        }
      }
    });
  }
  // Para a thread do watchdog e espera ela terminar.
  void stopWatchdog() {
    {
      std::lock_guard lk(wdMu_);
      stop_ = true;
    }
    wdCv_.notify_all();
    if (wd_.joinable()) wd_.join();
  }

private:
  using Clock = std::chrono::steady_clock;
  static constexpr size_t kHistory = 512;

  mutable std::mutex mu_;
  std::deque<Diagnostic> history_;
  std::unordered_map<std::string, ZoneStats> zones_;
  size_t counts_[4] = {0, 0, 0, 0};
  Handler handler_;
  bool fatalIsAbort_ = true;

  std::atomic<u64> frame_{0};
  Clock::time_point frameStart_ = Clock::now();
  double frameBudgetMs_ = 0, lastFrameMs_ = 0;

  std::atomic<Clock::rep> lastBeat_{0};
  std::thread wd_;
  std::mutex wdMu_;
  std::condition_variable wdCv_;
  bool stop_ = true;
};

// Atalho para DebugTool::get().
inline DebugTool& debug() { return DebugTool::get(); }

// Zona RAII: mede o escopo e confere o orcamento.
class ZoneScope {
public:
  // Inicia a medicao da zona com o nome dado.
  explicit ZoneScope(std::string name) : name_(std::move(name)), t0_(std::chrono::steady_clock::now()) {}
  // Encerra a medicao e registra o tempo decorrido.
  ~ZoneScope() {
    debug().recordZone(name_, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0_).count());
  }
  ZoneScope(const ZoneScope&) = delete;
  ZoneScope& operator=(const ZoneScope&) = delete;

private:
  std::string name_;
  std::chrono::steady_clock::time_point t0_;
};

} // namespace tool

// --- macros por nivel ---------------------------------------------------------
// Nivel 0: tudo vira no-op (zero custo). Nivel 1: EXPECT ativo. Nivel 2: + zonas.
#define TOOL_CAT2(a, b) a##b
#define TOOL_CAT(a, b) TOOL_CAT2(a, b)

#if TOOL_DEBUG_LEVEL >= 1
#define TOOL_EXPECT(cond, msg)                                                                                         \
  do {                                                                                                                 \
    if (!(cond))                                                                                                       \
      ::tool::debug().report(::tool::diag::kExpectFailed, ::tool::Severity::Error, std::string(msg) + " [" #cond "]", \
                             __FILE__, __LINE__);                                                                      \
  } while (0)
#else
#define TOOL_EXPECT(cond, msg) ((void)0)
#endif

#if TOOL_DEBUG_LEVEL >= 2
#define TOOL_ZONE(name) ::tool::ZoneScope TOOL_CAT(tool_zone_, __LINE__)(name)
#else
#define TOOL_ZONE(name) ((void)0)
#endif
