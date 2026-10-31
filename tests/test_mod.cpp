#include "test.hpp"
#include "tool/mod/mod.hpp"

#include <stdexcept>
#include <thread>

using namespace tool;
using namespace tool::mod;

namespace {
struct Quiet { Quiet() { Logger::get().setSink([](LogLevel, std::string_view) {}); debug().reset(); } ~Quiet() { Logger::get().setSink(nullptr); } };
Manifest man(const std::string& id, u32 perms) { Manifest m; m.id = id; m.version = "1.0.0"; m.permissions = perms; return m; }
}

TEST(mod_manifest_validation) {
  Quiet q; ModTool t; Manifest m;
  auto ok = Json::parse(R"({"id":"my-mod","version":"1.2.3","api":1,"permissions":["read_world","audio"]})");
  CHECK(t.parseManifest(*ok, Trust::Community, m) && m.id == "my-mod" && (m.permissions & kPermAudio));
  for (const char* bad : {R"({"id":"Mod Ruim!","version":"1.0.0"})", R"({"id":"x","version":"1.0"})", R"({"id":"x","version":"1.0.0","api":99})",
                          R"({"id":"x","version":"1.0.0","permissions":["root"]})", "[]"})
    CHECK(!t.parseManifest(*Json::parse(bad), Trust::Core, m));
}

TEST(mod_permissions_capped_by_trust) {
  Quiet q; ModTool t; Manifest m;
  auto j = Json::parse(R"({"id":"sketchy","version":"1.0.0","permissions":["network"]})");
  CHECK(!t.parseManifest(*j, Trust::Untrusted, m));
  CHECK(!t.parseManifest(*j, Trust::Community, m));
  CHECK(t.parseManifest(*j, Trust::Core, m));
  CHECK(debug().hasCode(code::kPermDenied));
}

TEST(mod_override_tool_replaces_behavior) {
  Quiet q; ModTool t;
  t.defineTool("enemy", [](Json& c) { c.set("hp", 100); });
  t.load(man("hard", kPermWriteWorld), Trust::Community);
  Json c = Json::object(); t.runTool("enemy", c); CHECK(c["hp"].asNumber() == 100);
  CHECK(t.overrideTool("hard", "enemy", [](Json& c2) { c2.set("hp", 500); }));
  Json c2 = Json::object(); t.runTool("enemy", c2); CHECK(c2["hp"].asNumber() == 500);
  t.unload("hard");
  Json c3 = Json::object(); t.runTool("enemy", c3); CHECK(c3["hp"].asNumber() == 100); // desfeito
}

TEST(mod_sealed_tag_cannot_be_overridden) {
  Quiet q; ModTool t;
  t.defineTool("save_system", [](Json&) {}, false);
  t.load(man("evil", kPermWriteWorld), Trust::Community);
  CHECK(!t.overrideTool("evil", "save_system", [](Json&) {}));
  CHECK(debug().hasCode(code::kSealed));
}

TEST(mod_override_requires_permission) {
  Quiet q; ModTool t;
  t.defineTool("enemy", [](Json&) {});
  t.load(man("readonly", kPermReadWorld), Trust::Community);
  CHECK(!t.overrideTool("readonly", "enemy", [](Json&) {}));
  CHECK(!t.overrideTool("desconhecido", "enemy", [](Json&) {}));
  CHECK(debug().hasCode(code::kPermDenied));
}

TEST(mod_priority_and_tie_warning) {
  Quiet q; ModTool t;
  t.defineTool("x", [](Json& c) { c.set("w", "base"); });
  t.load(man("a", kPermWriteWorld), Trust::Community); t.load(man("b", kPermWriteWorld), Trust::Community);
  t.overrideTool("a", "x", [](Json& c) { c.set("w", "a"); }, 5);
  t.overrideTool("b", "x", [](Json& c) { c.set("w", "b"); }, 1);
  Json c = Json::object(); t.runTool("x", c); CHECK(c["w"].asString() == "a");
  CHECK(!debug().hasCode(code::kPriorityTie));
  t.unload("a"); t.load(man("c", kPermWriteWorld), Trust::Community);
  t.overrideTool("c", "x", [](Json& cc) { cc.set("w", "c"); }, 1);
  Json d = Json::object(); t.runTool("x", d); CHECK(d["w"].asString() == "c" && debug().hasCode(code::kPriorityTie)); // empate: ultimo vence
}

TEST(mod_faulty_mod_falls_back_and_is_disabled) {
  Quiet q; ModTool t;
  t.defineTool("x", [](Json& c) { c.set("w", "base"); });
  t.load(man("bug", kPermWriteWorld), Trust::Community);
  t.overrideTool("bug", "x", [](Json& c) { c.set("w", "meio"); throw std::runtime_error("boom"); });
  for (int i = 0; i < 3; ++i) { Json c = Json::object(); t.runTool("x", c); CHECK(c["w"].asString() == "base"); } // efeito parcial desfeito
  CHECK(t.faults("bug") == 3 && !t.enabled("bug"));
  CHECK(debug().hasCode(code::kModDisabled));
  Json c = Json::object(); t.runTool("x", c); CHECK(c["w"].asString() == "base");
  CHECK(t.faults("bug") == 3); // desligado: nem executa mais
}

TEST(mod_slow_mod_is_penalized) {
  Quiet q; ModTool t; t.setCallBudgetMs(5);
  t.defineTool("x", [](Json&) {});
  t.load(man("slow", kPermWriteWorld), Trust::Community);
  t.overrideTool("slow", "x", [](Json&) { std::this_thread::sleep_for(std::chrono::milliseconds(20)); });
  Json c = Json::object(); t.runTool("x", c);
  CHECK(t.faults("slow") == 1 && debug().hasCode(code::kSlow));
}

TEST(mod_function_chain_with_next) {
  Quiet q; ModTool t;
  t.defineFunction("damage", [](const Args& a) { return Json(a["base"].asNumber()); });
  t.load(man("armor", kPermScript), Trust::Community); t.load(man("crit", kPermScript), Trust::Community);
  t.modFunction("armor", "damage", [](const Args& a, const FunctionImpl& next) { return Json(next(a).asNumber() - 3); }, 1);
  t.modFunction("crit", "damage", [](const Args& a, const FunctionImpl& next) { return Json(next(a).asNumber() * 2); }, 2);
  Json a = Json::object(); a.set("base", 10);
  CHECK(t.call("damage", a).asNumber() == (10 - 3) * 2); // crit(armor(base))
}

TEST(mod_function_failure_skips_to_next) {
  Quiet q; ModTool t;
  t.defineFunction("f", [](const Args&) { return Json(1); });
  t.load(man("bad", kPermScript), Trust::Community);
  t.modFunction("bad", "f", [](const Args&, const FunctionImpl&) -> Json { throw std::runtime_error("x"); });
  CHECK(t.call("f", Json::object()).asNumber() == 1);
  CHECK(!t.modFunction("bad", "naoexiste", [](const Args&, const FunctionImpl&) { return Json(); }));
}

TEST(mod_duplicate_load_rejected) {
  Quiet q; ModTool t;
  CHECK(t.load(man("a", 0), Trust::Community));
  CHECK(!t.load(man("a", 0), Trust::Community));
}
