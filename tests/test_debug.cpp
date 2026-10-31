#include "test.hpp"
#include "tool/debug/debug.hpp"

#include <chrono>
#include <thread>

using namespace tool;

namespace {
struct Quiet {
  Quiet() { Logger::get().setSink([](LogLevel, std::string_view) {}); debug().reset(); }
  ~Quiet() { Logger::get().setSink(nullptr); }
};
} // namespace

TEST(debug_report_and_history) {
  Quiet q;
  debug().report("TOOL-E999", Severity::Error, "teste");
  CHECK(debug().hasCode("TOOL-E999"));
  CHECK(debug().count(Severity::Error) == 1);
}

TEST(debug_expect_reports_without_abort) {
  Quiet q;
  TOOL_EXPECT(1 + 1 == 3, "matematica quebrada");
#if TOOL_DEBUG_LEVEL >= 1
  CHECK(debug().hasCode(diag::kExpectFailed));
#else
  CHECK(!debug().hasCode(diag::kExpectFailed));
#endif
}

TEST(debug_zone_budget) {
  Quiet q;
  debug().setZoneBudgetMs("lento", 1.0);
  debug().recordZone("lento", 5.0);
  CHECK(debug().hasCode(diag::kBudgetZone));
  CHECK(debug().zone("lento").overBudget == 1);
  CHECK(debug().zone("lento").maxMs == 5.0);
}

TEST(debug_frame_budget) {
  Quiet q;
  debug().setFrameBudgetMs(1.0);
  debug().beginFrame();
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  debug().endFrame();
  CHECK(debug().hasCode(diag::kBudgetFrame));
  debug().setFrameBudgetMs(0);
}

TEST(debug_watchdog_fires_when_stalled) {
  Quiet q;
  debug().startWatchdog(40);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  debug().stopWatchdog();
  CHECK(debug().hasCode(diag::kWatchdog));
}

TEST(debug_watchdog_quiet_with_heartbeat) {
  Quiet q;
  debug().startWatchdog(100);
  for (int i = 0; i < 10; ++i) {
    debug().heartbeat();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  debug().stopWatchdog();
  CHECK(!debug().hasCode(diag::kWatchdog));
}
