// Modelo do painel do Profiler feito com SpriteUI: decide QUAIS numeros aparecem e como eles se formatam.
//
// Nao usa SDL, ImGui nem SpriteUI. Le editor::FrameProfiler (profiler.hpp) e o SysSnapshot (sysmon.hpp) e
// produz uma lista fixa de linhas (Row) e os dois historicos dos graficos. O painel so desenha o resultado.
//
// Regras que o modelo garante (testadas em tests/test_profiler_model.cpp):
//  - Os numeros so mudam a cada refresh (padrao 4 vezes por segundo). Entre dois refreshes nada muda.
//  - Cada valor numerico ocupa uma largura fixa em caracteres (formatCell), alinhado a direita.
//    NaN e infinito viram "--"; valor que nao cabe vira "#" (nunca estoura a largura).
//  - A escala do grafico so cai devagar (kScaleDecay por refresh) e sobe na hora se o dado passar dela.
//  - Nenhuma alocacao no caminho de leitura (rows, graph, graphScale): tudo em arrays fixos.
#pragma once

#include "profiler.hpp"
#include "sysmon.hpp"
#include "tool/render/render_stats.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace editor::profiler_model {

constexpr int kGraphSamples = FrameProfiler::kHistory;   // 240 quadros
constexpr int kGraphCount = 2;                            // 0 = tempo de quadro, 1 = trabalho da CPU
constexpr int kMaxRows = 160;
constexpr int kMaxCells = 4;
constexpr int kLabelChars = 48;
constexpr int kTextChars = 200;
constexpr int kValueChars = 16;
constexpr int kMaxCores = 32;                             // nucleos listados; acima disso, uma linha de resumo
constexpr double kDefaultRefreshHz = 4.0;
constexpr float kScaleDecay = 0.97f;                      // quanto a escala do grafico pode cair por refresh (3%)
constexpr float kFrameScaleFloor = 16.7f;                 // ms: 60 FPS
constexpr float kWorkScaleFloor = 4.f;                    // ms

// Formata `v` em um campo de exatamente `width` caracteres, alinhado a direita, com `decimals` casas.
// `suffix` (ex.: "%") entra antes do preenchimento e conta na largura. Retorna `width` (0 se o buffer nao couber).
// NaN e infinito: "--". Valor que nao cabe na largura: so '#'. -0.00 vira 0.00.
inline int formatCell(double v, int decimals, int width, char* out, int outSize, const char* suffix = "") {
  if (width <= 0 || width + 1 > outSize) {
    if (outSize > 0) out[0] = '\0';
    return 0;
  }
  decimals = std::clamp(decimals, 0, 6);
  char body[64];
  if (!std::isfinite(v)) {
    std::snprintf(body, sizeof body, "--%s", suffix);
  } else {
    const double eps = 0.5 * std::pow(10.0, -decimals);
    if (std::fabs(v) < eps) v = 0.0;                       // evita "-0.00"
    std::snprintf(body, sizeof body, "%.*f%s", decimals, v, suffix);
  }
  const int len = static_cast<int>(std::strlen(body));
  if (len > width) {
    std::memset(out, '#', static_cast<size_t>(width));
  } else {
    const int pad = width - len;
    std::memset(out, ' ', static_cast<size_t>(pad));
    std::memcpy(out + pad, body, static_cast<size_t>(len));
  }
  out[width] = '\0';
  return width;
}

// Copia texto para um buffer fixo, truncando com seguranca.
inline void copyText(char* dst, int cap, const char* src) {
  if (cap <= 0) return;
  if (!src) src = "";
  std::snprintf(dst, static_cast<size_t>(cap), "%s", src);
}

// Estado extra que o Profiler nao conhece (API, luz, GPU, cena). Preenchido pelo editor a cada refresh.
struct Extras {
  std::string api;                  // "Software (CPU)", "Vulkan", ...
  std::string adapter;              // nome da placa, ou "(software renderer)"
  std::string cpuName;              // CPU (ja vem do SysMonitor; deixe vazio para usar a dele)
  bool hasGpu = false;
  unsigned gpuDraws = 0, gpuTriangles = 0, gpuUploads = 0, gpuPipelineSwitches = 0;
  tool::render::FrameRenderStats renderStats;
  unsigned gpuBadFrames = 0;
  bool lightShadows = false;
  unsigned lightCascades = 0, lightShadowRes = 0, lightDropped = 0;
  bool postOn = false;
  std::size_t entities = 0, dormant = 0, reused = 0, freed = 0, meshes = 0, sounds = 0, tags = 0;
  unsigned errors = 0, warnings = 0;
};

enum class RowKind : std::uint8_t { Heading, Text, Cells, Bar, Graph, Gap };

struct Cell {
  char label[kLabelChars] = {};
  char value[kValueChars] = {};     // texto ja formatado e com a largura fixa (ou texto livre se valueWidth == 0)
  int valueWidth = 0;               // caracteres reservados ao valor; 0 = texto livre
};

struct Row {
  RowKind kind = RowKind::Gap;
  char text[kTextChars] = {};       // Heading, Text, rotulo de Bar, legenda de Graph
  int cellCount = 0;
  Cell cells[kMaxCells];
  float fraction = 0.f;             // Bar: 0..1 (preenchimento da barra)
  int graph = -1;                   // Graph: indice do historico (0 ou 1)
  bool emphasis = false;            // Prioridade: o painel desenha em negrito e com fonte maior
};

class ProfilerModel {
 public:
  explicit ProfilerModel(double refreshHz = kDefaultRefreshHz) { setRefreshHz(refreshHz); }

  void setRefreshHz(double hz) {
    if (!(hz > 0.0)) hz = kDefaultRefreshHz;
    hz = std::clamp(hz, 0.5, 60.0);
    interval_ = 1.0 / hz;
  }
  double refreshInterval() const { return interval_; }

  // true quando o proximo refresh ja deve acontecer (ou nunca aconteceu). Use para nao montar Extras a cada quadro.
  bool due(double nowSec) const {
    if (!refreshed_) return true;
    return nowSec - lastRefresh_ >= interval_ - 1e-9;
  }

  // Recalcula todas as linhas e os graficos a partir dos dados atuais. `tr` traduz os rotulos fixos (T()).
  template <class Translate>
  void refresh(double nowSec, const FrameProfiler& prof, const SysSnapshot& sys, const Extras& ex, Translate tr) {
    lastRefresh_ = nowSec;
    refreshed_ = true;
    ++generation_;
    rowCount_ = 0;
    truncated_ = false;
    const bool have = prof.filled > 0;
    const FrameProfiler::Stats st = prof.stats();
    const float na = std::nanf("");
    auto num = [&](float v) { return have ? v : na; };   // sem amostras: "--" em todos os numeros

    // ---- graficos: copia ordenada do historico, com NaN trocado por 0, e escala estavel
    copyGraph(0, prof.frameMs, prof);
    copyGraph(1, prof.workMs, prof);
    const float targetFrame = std::max(kFrameScaleFloor, have ? st.max * 1.1f : 0.f);
    const float targetWork = std::max(kWorkScaleFloor, have ? st.workAvg * 2.f : 0.f);
    scale_[0] = stableScale(scale_[0], targetFrame, kFrameScaleFloor);
    scale_[1] = stableScale(scale_[1], targetWork, kWorkScaleFloor);

    // ---- Prioridade: o que mais importa, em destaque (negrito e fonte maior). Os detalhes continuam abaixo.
    {
      Row& h = beginRow(RowKind::Heading);
      copyText(h.text, kTextChars, tr("Priority"));
      h.emphasis = true;
    }
    {
      Row& r = beginRow(RowKind::Cells); r.emphasis = true;
      cell(r, tr("draw calls"), static_cast<double>(ex.gpuDraws), 0, 6);
      cell(r, tr("objects drawn"), static_cast<double>(prof.last.draws), 0, 6);
      cell(r, tr("triangles"), static_cast<double>(prof.last.tris), 0, 10);
    }
    {
      Row& r = beginRow(RowKind::Cells); r.emphasis = true;
      cell(r, tr("CPU % (editor)"), num(sys.editorCpu), 1, 6);
      cell(r, tr("CPU work ms"), num(st.workAvg), 2, 7);
      cell(r, tr("GPU zone ms"), num(prof.zoneAvg(editor::Zone::GpuRender)), 2, 7);
    }
    {
      Row& r = beginRow(RowKind::Cells); r.emphasis = true;
      const float renderMs = prof.zoneAvg(editor::Zone::SceneRender) + prof.zoneAvg(editor::Zone::GameRender) + prof.zoneAvg(editor::Zone::Ui);
      cell(r, tr("render ms"), num(renderMs), 2, 7);
      cell(r, tr("process MB"), prof.memoryMB, 0, 6);
      cell(r, tr("VRAM MB"), sys.vramUsedMB, 0, 7);
    }
    {
      Row& r = beginRow(RowKind::Cells); r.emphasis = true;
      cell(r, tr("delay avg ms"), num(st.avg), 2, 7);
      cell(r, tr("delay p99 ms"), num(st.p99), 2, 7);
      cell(r, tr("delay max ms"), num(st.max), 2, 7);
      cell(r, tr("jitter ms"), num(st.jitter), 2, 7);
    }
    text(tr("GPU % is not measured yet: it needs GPU timestamp queries in the RHI (see docs/planos/00_ROADMAP.md, item 7)."));
    // ---- Frames
    heading(tr("Frames"));
    {
      Row& r = beginRow(RowKind::Cells);
      cell(r, tr("FPS"), num(st.fps), 0, 4);
      cell(r, tr("avg ms"), num(st.avg), 2, 7);
      cell(r, tr("min ms"), num(st.min), 2, 7);
      cell(r, tr("max ms"), num(st.max), 2, 7);
    }
    {
      Row& r = beginRow(RowKind::Cells);
      cell(r, tr("1% low FPS"), num(st.low1), 0, 4);
      cell(r, tr("p99 ms"), num(st.p99), 2, 7);
      cell(r, tr("jitter ms"), num(st.jitter), 2, 7);
    }
    text(prof.paused ? tr("Graph paused (frozen)") : tr("Graph live"));
    graphRow(0, tr("frame time (ms), last frames"), st.max).emphasis = true;
    {
      Row& r = beginRow(RowKind::Cells);
      cell(r, tr("CPU work avg ms"), num(st.workAvg), 2, 7);
      cell(r, tr("work jitter ms"), num(st.workJitter), 2, 7);
    }
    graphRow(1, tr("CPU work per frame (ms)"), st.workAvg);
    text(tr("CPU work only (no VSync wait)"));

    // ---- Tempo por zona
    heading(tr("Time per zone (average ms)"));
    for (int z = 0; z < static_cast<int>(Zone::Count); ++z) {
      const float a = have ? prof.zoneAvg(static_cast<Zone>(z)) : na;
      Row& r = beginRow(RowKind::Bar);
      copyText(r.text, kTextChars, tr(zoneName(static_cast<Zone>(z))));
      barCell(r, a, 2, 7);
      r.fraction = (have && st.avg > 0.f && std::isfinite(a)) ? std::clamp(a / st.avg, 0.f, 1.f) : 0.f;
    }
    {
      Row& r = beginRow(RowKind::Cells);
      cell(r, tr("measured CPU work ms"), num(st.workAvg), 2, 7);
      cell(r, tr("of avg ms"), num(st.avg), 2, 7);
    }

    // ---- Rendering (contadores do ultimo quadro)
    const FrameCounters& c = prof.last;
    heading(tr("Rendering"));
    textValue(tr("API"), ex.api.c_str());
    {
      Row& r = beginRow(RowKind::Cells);
      cell(r, tr("views"), static_cast<double>(c.views), 0, 2);
      cell(r, tr("pixels M"), static_cast<double>(c.pixels) / 1e6, 2, 6);
      cell(r, tr("objects drawn"), static_cast<double>(c.draws), 0, 5);
    }
    {
      Row& r = beginRow(RowKind::Cells);
      cell(r, tr("triangles"), static_cast<double>(c.tris), 0, 10);
      cell(r, tr("culled"), static_cast<double>(c.culled), 0, 5);
    }
    {
      Row& r = beginRow(RowKind::Cells);
      cell(r, tr("shadow casters"), static_cast<double>(c.shadowCasters), 0, 4);
      cell(r, tr("shadow tris (CPU)"), static_cast<double>(c.shadowTris), 0, 9);
      cell(r, tr("shadow reused"), static_cast<double>(c.shadowReused), 0, 3);
    }
    {
      Row& r = beginRow(RowKind::Cells);
      cell(r, tr("Light Prism shadows"), 0, 0, 0, ex.lightShadows ? tr("on") : tr("off"));
      cell(r, tr("cascades"), static_cast<double>(ex.lightCascades), 0, 2);
      cell(r, tr("shadow px"), static_cast<double>(ex.lightShadowRes), 0, 5);
      cell(r, tr("lights dropped"), static_cast<double>(ex.lightDropped), 0, 3);
    }
    textValue(tr("post-processing"), ex.postOn ? tr("on") : tr("off"));
    if (ex.hasGpu) {
      auto appendShadowCache = [&](const tool::render::ShadowCacheStats& cache) {
        heading(tr("Point/Spot face cache"));
        char line[kTextChars];
        std::snprintf(line, sizeof line, "%llu reused, %llu rendered, %llu first use, %llu target resets",
                      static_cast<unsigned long long>(cache.pointFacesReused),
                      static_cast<unsigned long long>(cache.pointFacesRendered),
                      static_cast<unsigned long long>(cache.pointFacesFirstUse),
                      static_cast<unsigned long long>(cache.pointFacesTargetReset));
        text(line);
        std::snprintf(line, sizeof line, "%llu light changes, %llu caster changes, %llu render failures",
                      static_cast<unsigned long long>(cache.pointFacesLightChanged),
                      static_cast<unsigned long long>(cache.pointFacesCastersChanged),
                      static_cast<unsigned long long>(cache.pointFaceRenderFailures));
        text(line);
      };
      Row& r = beginRow(RowKind::Cells);
      cell(r, tr("GPU draws"), static_cast<double>(ex.gpuDraws), 0, 6);
      cell(r, tr("GPU triangles"), static_cast<double>(ex.gpuTriangles), 0, 9);
      cell(r, tr("uploads"), static_cast<double>(ex.gpuUploads), 0, 5);
      cell(r, tr("switches"), static_cast<double>(ex.gpuPipelineSwitches), 0, 5);
      heading(tr("GpuRenderer passes (CPU ms; GPU time unavailable)"));
      for (std::size_t viewIndex = 0; viewIndex < tool::render::kRenderViewCount; ++viewIndex) {
        const auto& view = ex.renderStats.views[viewIndex];
        if (!view.valid) continue;
        heading(tr(tool::render::renderViewName(static_cast<tool::render::RenderViewId>(viewIndex))));
        for (std::size_t passIndex = 0; passIndex < tool::render::kRenderPassCount; ++passIndex) {
          const auto& pass = view.passes[passIndex];
          char line[kTextChars];
          std::snprintf(line, sizeof line, "%s: %llu draws, %llu instances, %llu triangles, %.3f ms CPU",
                        tr(tool::render::renderPassName(static_cast<tool::render::RenderPassId>(passIndex))),
                        static_cast<unsigned long long>(pass.drawCalls),
                        static_cast<unsigned long long>(pass.instances),
                        static_cast<unsigned long long>(pass.triangles), pass.cpuMs);
          text(line);
        }
        char backendLine[kTextChars];
        std::snprintf(backendLine, sizeof backendLine, "Backend: %llu draws, %llu triangles, %llu uploads, %llu switches, %llu submits, %llu flushes",
                      static_cast<unsigned long long>(view.backendDrawCalls),
                      static_cast<unsigned long long>(view.backendTriangles),
                      static_cast<unsigned long long>(view.backendBufferUploads),
                      static_cast<unsigned long long>(view.backendPipelineSwitches),
                      static_cast<unsigned long long>(view.backendSubmitCount),
                      static_cast<unsigned long long>(view.backendFlushCount));
        text(backendLine);
        appendShadowCache(view.shadowCache);
      }
      heading(tr("All views by pass"));
      for (std::size_t passIndex = 0; passIndex < tool::render::kRenderPassCount; ++passIndex) {
        const auto pass = ex.renderStats.total(static_cast<tool::render::RenderPassId>(passIndex));
        char line[kTextChars];
        std::snprintf(line, sizeof line, "%s: %llu draws, %llu instances, %llu triangles, %.3f ms CPU",
                      tr(tool::render::renderPassName(static_cast<tool::render::RenderPassId>(passIndex))),
                      static_cast<unsigned long long>(pass.drawCalls),
                      static_cast<unsigned long long>(pass.instances),
                      static_cast<unsigned long long>(pass.triangles), pass.cpuMs);
        text(line);
      }
      const auto total = ex.renderStats.total();
      char totalLine[kTextChars];
      std::snprintf(totalLine, sizeof totalLine, "%s: %llu draws, %llu instances, %llu triangles, %.3f ms CPU",
                    tr("All rendered views"), static_cast<unsigned long long>(total.drawCalls),
                    static_cast<unsigned long long>(total.instances), static_cast<unsigned long long>(total.triangles),
                    total.cpuMs);
      text(totalLine);
      const auto backendTotal = ex.renderStats.backendTotal();
      std::snprintf(totalLine, sizeof totalLine, "%s: %llu draws, %llu triangles, %llu uploads, %llu switches",
                    tr("Backend total"), static_cast<unsigned long long>(backendTotal.backendDrawCalls),
                    static_cast<unsigned long long>(backendTotal.backendTriangles),
                    static_cast<unsigned long long>(backendTotal.backendBufferUploads),
                    static_cast<unsigned long long>(backendTotal.backendPipelineSwitches));
      text(totalLine);
      appendShadowCache(ex.renderStats.shadowCacheTotal());
    }
    textValue(tr("Adapter"), ex.adapter.c_str());

    // ---- Sistema
    heading(tr("System: CPU, RAM, VRAM and cache"));
    if (!sys.cpuName.empty()) text(sys.cpuName.c_str());
    if (sys.cpuOk) {
      Row& r = beginRow(RowKind::Bar);
      copyText(r.text, kTextChars, tr("CPU total"));
      barCell(r, sys.cpuTotal, 0, 5, "%");
      r.fraction = std::clamp(sys.cpuTotal / 100.f, 0.f, 1.f);
      if (!std::isfinite(sys.cpuTotal)) r.fraction = 0.f;
      {
        Row& q = beginRow(RowKind::Cells);
        cell(q, tr("cores in use (above 25%)"), static_cast<double>(sys.coresBusy), 0, 3);
        cell(q, tr("cores online"), static_cast<double>(sys.coresOnline), 0, 3);
        cell(q, tr("editor, all cores %"), static_cast<double>(sys.editorCpu), 1, 5);
      }
      const int cores = static_cast<int>(sys.core.size());
      for (int i = 0; i < cores && i < kMaxCores; ++i) {
        Row& q = beginRow(RowKind::Bar);
        char label[kLabelChars];
        std::snprintf(label, sizeof label, "%s %d", tr("core"), i);
        copyText(q.text, kTextChars, label);
        barCell(q, sys.core[static_cast<size_t>(i)], 0, 5, "%");
        q.fraction = std::isfinite(sys.core[static_cast<size_t>(i)]) ? std::clamp(sys.core[static_cast<size_t>(i)] / 100.f, 0.f, 1.f) : 0.f;
      }
      if (cores > kMaxCores) {
        char more[kTextChars];
        std::snprintf(more, sizeof more, "+ %d %s", cores - kMaxCores, tr("more cores not listed"));
        text(more);
      }
    } else {
      text(tr("CPU: measuring..."));
    }
    if (sys.ramOk) {
      Row& r = beginRow(RowKind::Cells);
      cell(r, tr("RAM used MB"), sys.ramUsedMB, 0, 7);
      cell(r, tr("RAM total MB"), sys.ramTotalMB, 0, 7);
      cell(r, tr("editor MB"), prof.memoryMB, 0, 6);
      cell(r, tr("editor peak MB"), prof.peakMemoryMB, 0, 6);
    } else {
      text(tr("RAM (system): not available on this platform"));
    }
    if (sys.vramOk) {
      Row& r = beginRow(RowKind::Cells);
      cell(r, tr("VRAM used MB"), sys.vramUsedMB, 0, 7);
      cell(r, tr("VRAM budget MB"), sys.vramBudgetMB, 0, 7);
    } else {
      char note[kTextChars];
      std::snprintf(note, sizeof note, "%s: %s", tr("VRAM"), sys.vramNote.c_str());
      text(note);
    }
    text(tr("Cache sizes"));
    for (const auto& k : sys.caches) {
      Row& r = beginRow(RowKind::Cells);
      char label[kLabelChars];
      std::snprintf(label, sizeof label, "L%d %s KB", k.level, k.type.c_str());
      copyText(r.cells[0].label, kLabelChars, label);
      formatCell(static_cast<double>(k.sizeKB), 0, 8, r.cells[0].value, kValueChars);
      r.cells[0].valueWidth = 8;
      r.cellCount = 1;
      if (r.cellCount < kMaxCells) {
        Cell& lb = r.cells[1];
        copyText(lb.label, kLabelChars, tr("line bytes"));
        formatCell(static_cast<double>(k.lineBytes), 0, 4, lb.value, kValueChars);
        lb.valueWidth = 4;
        r.cellCount = 2;
      }
    }
    if (sys.caches.empty() && !sys.cacheNote.empty()) text(sys.cacheNote.c_str());
    text(tr("Hit rate is not measured: it needs hardware counters (perf / VTune)."));

    // ---- Cena e memoria
    heading(tr("Scene and memory"));
    {
      Row& r = beginRow(RowKind::Cells);
      cell(r, tr("live entities"), static_cast<double>(ex.entities), 0, 6);
      cell(r, tr("dormant"), static_cast<double>(ex.dormant), 0, 6);
      cell(r, tr("reused"), static_cast<double>(ex.reused), 0, 6);
      cell(r, tr("freed"), static_cast<double>(ex.freed), 0, 6);
    }
    {
      Row& r = beginRow(RowKind::Cells);
      cell(r, tr("loaded models"), static_cast<double>(ex.meshes), 0, 6);
      cell(r, tr("sounds"), static_cast<double>(ex.sounds), 0, 6);
      cell(r, tr("tags"), static_cast<double>(ex.tags), 0, 6);
    }
    if (prof.memoryMB > 0) {
      Row& r = beginRow(RowKind::Cells);
      cell(r, tr("process memory MB"), prof.memoryMB, 0, 6);
      cell(r, tr("peak MB"), prof.peakMemoryMB, 0, 6);
    } else {
      text(tr("process memory: not available on this platform"));
    }
    {
      Row& r = beginRow(RowKind::Cells);
      cell(r, tr("errors"), static_cast<double>(ex.errors), 0, 5);
      cell(r, tr("warnings"), static_cast<double>(ex.warnings), 0, 5);
    }
    if (truncated_) {   // so aparece se o limite de linhas foi atingido (nao deve acontecer com o layout atual)
      text("...");
    }
  }

  // ---- Leitura (sem alocacao) -------------------------------------------------------------------------------
  unsigned generation() const { return generation_; }
  int rowCount() const { return rowCount_; }
  const Row& row(int i) const { return rows_[static_cast<size_t>(i)]; }
  bool truncated() const { return truncated_; }
  // Historico do grafico `g` em ms, do mais antigo ao mais novo (sempre finito).
  const float* graph(int g) const { return graphs_[static_cast<size_t>(g)].data(); }
  int graphSamples() const { return kGraphSamples; }
  // Escala (ms) do grafico `g`: o valor de cima do grafico.
  float graphScale(int g) const { return scale_[static_cast<size_t>(g)]; }

  // Escala que sobe na hora se o alvo passou dela, e cai no maximo kScaleDecay por refresh.
  static float stableScale(float prev, float target, float floor) {
    if (!std::isfinite(target)) target = floor;
    float base = prev;
    if (!std::isfinite(base) || base < floor) base = floor;
    return std::max(floor, std::max(target, base * kScaleDecay));
  }

 private:
  template <class Arr>
  void copyGraph(int g, const Arr& src, const FrameProfiler& prof) {
    float* out = graphs_[static_cast<size_t>(g)].data();
    prof.ordered(src, out);
    for (int i = 0; i < kGraphSamples; ++i) {
      if (!std::isfinite(out[i])) out[i] = 0.f;
    }
  }

  Row& beginRow(RowKind kind) {
    if (rowCount_ >= kMaxRows) {
      truncated_ = true;
      static Row sink;                   // linhas extras vao para um lixo descartavel (sem alocar)
      sink = Row{};
      return sink;
    }
    Row& r = rows_[static_cast<size_t>(rowCount_++)];
    r = Row{};
    r.kind = kind;
    return r;
  }

  void heading(const char* t) { copyText(beginRow(RowKind::Heading).text, kTextChars, t); }
  void text(const char* t) { copyText(beginRow(RowKind::Text).text, kTextChars, t); }
  void textValue(const char* label, const char* value) {
    Row& r = beginRow(RowKind::Cells);
    cell(r, label, 0, 0, 0, value);
  }
  Row& graphRow(int g, const char* caption, float shownMax) {
    Row& r = beginRow(RowKind::Graph);
    r.graph = g;
    copyText(r.text, kTextChars, caption);
    r.cellCount = 1;
    formatCell(static_cast<double>(shownMax), 1, 6, r.cells[0].value, kValueChars);
    copyText(r.cells[0].label, kLabelChars, "max");
    r.cells[0].valueWidth = 6;
    return r;
  }

  // Adiciona uma celula numerica de largura fixa (ou uma celula de texto quando valueWidth == 0 e `text` vem).
  void cell(Row& r, const char* label, double v, int decimals, int width, const char* textValue = nullptr) {
    if (r.cellCount >= kMaxCells) return;
    Cell& c = r.cells[r.cellCount++];
    copyText(c.label, kLabelChars, label);
    if (textValue) {
      copyText(c.value, kValueChars, textValue);
      c.valueWidth = 0;
    } else {
      formatCell(v, decimals, width, c.value, kValueChars);
      c.valueWidth = width;
    }
  }
  // Celula de barra: so o valor, com rotulo vazio.
  void barCell(Row& r, double v, int decimals, int width, const char* suffix = "") {
    if (r.cellCount >= kMaxCells) return;
    Cell& c = r.cells[r.cellCount++];
    c.label[0] = '\0';
    formatCell(v, decimals, width, c.value, kValueChars, suffix);
    c.valueWidth = width;
  }

  std::array<Row, kMaxRows> rows_{};
  int rowCount_ = 0;
  bool truncated_ = false;
  std::array<std::array<float, kGraphSamples>, kGraphCount> graphs_{};
  std::array<float, kGraphCount> scale_{kFrameScaleFloor, kWorkScaleFloor};
  double interval_ = 1.0 / kDefaultRefreshHz;
  double lastRefresh_ = 0.0;
  bool refreshed_ = false;
  unsigned generation_ = 0;
};

}  // namespace editor::profiler_model
