// Modelo do painel do Profiler (SpriteUI): largura fixa dos numeros, taxa de atualizacao, escala estavel e NaN.
// Nao usa SDL nem ImGui nem SpriteUI. Roda no ctest como Editor.ProfilerModel.
#include "profiler_model.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>

namespace {

using editor::FrameProfiler;
using editor::SysSnapshot;
namespace pm = editor::profiler_model;

int run = 0;
int failed = 0;
const char* current = "";

void Check(bool ok, const char* expr) {
  ++run;
  if (!ok) {
    ++failed;
    std::fprintf(stderr, "[FAIL] %s: %s\n", current, expr);
  }
}
#define CHECK(expr) Check((expr), #expr)

bool Eq(const char* a, const char* b) { return std::strcmp(a, b) == 0; }

const float kNaN = std::numeric_limits<float>::quiet_NaN();

auto Identity = [](const char* s) { return s; };

// Preenche um Profiler com `n` quadros do mesmo tempo (ms) e o resto com `fill`.
void SetFrames(FrameProfiler& p, float ms, int n = FrameProfiler::kHistory) {
  for (int i = 0; i < FrameProfiler::kHistory; ++i) {
    p.frameMs[static_cast<std::size_t>(i)] = i < n ? ms : 0.f;
    p.workMs[static_cast<std::size_t>(i)] = i < n ? ms * 0.25f : 0.f;
  }
  p.filled = n;
}

// Acha a primeira celula com o rotulo dado em qualquer linha. Retorna nullptr se nao achar.
const pm::Cell* FindCell(const pm::ProfilerModel& m, const char* label) {
  for (int i = 0; i < m.rowCount(); ++i) {
    const pm::Row& r = m.row(i);
    for (int c = 0; c < r.cellCount; ++c) {
      if (Eq(r.cells[c].label, label)) return &r.cells[c];
    }
  }
  return nullptr;
}

void TestFormatFixedWidth() {
  current = "TestFormatFixedWidth";
  char b[32];
  CHECK(pm::formatCell(9.99, 2, 6, b, sizeof b) == 6);
  CHECK(Eq(b, "  9.99"));
  CHECK(pm::formatCell(10.0, 2, 6, b, sizeof b) == 6);
  CHECK(Eq(b, " 10.00"));
  CHECK(std::strlen(b) == 6);
  pm::formatCell(0.0, 2, 6, b, sizeof b);
  CHECK(Eq(b, "  0.00"));
  pm::formatCell(999.999, 2, 6, b, sizeof b);      // arredonda para 1000.00, que nao cabe em 6
  CHECK(Eq(b, "######"));
  pm::formatCell(1234567.0, 2, 6, b, sizeof b);
  CHECK(Eq(b, "######"));
}

void TestFormatSameLengthForManyValues() {
  current = "TestFormatSameLengthForManyValues";
  const double values[] = {0.0, 0.004, 9.99, 9.996, 10.0, 99.5, 123.456, -7.25, 1e5, 4.0e7};
  for (double v : values) {
    char b[32];
    pm::formatCell(v, 2, 7, b, sizeof b);
    CHECK(std::strlen(b) == 7);
  }
}

void TestFormatNegativeZeroAndSuffix() {
  current = "TestFormatNegativeZeroAndSuffix";
  char b[32];
  pm::formatCell(-0.001, 2, 6, b, sizeof b);
  CHECK(Eq(b, "  0.00"));
  pm::formatCell(42.0, 0, 5, b, sizeof b, "%");
  CHECK(Eq(b, "  42%"));
  CHECK(std::strlen(b) == 5);
}

void TestFormatNanAndInfinity() {
  current = "TestFormatNanAndInfinity";
  char b[32];
  pm::formatCell(kNaN, 2, 6, b, sizeof b);
  CHECK(Eq(b, "    --"));
  pm::formatCell(std::numeric_limits<double>::infinity(), 2, 6, b, sizeof b);
  CHECK(Eq(b, "    --"));
  pm::formatCell(-std::numeric_limits<double>::infinity(), 0, 4, b, sizeof b, "%");
  CHECK(std::strlen(b) == 4);
}

void TestFormatBufferTooSmall() {
  current = "TestFormatBufferTooSmall";
  char b[4] = {'x', 'x', 'x', 'x'};
  CHECK(pm::formatCell(1.0, 2, 6, b, sizeof b) == 0);
  CHECK(b[0] == '\0');
}

void TestRefreshRateAtFourHz() {
  current = "TestRefreshRateAtFourHz";
  pm::ProfilerModel m(4.0);
  CHECK(std::fabs(m.refreshInterval() - 0.25) < 1e-12);
  FrameProfiler p;
  SysSnapshot sys;
  pm::Extras ex;
  SetFrames(p, 16.0f);
  m.refresh(0.0, p, sys, ex, Identity);
  const unsigned gen0 = m.generation();
  CHECK(gen0 == 1);

  // Dois quadros dentro do intervalo: o texto nao muda mesmo com o Profiler mudando por baixo.
  char before[32];
  std::snprintf(before, sizeof before, "%s", pm::Cell{}.value);
  const pm::Cell* fps0 = FindCell(m, "FPS");
  CHECK(fps0 != nullptr);
  char fpsBefore[32];
  std::snprintf(fpsBefore, sizeof fpsBefore, "%s", fps0 ? fps0->value : "");
  SetFrames(p, 8.0f);                                   // dados mudaram: 120 FPS
  CHECK(!m.due(0.10));
  CHECK(!m.due(0.2499));
  const pm::Cell* fps1 = FindCell(m, "FPS");
  CHECK(fps1 != nullptr && Eq(fps1->value, fpsBefore));
  CHECK(m.generation() == gen0);

  // Depois do intervalo: atualiza.
  CHECK(m.due(0.25));
  m.refresh(0.25, p, sys, ex, Identity);
  CHECK(m.generation() == gen0 + 1);
  const pm::Cell* fps2 = FindCell(m, "FPS");
  CHECK(fps2 != nullptr && !Eq(fps2->value, fpsBefore));
  (void)before;
}

void TestRefreshCountInOneSecond() {
  current = "TestRefreshCountInOneSecond";
  pm::ProfilerModel m(4.0);
  FrameProfiler p;
  SysSnapshot sys;
  pm::Extras ex;
  SetFrames(p, 16.0f);
  int refreshes = 0;
  for (int i = 0; i < 60; ++i) {          // 60 quadros em 1 s (60 FPS simulados)
    const double t = i / 60.0;
    if (m.due(t)) {
      m.refresh(t, p, sys, ex, Identity);
      ++refreshes;
    }
  }
  CHECK(refreshes == 4);                  // 0, 0,25, 0,5 e 0,75 s
}

void TestRefreshRateIsConfigurable() {
  current = "TestRefreshRateIsConfigurable";
  pm::ProfilerModel m(2.0);
  CHECK(std::fabs(m.refreshInterval() - 0.5) < 1e-12);
  m.setRefreshHz(10.0);
  CHECK(std::fabs(m.refreshInterval() - 0.1) < 1e-12);
  m.setRefreshHz(0.0);                    // invalido: volta ao padrao
  CHECK(std::fabs(m.refreshInterval() - 0.25) < 1e-12);
}

void TestCellsKeepFixedWidthAcrossRefreshes() {
  current = "TestCellsKeepFixedWidthAcrossRefreshes";
  pm::ProfilerModel m;
  FrameProfiler p;
  SysSnapshot sys;
  pm::Extras ex;
  const float frames[] = {9.9f, 10.0f, 99.99f, 100.0f, 1000.0f, 0.0f};
  double t = 0.0;
  for (float ms : frames) {
    SetFrames(p, ms);
    m.refresh(t, p, sys, ex, Identity);
    t += 0.25;
    for (int i = 0; i < m.rowCount(); ++i) {
      const pm::Row& r = m.row(i);
      for (int c = 0; c < r.cellCount; ++c) {
        if (r.cells[c].valueWidth > 0) CHECK(static_cast<int>(std::strlen(r.cells[c].value)) == r.cells[c].valueWidth);
      }
    }
  }
}

void TestNumbersShowWhenDataExists() {
  current = "TestNumbersShowWhenDataExists";
  pm::ProfilerModel m;
  FrameProfiler p;
  SysSnapshot sys;
  pm::Extras ex;
  SetFrames(p, 20.0f);                    // 50 FPS
  m.refresh(0.0, p, sys, ex, Identity);
  const pm::Cell* avg = FindCell(m, "avg ms");
  CHECK(avg != nullptr && Eq(avg->value, "  20.00"));
  const pm::Cell* fps = FindCell(m, "FPS");
  CHECK(fps != nullptr && Eq(fps->value, "  50"));
}

void TestEmptyProfilerShowsDashes() {
  current = "TestEmptyProfilerShowsDashes";
  pm::ProfilerModel m;
  FrameProfiler p;                        // sem amostras
  SysSnapshot sys;
  pm::Extras ex;
  m.refresh(0.0, p, sys, ex, Identity);
  const pm::Cell* avg = FindCell(m, "avg ms");
  CHECK(avg != nullptr && Eq(avg->value, "     --"));
  CHECK(m.rowCount() > 0);
  CHECK(!m.truncated());
}

void TestNanDoesNotBreakTextOrGraphs() {
  current = "TestNanDoesNotBreakTextOrGraphs";
  pm::ProfilerModel m;
  FrameProfiler p;
  SysSnapshot sys;
  sys.cpuOk = false;
  pm::Extras ex;
  p.frameMs[0] = kNaN;
  p.frameMs[1] = std::numeric_limits<float>::infinity();
  p.workMs[0] = kNaN;
  p.filled = 2;
  m.refresh(0.0, p, sys, ex, Identity);
  CHECK(m.rowCount() > 0);
  bool allFinite = true;
  for (int g = 0; g < pm::kGraphCount; ++g) {
    for (int i = 0; i < m.graphSamples(); ++i) allFinite = allFinite && std::isfinite(m.graph(g)[i]);
    CHECK(std::isfinite(m.graphScale(g)));
    CHECK(m.graphScale(g) > 0.f);
  }
  CHECK(allFinite);
  bool sawMeasuring = false;
  for (int i = 0; i < m.rowCount(); ++i) {
    if (std::strstr(m.row(i).text, "measuring") != nullptr) sawMeasuring = true;
  }
  CHECK(sawMeasuring);
}

void TestGraphScaleRisesAtOnceAndFallsSlowly() {
  current = "TestGraphScaleRisesAtOnceAndFallsSlowly";
  pm::ProfilerModel m;
  FrameProfiler p;
  SysSnapshot sys;
  pm::Extras ex;
  SetFrames(p, 16.0f);
  m.refresh(0.0, p, sys, ex, Identity);
  const float base = m.graphScale(0);
  CHECK(base >= 16.7f);

  SetFrames(p, 100.0f, 10);               // um pico de 100 ms: a escala sobe na hora
  p.frameMs[5] = 100.0f;
  double t = 0.25;
  m.refresh(t, p, sys, ex, Identity);
  const float peak = m.graphScale(0);
  CHECK(peak >= 110.0f - 1e-3f);

  // Pico some: a escala nao cai de uma vez; cada refresh tira no maximo ~3%.
  SetFrames(p, 16.0f);
  float prev = peak;
  bool slowFall = true;
  for (int i = 0; i < 40; ++i) {
    t += 0.25;
    m.refresh(t, p, sys, ex, Identity);
    const float cur = m.graphScale(0);
    if (cur < prev * pm::kScaleDecay - 1e-3f) slowFall = false;
    if (cur > prev + 1e-4f) slowFall = false;
    prev = cur;
  }
  CHECK(slowFall);
  CHECK(prev < peak);                     // acaba descendo
  CHECK(prev >= 16.7f - 1e-4f);           // nunca abaixo do piso
}

void TestStableScaleFunction() {
  current = "TestStableScaleFunction";
  CHECK(std::fabs(pm::ProfilerModel::stableScale(50.f, 20.f, 16.7f) - 48.5f) < 1e-3f);   // 50 * 0,97
  CHECK(std::fabs(pm::ProfilerModel::stableScale(20.f, 80.f, 16.7f) - 80.f) < 1e-4f);    // sobe na hora
  CHECK(std::fabs(pm::ProfilerModel::stableScale(kNaN, 20.f, 16.7f) - 20.f) < 1e-4f);    // NaN anterior
  // alvo NaN vira piso, mas a escala ainda cai so 3% por refresh (regra do cabecalho): 20 * 0,97.
  CHECK(std::fabs(pm::ProfilerModel::stableScale(20.f, kNaN, 16.7f) - 19.4f) < 1e-3f);
  CHECK(std::fabs(pm::ProfilerModel::stableScale(16.7f, kNaN, 16.7f) - 16.7f) < 1e-4f);  // no piso, fica no piso
}

void TestRowBufferDoesNotMove() {
  current = "TestRowBufferDoesNotMove";
  pm::ProfilerModel m;
  FrameProfiler p;
  SysSnapshot sys;
  pm::Extras ex;
  SetFrames(p, 10.0f);
  m.refresh(0.0, p, sys, ex, Identity);
  const pm::Row* first = &m.row(0);
  const float* g = m.graph(0);
  SetFrames(p, 30.0f);
  m.refresh(0.25, p, sys, ex, Identity);
  CHECK(&m.row(0) == first);              // mesmo endereco: nada foi realocado
  CHECK(m.graph(0) == g);
}

void TestZoneBarFractionsAreBounded() {
  current = "TestZoneBarFractionsAreBounded";
  pm::ProfilerModel m;
  FrameProfiler p;
  SysSnapshot sys;
  pm::Extras ex;
  SetFrames(p, 12.0f);
  p.add(editor::Zone::SceneRender, 500.0f);   // zona maior que o quadro
  p.filled = 1;
  m.refresh(0.0, p, sys, ex, Identity);
  bool bounded = true;
  int bars = 0;
  for (int i = 0; i < m.rowCount(); ++i) {
    const pm::Row& r = m.row(i);
    if (r.kind != pm::RowKind::Bar) continue;
    ++bars;
    bounded = bounded && r.fraction >= 0.f && r.fraction <= 1.f && std::isfinite(r.fraction);
  }
  CHECK(bars == static_cast<int>(editor::Zone::Count));
  CHECK(bounded);
}

void TestCpuAndCoresRows() {
  current = "TestCpuAndCoresRows";
  pm::ProfilerModel m;
  FrameProfiler p;
  SysSnapshot sys;
  sys.cpuOk = true;
  sys.cpuTotal = 37.5f;
  sys.core = {10.f, 90.f};
  sys.coresOnline = 2;
  sys.coresBusy = 1;
  pm::Extras ex;
  SetFrames(p, 16.0f);
  m.refresh(0.0, p, sys, ex, Identity);
  int coreRows = 0;
  for (int i = 0; i < m.rowCount(); ++i) {
    const pm::Row& r = m.row(i);
    if (r.kind == pm::RowKind::Bar && std::strncmp(r.text, "core", 4) == 0) ++coreRows;
  }
  CHECK(coreRows == 2);
  const pm::Cell* busy = FindCell(m, "cores in use (above 25%)");
  CHECK(busy != nullptr && Eq(busy->value, "  1"));
}

void TestGpuPassMetricsShowPerViewAndTotal() {
  current = "TestGpuPassMetricsShowPerViewAndTotal";
  pm::ProfilerModel m;
  FrameProfiler p;
  SysSnapshot sys;
  pm::Extras ex;
  ex.hasGpu = true;
  ex.renderStats.beginFrame();
  auto& scene = ex.renderStats.views[static_cast<std::size_t>(tool::render::RenderViewId::Scene)];
  scene.valid = true;
  scene.passes[static_cast<std::size_t>(tool::render::RenderPassId::Opaque)].recordDraw(6, 2);
  scene.shadowCache.pointFacesReused = 2;
  scene.shadowCache.pointFacesRendered = 1;
  auto& game = ex.renderStats.views[static_cast<std::size_t>(tool::render::RenderViewId::Game)];
  game.valid = true;
  game.passes[static_cast<std::size_t>(tool::render::RenderPassId::Opaque)].recordDraw(3, 1);
  game.shadowCache.pointFacesReused = 4;
  game.shadowCache.pointFacesCastersChanged = 3;
  m.refresh(0.0, p, sys, ex, Identity);

  bool sawScene = false, sawGame = false, sawSceneOpaque = false, sawGameOpaque = false, sawPassTotal = false;
  bool sawSceneCache = false, sawGameCache = false, sawCacheTotal = false;
  for (int i = 0; i < m.rowCount(); ++i) {
    const pm::Row& row = m.row(i);
    if (std::strstr(row.text, "Scene") != nullptr) sawScene = true;
    if (std::strstr(row.text, "Game") != nullptr) sawGame = true;
    if (std::strstr(row.text, "Opaque: 1 draws, 2 instances, 4 triangles") != nullptr) sawSceneOpaque = true;
    if (std::strstr(row.text, "Opaque: 1 draws, 1 instances, 1 triangles") != nullptr) sawGameOpaque = true;
    if (std::strstr(row.text, "Opaque: 2 draws, 3 instances, 5 triangles") != nullptr) sawPassTotal = true;
    if (std::strstr(row.text, "2 reused, 1 rendered") != nullptr) sawSceneCache = true;
    if (std::strstr(row.text, "3 caster changes") != nullptr) sawGameCache = true;
    if (std::strstr(row.text, "6 reused, 1 rendered") != nullptr) sawCacheTotal = true;
  }
  CHECK(sawScene && sawGame);
  CHECK(sawSceneOpaque && sawGameOpaque && sawPassTotal);
  CHECK(sawSceneCache && sawGameCache && sawCacheTotal);
  CHECK(!m.truncated());
}

}  // namespace

int main() {
  TestFormatFixedWidth();
  TestFormatSameLengthForManyValues();
  TestFormatNegativeZeroAndSuffix();
  TestFormatNanAndInfinity();
  TestFormatBufferTooSmall();
  TestRefreshRateAtFourHz();
  TestRefreshCountInOneSecond();
  TestRefreshRateIsConfigurable();
  TestCellsKeepFixedWidthAcrossRefreshes();
  TestNumbersShowWhenDataExists();
  TestEmptyProfilerShowsDashes();
  TestNanDoesNotBreakTextOrGraphs();
  TestGraphScaleRisesAtOnceAndFallsSlowly();
  TestStableScaleFunction();
  TestRowBufferDoesNotMove();
  TestZoneBarFractionsAreBounded();
  TestCpuAndCoresRows();
  TestGpuPassMetricsShowPerViewAndTotal();
  std::printf("Editor.ProfilerModel: %d checks, %d failed\n", run, failed);
  return failed == 0 ? 0 : 1;
}
