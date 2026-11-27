// Fisica da cena (Settings > Physics) e tabela de zonas do Profiler. Sem janela. Roda no ctest como Editor.PhysicsSettings.
#include "profiler.hpp"
#include "project.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {
int run = 0, failed = 0;
void Check(bool ok, const char* expr, int line) {
  ++run;
  if (!ok) { ++failed; std::fprintf(stderr, "[FAIL] linha %d: %s\n", line, expr); }
}
#define CHECK(e) Check((e), #e, __LINE__)
}

int main() {
  using editor::PhysicsSettings;

  // (a) cena antiga, sem as chaves novas: abre com os padroes e preserva o que ja existia.
  {
    const auto j = tool::Json::parse(R"({"enabled":false,"solverIterations":20,"stepHz":120,"simRadius":50})");
    CHECK(j.has_value());
    const PhysicsSettings p = editor::loadPhysicsSettings(*j);
    CHECK(!p.enabled && p.solverIterations == 20 && p.stepHz == 120 && p.simRadius == 50.f);
    CHECK(p.softStep && p.warmStarting && p.islands && p.interpolation);
    CHECK(p.contactHertz == 30.f && p.contactDampingRatio == 1.f);
    const auto empty = tool::Json::parse("{}");
    const PhysicsSettings d = editor::loadPhysicsSettings(*empty);
    CHECK(d.softStep && d.interpolation && d.contactHertz == 30.f);
  }
  // (b) valores fora da faixa sao limitados (leitura e funcao pura).
  {
    const auto j = tool::Json::parse(R"({"contactHertz":1000,"contactDampingRatio":-3})");
    const PhysicsSettings p = editor::loadPhysicsSettings(*j);
    CHECK(p.contactHertz == 120.f && p.contactDampingRatio == 0.f);
    const auto k = tool::Json::parse(R"({"contactHertz":0.5,"contactDampingRatio":99})");
    const PhysicsSettings q = editor::loadPhysicsSettings(*k);
    CHECK(q.contactHertz == 5.f && q.contactDampingRatio == 5.f);
    PhysicsSettings n; n.contactHertz = std::numeric_limits<float>::quiet_NaN(); n.contactDampingRatio = std::numeric_limits<float>::infinity();
    editor::clampPhysicsSettings(n);
    CHECK(n.contactHertz == 30.f && n.contactDampingRatio == 1.f);
  }
  // (c) ida-e-volta de escrita/leitura mantem os valores.
  {
    PhysicsSettings a; a.softStep = false; a.warmStarting = false; a.islands = false; a.interpolation = false;
    a.contactHertz = 60.f; a.contactDampingRatio = 2.5f; a.simRadius = 12.f; a.solverIterations = 7;
    const auto text = editor::savePhysicsSettings(a).dump();
    const auto parsed = tool::Json::parse(text);
    CHECK(parsed.has_value());
    const PhysicsSettings b = editor::loadPhysicsSettings(*parsed);
    CHECK(!b.softStep && !b.warmStarting && !b.islands && !b.interpolation);
    CHECK(b.contactHertz == 60.f && b.contactDampingRatio == 2.5f && b.simRadius == 12.f && b.solverIterations == 7);
    PhysicsSettings def; const PhysicsSettings c = editor::loadPhysicsSettings(*tool::Json::parse(editor::savePhysicsSettings(def).dump()));
    CHECK(c.softStep && c.warmStarting && c.islands && c.interpolation && c.contactHertz == 30.f && c.contactDampingRatio == 1.f);
  }
  // Tabela de zonas: as fases da fisica existem, na ordem, com nome nao vazio e distinto.
  {
    using editor::Zone;
    const Zone phases[] = {Zone::PhysicsIntegrate, Zone::PhysicsPrepare, Zone::PhysicsBroadphase, Zone::PhysicsNarrowphase,
                           Zone::PhysicsSolver, Zone::PhysicsCCD, Zone::PhysicsFinish};
    for (int i = 0; i < 7; ++i) {
      CHECK(static_cast<int>(phases[i]) == static_cast<int>(Zone::PhysicsIntegrate) + i);
      CHECK(editor::zoneName(phases[i])[0] != '\0');
      for (int k = 0; k < i; ++k) CHECK(std::strcmp(editor::zoneName(phases[i]), editor::zoneName(phases[k])) != 0);
    }
    CHECK(static_cast<int>(Zone::PhysicsFinish) + 1 == static_cast<int>(Zone::Count));
    for (int z = 0; z < static_cast<int>(Zone::Count); ++z) CHECK(editor::zoneName(static_cast<Zone>(z))[0] != '\0');
    editor::FrameProfiler pf; pf.beginFrame(); pf.add(Zone::PhysicsSolver, 1.5f); pf.beginFrame();
    CHECK(pf.zoneAvg(Zone::PhysicsSolver) > 0.f);
  }
  std::printf("physics settings tests: %d checks, %d failed\n", run, failed);
  return failed == 0 ? 0 : 1;
}
