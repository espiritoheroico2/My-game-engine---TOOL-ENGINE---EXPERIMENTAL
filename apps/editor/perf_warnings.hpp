// ToolEngine Editor - avisos de uso excessivo de recurso no Console (pista para o frame time instavel).
//
// Cada aviso dispara quando o recurso passa do limite (transicao) e repete a cada `repeatSec` enquanto continuar
// acima dele, para nao lotar o Console. Codigos na faixa TOOL-W3xx (desempenho). A checagem roda a cada 30 quadros:
// custa quase nada e nao muda o que e medido.
#pragma once
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <unordered_map>

#include "profiler.hpp"
#include "tool/core/parallel.hpp"
#include "tool/debug/debug.hpp"

namespace editor {

namespace perfdiag {
inline constexpr const char* kFrameJitter = "TOOL-W301";  // oscilacao do trabalho da CPU
inline constexpr const char* kZoneSpike = "TOOL-W302";    // uma zona com pico muito acima da media
inline constexpr const char* kDrawCalls = "TOOL-W303";    // objetos desenhados demais
inline constexpr const char* kTriangles = "TOOL-W304";    // triangulos demais por quadro
inline constexpr const char* kPixels = "TOOL-W305";       // resolucao de render alta demais
inline constexpr const char* kShadows = "TOOL-W306";      // sombras refeitas a cada quadro
inline constexpr const char* kMemory = "TOOL-W307";       // memoria do processo alta
inline constexpr const char* kCpuInfo = "TOOL-I308";      // informacao: nucleos e threads usados
}

class PerfWarnings {
public:
  using Clock = std::chrono::steady_clock;

  // Limites (ajustaveis). Acima deles, o aviso aparece no Console.
  struct Limits {
    float jitterMs = 4.f;                    // desvio padrao do trabalho da CPU
    float zoneSpikeMs = 8.f;                 // pico minimo para contar como pico de zona
    unsigned drawCalls = 2000;
    unsigned long long tris = 2000000;
    unsigned long long pixels = 3000000;
    unsigned shadowCasters = 64;
    double memoryMB = 2048;
    double repeatSec = 10;                   // intervalo entre repeticoes de um aviso que continua ativo
  };
  Limits limits;

  // Ignora os proximos `frames` quadros (carregamento de cena, primeira compilacao de shaders): esses picos nao sao
  // o problema do usuario e disparariam avisos falsos logo ao abrir o projeto.
  void quietFor(unsigned frames) { quiet_ = frames; }

  // Chamar uma vez por quadro, depois de Present.
  void check(const FrameProfiler& p) {
    if (quiet_ > 0) { --quiet_; return; }
    if (++tick_ % 30 != 0) return;
    if (tick_ == 30) {
      const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
      std::printf("[INFO] %s CPU: %u nucleos logicos, %zu threads de trabalho\n", perfdiag::kCpuInfo, hw, tool::ParallelPool::get().threads());
    }
    if (p.filled < 60) return;           // janela pequena demais para julgar

    const auto st = p.stats();
    const FrameCounters& c = p.last;

    gate(perfdiag::kFrameJitter, st.workJitter > limits.jitterMs,
         "trabalho da CPU oscila muito (desvio " + fmt(st.workJitter) + " ms, media " + fmt(st.workAvg) +
         " ms). Veja 'Time per zone' no Profiler: o pico esta na zona com maior pico.");

    // Pico por zona: maximo na janela contra a media da zona.
    for (int z = 0; z < static_cast<int>(Zone::Count); ++z) {
      if (static_cast<Zone>(z) == Zone::Present) continue;
      float mx = 0; double sum = 0;
      for (int i = 0; i < p.filled; ++i) { const float v = p.zoneMs[static_cast<size_t>(z)][static_cast<size_t>(i)]; mx = std::max(mx, v); sum += v; }
      const float avg = static_cast<float>(sum / p.filled);
      gate(std::string(perfdiag::kZoneSpike) + "." + std::to_string(z), mx > limits.zoneSpikeMs && mx > 3.f * avg,
           "zona '" + std::string(zoneName(static_cast<Zone>(z))) + "' teve pico de " + fmt(mx) + " ms (media " + fmt(avg) + " ms).");
    }

    gate(perfdiag::kDrawCalls, c.draws > limits.drawCalls,
         "muitos objetos desenhados por quadro (" + std::to_string(c.draws) + "). Use culling, LOD ou agrupe malhas.");
    gate(perfdiag::kTriangles, c.tris > limits.tris,
         "muitos triangulos por quadro (" + std::to_string(c.tris) + "). Reduza a malha ou a distancia de desenho.");
    gate(perfdiag::kPixels, c.pixels > limits.pixels,
         "resolucao de render alta (" + std::to_string(c.pixels) + " pixels por quadro). Reduza a escala de render.");
    gate(perfdiag::kShadows, c.shadowCasters > limits.shadowCasters && c.shadowReused == 0,
         "sombras refeitas a cada quadro (" + std::to_string(c.shadowCasters) + " objetos que projetam sombra). Mova menos objetos ou use menos cascatas.");
    gate(perfdiag::kMemory, p.memoryMB > limits.memoryMB,
         "memoria do processo em " + fmt(static_cast<float>(p.memoryMB)) + " MB.");
  }

private:
  struct State { bool active = false; Clock::time_point last{}; };
  std::unordered_map<std::string, State> state_;
  unsigned tick_ = 0;
  unsigned quiet_ = 0;

  static std::string fmt(float v) { char b[32]; std::snprintf(b, sizeof b, "%.2f", static_cast<double>(v)); return b; }

  // Dispara na transicao (ativo agora e nao estava) e repete a cada repeatSec enquanto continuar ativo.
  void gate(const std::string& key, bool over, const std::string& msg) {
    State& s = state_[key];
    const auto now = Clock::now();
    const bool repeat = s.active && std::chrono::duration<double>(now - s.last).count() >= limits.repeatSec;
    if (over && (!s.active || repeat)) {
      // o codigo do aviso e o prefixo antes de qualquer '.' (zonas usam TOOL-W302.<indice> como chave)
      const std::string code = key.substr(0, key.find('.'));
      tool::debug().report(code.c_str(), tool::Severity::Warning, msg);
      s.last = now;
    }
    s.active = over;
  }
};

} // namespace editor
