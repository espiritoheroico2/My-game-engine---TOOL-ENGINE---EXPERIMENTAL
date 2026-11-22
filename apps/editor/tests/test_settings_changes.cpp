// Teste da regra que grava os pedidos da janela Configuracoes no Editor (settings_changes.hpp). Usa um alvo falso
// que guarda o que foi chamado. Exemplo: pedido de API 2 com a API atual 0 chama applyApi(2) uma vez.
#include "settings_changes.hpp"

#include <cstdio>
#include <vector>

namespace {

using namespace editor::settings_changes;

int run = 0;
int failed = 0;

void Check(bool ok, const char* expr, int line) {
  ++run;
  if (!ok) {
    ++failed;
    std::fprintf(stderr, "[FAIL] line %d: %s\n", line, expr);
  }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)

// Alvo falso: cada metodo anota a chamada (nome e valor). Os valores ficam no ultimo pedido de cada campo.
struct RecordingTarget {
  int api = 0;                 // API atual do Editor
  std::vector<int> appliedApis;
  int tab = -1;
  int shadowQuality = -1;
  float renderScale = -1.f;
  bool shadows = false;
  bool rayTracing = true;
  bool rtAo = false;
  int chunkCacheMax = -1;
  float interfaceScale = -1.f;
  float buttonRadius = -1.f, windowRadius = -1.f, textScale = -1.f;
  bool idleRendering = false;
  int calls = 0;

  int apiRequested() const { return api; }
  void applyApi(int a) { appliedApis.push_back(a); ++calls; }
  void setTab(int i) { tab = i; ++calls; }
  void setResMode(int) { ++calls; }
  void setCustomW(int) { ++calls; }
  void setCustomH(int) { ++calls; }
  void setRenderScale(float s) { renderScale = s; ++calls; }
  void setUpscale(int) { ++calls; }
  void setAa(int) { ++calls; }
  void setPixelFilter(bool) { ++calls; }
  void setFps(int) { ++calls; }
  void setShadows(bool on) { shadows = on; ++calls; }
  void setRayTracing(bool on) { rayTracing = on; ++calls; }
  void setRtAo(bool on) { rtAo = on; ++calls; }
  void setShadowQuality(int i) { shadowQuality = i; ++calls; }
  void setChunkCacheMax(int i) { chunkCacheMax = i; ++calls; }
  void setInterfaceScale(float s) { interfaceScale = s; ++calls; }
  void setButtonRadius(float v) { buttonRadius = v; ++calls; }
  void setWindowRadius(float v) { windowRadius = v; ++calls; }
  void setTextScale(float v) { textScale = v; ++calls; }
  void setIdleRendering(bool on) { idleRendering = on; ++calls; }
  void setPalette(int) { ++calls; }
};

void TestApiChangeCallsApplyWithRequestedValue() {
  RecordingTarget t;
  t.api = 0;
  ApplyChanges(t, Requests{Change{Field::Api, 2, 0.f, false}});
  CHECK(t.appliedApis.size() == 1 && t.appliedApis[0] == 2);
}

void TestSameApiDoesNotApply() {
  RecordingTarget t;
  t.api = 1;
  ApplyChanges(t, Requests{Change{Field::Api, 1, 0.f, false}});
  CHECK(t.appliedApis.empty());
}

void TestTabAndShadowQualityPassIndex() {
  RecordingTarget t;
  ApplyChanges(t, Requests{Change{Field::Tab, 2, 0.f, false}, Change{Field::ShadowQuality, 3, 0.f, false}});
  CHECK(t.tab == 2);
  CHECK(t.shadowQuality == 3);
}

void TestRenderScaleAndShadowsPassValues() {
  RecordingTarget t;
  ApplyChanges(t, Requests{Change{Field::RenderScale, 0, 1.5f, false}, Change{Field::Shadows, 0, 0.f, true}});
  CHECK(t.renderScale == 1.5f);
  CHECK(t.shadows);
}

void TestOptimizingRequestsPassValues() {
  RecordingTarget t;
  ApplyChanges(t, Requests{
      Change{Field::ChunkCacheMax, 16384, 0.f, false},
      Change{Field::InterfaceScale, 0, 1.25f, false},
      Change{Field::IdleRendering, 0, 0.f, true}});
  CHECK(t.chunkCacheMax == 16384);
  CHECK(t.interfaceScale == 1.25f);
  CHECK(t.idleRendering);
}

void TestEmptyRequestsDoNothing() {
  RecordingTarget t;
  ApplyChanges(t, Requests{});
  CHECK(t.calls == 0);
}

} // namespace

int main() {
  TestApiChangeCallsApplyWithRequestedValue();
  TestSameApiDoesNotApply();
  TestTabAndShadowQualityPassIndex();
  TestRenderScaleAndShadowsPassValues();
  TestOptimizingRequestsPassValues();
  TestEmptyRequestsDoNothing();
  std::printf("settings changes: %d/%d checks passed\n", run - failed, run);
  return failed == 0 ? 0 : 1;
}
