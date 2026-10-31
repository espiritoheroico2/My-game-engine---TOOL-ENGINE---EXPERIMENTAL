#include "test.hpp"
#include "tool/events/events.hpp"

using namespace tool;

namespace { struct Quiet { Quiet() { Logger::get().setSink([](LogLevel, std::string_view) {}); debug().reset(); } ~Quiet() { Logger::get().setSink(nullptr); } }; }

TEST(events_oneshot_fires_once) {
  Quiet q; EventBus bus;
  EventId e = bus.intern("cutscene");
  bus.arbiter().setRule(e, {0, true, 0});
  int n = 0; bus.subscribe(e, [&](const std::any&) { ++n; });
  CHECK(bus.emit(e, 0) == Verdict::Allowed);
  CHECK(bus.emit(e, 1) == Verdict::AlreadyConsumed);
  CHECK(n == 1);
  bus.arbiter().resetOneShot(e);
  CHECK(bus.emit(e, 2) == Verdict::Allowed);
}

TEST(events_group_mutual_exclusion_per_tick) {
  Quiet q; EventBus bus;
  EventId a = bus.intern("door_open"), b = bus.intern("door_locked");
  bus.arbiter().setRule(a, {1, false, 0});
  bus.arbiter().setRule(b, {1, false, 0});
  CHECK(bus.emit(a, 0) == Verdict::Allowed);
  CHECK(bus.emit(b, 0) == Verdict::GroupBusy);
  CHECK(debug().hasCode(diag::kEventConflict));
  bus.arbiter().beginTick();
  CHECK(bus.emit(b, 1) == Verdict::Allowed);
}

TEST(events_cooldown) {
  Quiet q; EventBus bus;
  EventId e = bus.intern("hit");
  bus.arbiter().setRule(e, {0, false, 1.0});
  CHECK(bus.emit(e, 0.0) == Verdict::Allowed);
  CHECK(bus.emit(e, 0.5) == Verdict::Cooldown);
  CHECK(bus.emit(e, 1.5) == Verdict::Allowed);
}

TEST(events_cycle_detected) {
  Quiet q; EventBus bus;
  EventId a = bus.intern("a"), b = bus.intern("b");
  Verdict inner = Verdict::Allowed;
  bus.subscribe(a, [&](const std::any&) { bus.emit(b, 0); });
  bus.subscribe(b, [&](const std::any&) { inner = bus.emit(a, 0); });
  bus.emit(a, 0);
  CHECK(inner == Verdict::Cycle);
  CHECK(debug().hasCode(diag::kEventLoop));
}

TEST(events_duplicate_message_ignored) {
  Quiet q; EventBus bus;
  EventId e = bus.intern("net_msg");
  CHECK(bus.emit(e, 0, {}, 42) == Verdict::Allowed);
  CHECK(bus.emit(e, 0, {}, 42) == Verdict::Duplicate);
}

TEST(events_payload_delivered) {
  Quiet q; EventBus bus;
  EventId e = bus.intern("dmg");
  int got = 0; bus.subscribe(e, [&](const std::any& p) { got = std::any_cast<int>(p); });
  bus.emit(e, 0, 7);
  CHECK(got == 7);
}
