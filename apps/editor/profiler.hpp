// ToolEngine Editor - coleta de dados do Profiler (tempo por zona, historico de quadros, contadores, memoria).
//
// Como usar no codigo:   { editor::ZoneTimer z(prof, editor::Zone::SceneRender); ...trabalho...; }
// O custo de medir e 2 leituras do relogio por zona (~50 ns): pode ficar sempre ligado.
// Estatisticas calculadas a cada quadro sobre a janela dos ultimos kHistory quadros (sem alocar memoria).
#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>

#if defined(_WIN32)
#define PSAPI_VERSION 2   // GetProcessMemoryInfo vem do kernel32 (nenhuma biblioteca extra para linkar)
#include <windows.h>
#include <psapi.h>
#endif

namespace editor {

enum class Zone : int { Events, Update, Hierarchy, SceneRender, GameRender, PostFx, Ui, Present, Count };
inline const char* zoneName(Zone z) {
  static const char* n[] = {"Events + input", "Update (audio, pool)", "Panels (hierarchy, inspector...)", "Scene view render", "Game view render", "Post-processing", "UI draw (ImGui)", "Present + frame limiter"};
  return n[static_cast<int>(z)];
}

// Memoria residente do processo em MB (0 se a plataforma nao for suportada).
inline double processMemoryMB() {
#if defined(_WIN32)
  PROCESS_MEMORY_COUNTERS pmc{};
  if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc)) return static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
  return 0.0;
#elif defined(__linux__)
  long total = 0, resident = 0;
  if (FILE* f = std::fopen("/proc/self/statm", "r")) { if (std::fscanf(f, "%ld %ld", &total, &resident) != 2) resident = 0; std::fclose(f); }
  return static_cast<double>(resident) * 4096.0 / (1024.0 * 1024.0);
#else
  return 0.0;
#endif
}

struct FrameCounters {            // zerados a cada quadro
  unsigned views = 0;             // quantas imagens 3D foram renderizadas (Scene + Game, 1 ou 2 cameras)
  unsigned long long pixels = 0;  // pixels renderizados
  unsigned long long tris = 0;    // triangulos enviados ao renderizador
  unsigned long long shadowTris = 0;
  unsigned draws = 0;             // objetos desenhados
  unsigned shadowCasters = 0, shadowReused = 0; // shadowReused: quantas vistas reaproveitaram o shadow map (sleeper)
};

struct FrameProfiler {
  static constexpr int kHistory = 240;
  using Clock = std::chrono::steady_clock;

  std::array<float, kHistory> frameMs{};                                         // tempo total do quadro (inclui esperas de VSync)
  std::array<std::array<float, kHistory>, static_cast<size_t>(Zone::Count)> zoneMs{};
  int head = 0, filled = 0;                                                      // anel circular
  float cur[static_cast<size_t>(Zone::Count)] = {};                              // zonas do quadro em andamento
  FrameCounters counters, last;                                                  // `last` = quadro anterior (para mostrar)
  double memoryMB = 0, peakMemoryMB = 0;
  bool paused = false;                                                           // congela o grafico para inspecionar
  Clock::time_point frameStart = Clock::now();

  void beginFrame() {
    const auto now = Clock::now();
    const float ms = std::chrono::duration<float, std::milli>(now - frameStart).count();
    frameStart = now;
    if (!paused) {
      frameMs[static_cast<size_t>(head)] = ms;
      for (size_t z = 0; z < static_cast<size_t>(Zone::Count); ++z) { zoneMs[z][static_cast<size_t>(head)] = cur[z]; }
      head = (head + 1) % kHistory; filled = std::min(filled + 1, kHistory);
    }
    std::memset(cur, 0, sizeof cur);
    last = counters; counters = FrameCounters{};
    if ((++memTick & 31) == 1) { memoryMB = processMemoryMB(); peakMemoryMB = std::max(peakMemoryMB, memoryMB); } // memoria: 1 leitura a cada 32 quadros
  }
  void add(Zone z, float ms) { cur[static_cast<int>(z)] += ms; }

  // Resumo estatistico dos ultimos `filled` quadros.
  struct Stats { float avg = 0, min = 0, max = 0, p99 = 0, fps = 0, low1 = 0, jitter = 0; };
  Stats stats() const {
    Stats s; if (filled == 0) return s;
    std::array<float, kHistory> tmp{}; double sum = 0; s.min = 1e9f;
    for (int i = 0; i < filled; ++i) { const float v = frameMs[static_cast<size_t>(i)]; tmp[static_cast<size_t>(i)] = v; sum += v; s.min = std::min(s.min, v); s.max = std::max(s.max, v); }
    s.avg = static_cast<float>(sum / filled);
    std::sort(tmp.begin(), tmp.begin() + filled);
    s.p99 = tmp[static_cast<size_t>(std::min(filled - 1, static_cast<int>(static_cast<float>(filled) * 0.99f)))];
    s.fps = s.avg > 0 ? 1000.f / s.avg : 0; s.low1 = s.p99 > 0 ? 1000.f / s.p99 : 0;   // "1% low": FPS do percentil 99 dos tempos
    double var = 0; for (int i = 0; i < filled; ++i) { const double d = static_cast<double>(frameMs[static_cast<size_t>(i)] - s.avg); var += d * d; }
    s.jitter = static_cast<float>(std::sqrt(var / filled));                              // desvio padrao: quanto o tempo oscila
    return s;
  }
  // Media de uma zona nos ultimos quadros.
  float zoneAvg(Zone z) const {
    if (filled == 0) return 0;
    double sum = 0;
    for (int i = 0; i < filled; ++i) sum += zoneMs[static_cast<size_t>(z)][static_cast<size_t>(i)];
    return static_cast<float>(sum / filled);
  }
  // Copia o historico em ordem cronologica (do mais antigo ao mais novo) para o grafico.
  template <class Arr> void ordered(const Arr& src, float* out) const {
    for (int i = 0; i < kHistory; ++i) out[i] = src[static_cast<size_t>((head + i) % kHistory)];
  }

private:
  unsigned memTick = 0;
};

// Mede uma zona enquanto o objeto existir.
struct ZoneTimer {
  FrameProfiler& p; Zone z; FrameProfiler::Clock::time_point t0 = FrameProfiler::Clock::now();
  ZoneTimer(FrameProfiler& prof, Zone zone) : p(prof), z(zone) {}
  ~ZoneTimer() { p.add(z, std::chrono::duration<float, std::milli>(FrameProfiler::Clock::now() - t0).count()); }
  ZoneTimer(const ZoneTimer&) = delete; ZoneTimer& operator=(const ZoneTimer&) = delete;
};

} // namespace editor
