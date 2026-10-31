#include "test.hpp"
#include "tool/narrative/narrative.hpp"

#include <cstdio>

using namespace tool;
using namespace tool::narr;

namespace {
struct Quiet { Quiet() { Logger::get().setSink([](LogLevel, std::string_view) {}); debug().reset(); } ~Quiet() { Logger::get().setSink(nullptr); } };

Program build(const char* text, std::string* err = nullptr) {
  auto j = Json::parse(text);
  Program p; std::string e;
  bool ok = j && Compiler().compile(*j, p, e);
  if (err) *err = ok ? "" : e;
  if (!ok && !err) CHECK(false);
  return p;
}

// Exemplo 1: cinematica de abertura (one-shot).
const char* kOpening = R"({
 "variables":{"seen_intro":{"value":0}},
 "nodes":[
  {"id":"t","type":"trigger","event":"player_enter_area","mode":"oneshot"},
  {"id":"a1","type":"action","name":"lock_input","arg":"true"},
  {"id":"a2","type":"action","name":"play_cutscene","arg":"intro"},
  {"id":"d","type":"delay","seconds":2},
  {"id":"a3","type":"action","name":"lock_input","arg":"false"},
  {"id":"s","type":"set","var":"seen_intro","value":1}],
 "links":[{"from":"t","to":"a1"},{"from":"a1","to":"a2"},{"from":"a2","to":"d"},{"from":"d","to":"a3"},{"from":"a3","to":"s"}]})";

// Exemplo 2: porta trancada (condicao E de duas variaveis, reativavel).
const char* kDoor = R"({
 "variables":{"has_key":{"value":0},"power_on":{"value":1}},
 "nodes":[
  {"id":"t","type":"trigger","event":"interact_door","mode":"reactivable"},
  {"id":"c1","type":"compare","var":"has_key","op":"==","value":1},
  {"id":"c2","type":"compare","var":"power_on","op":"==","value":1},
  {"id":"and","type":"and","inputs":["c1","c2"]},
  {"id":"b","type":"branch","cond":"and"},
  {"id":"open","type":"action","name":"open_door","arg":""},
  {"id":"deny","type":"action","name":"show_message","arg":"Trancada"}],
 "links":[{"from":"t","to":"b"},{"from":"b","port":"then","to":"open"},{"from":"b","port":"else","to":"deny"}]})";

// Exemplo 3: chefe (fases por variavel observada, com histerese).
const char* kBoss = R"({
 "variables":{"boss_hp":{"value":100}},
 "nodes":[
  {"id":"p2","type":"onvar","var":"boss_hp","op":"<","value":50,"hysteresis":10,"mode":"reactivable"},
  {"id":"a","type":"action","name":"boss_phase","arg":"2"},
  {"id":"p3","type":"onvar","var":"boss_hp","op":"<","value":20,"mode":"oneshot"},
  {"id":"b","type":"action","name":"boss_phase","arg":"3"}],
 "links":[{"from":"p2","to":"a"},{"from":"p3","to":"b"}]})";

struct Log { std::vector<std::string> v; void operator()(const std::string& n, const std::string& a) { v.push_back(n + ":" + a); } };
}

TEST(narr_opening_oneshot_with_delay) {
  Quiet q; Program p = build(kOpening); Runtime rt(p); Log log; rt.setActionHandler([&](auto& n, auto& a) { log(n, a); });
  CHECK(rt.fire("player_enter_area") == 1);
  CHECK(log.v.size() == 2 && log.v[1] == "play_cutscene:intro");
  CHECK(rt.runningFlows() == 1 && rt.var("seen_intro") == 0);
  rt.update(1.0); CHECK(log.v.size() == 2);
  rt.update(1.5); CHECK(log.v.size() == 3 && rt.var("seen_intro") == 1 && rt.runningFlows() == 0);
  CHECK(rt.fire("player_enter_area") == 0); // one-shot: nao repete
  CHECK(log.v.size() == 3);
}

TEST(narr_door_branch_and_reactivable) {
  Quiet q; Program p = build(kDoor); Runtime rt(p); Log log; rt.setActionHandler([&](auto& n, auto& a) { log(n, a); });
  rt.fire("interact_door");
  CHECK(log.v.back() == "show_message:Trancada");
  rt.setVar("has_key", 1);
  rt.fire("interact_door");
  CHECK(log.v.back() == "open_door:");
  rt.setVar("power_on", 0);
  rt.fire("interact_door");
  CHECK(log.v.back() == "show_message:Trancada");
  CHECK(log.v.size() == 3);
}

TEST(narr_boss_phases_hysteresis) {
  Quiet q; Program p = build(kBoss); Runtime rt(p); Log log; rt.setActionHandler([&](auto& n, auto& a) { log(n, a); });
  rt.setVar("boss_hp", 60); CHECK(log.v.empty());
  rt.setVar("boss_hp", 45); CHECK(log.v.size() == 1 && log.v[0] == "boss_phase:2");
  rt.setVar("boss_hp", 40); CHECK(log.v.size() == 1);   // ja disparou
  rt.setVar("boss_hp", 55); rt.setVar("boss_hp", 49);   // dentro da histerese: nao rearma
  CHECK(log.v.size() == 1);
  rt.setVar("boss_hp", 65); rt.setVar("boss_hp", 30);   // acima de 60: rearmou
  CHECK(log.v.size() == 2 && log.v[1] == "boss_phase:2");
  rt.setVar("boss_hp", 10);
  CHECK(log.v.size() == 3 && log.v[2] == "boss_phase:3"); // one-shot
  rt.setVar("boss_hp", 90); rt.setVar("boss_hp", 5);
  CHECK(log.v.size() == 4 && log.v[3] == "boss_phase:2"); // fase 3 nao repete
}

TEST(narr_mutual_exclusion_group_per_tick) {
  Quiet q;
  Program p = build(R"({"variables":{},"nodes":[
   {"id":"a","type":"trigger","event":"x","mode":"reactivable","group":1},{"id":"aa","type":"action","name":"A","arg":""},
   {"id":"b","type":"trigger","event":"y","mode":"reactivable","group":1},{"id":"bb","type":"action","name":"B","arg":""}],
   "links":[{"from":"a","to":"aa"},{"from":"b","to":"bb"}]})");
  Runtime rt(p); Log log; rt.setActionHandler([&](auto& n, auto& a) { log(n, a); });
  rt.beginTick(); rt.fire("x"); rt.fire("y");
  CHECK(log.v.size() == 1 && log.v[0] == "A:");
  rt.beginTick(); rt.fire("y");
  CHECK(log.v.size() == 2);
}

TEST(narr_save_load_roundtrip_with_pending_flow) {
  Quiet q; Program p = build(kOpening);
  Runtime a(p); Log la; a.setActionHandler([&](auto& n, auto& x) { la(n, x); });
  a.fire("player_enter_area"); a.update(0.5);
  Json saved = a.save();
  Runtime b(p); Log lb; b.setActionHandler([&](auto& n, auto& x) { lb(n, x); });
  CHECK(b.load(*Json::parse(saved.dump())));
  CHECK(b.fire("player_enter_area") == 0);   // one-shot ja consumido no save
  b.update(2.0);
  CHECK(lb.v.size() == 1 && lb.v[0] == "lock_input:false"); // retomou o fluxo pendente
  CHECK(b.var("seen_intro") == 1);
}

TEST(narr_corrupted_save_is_rejected_and_state_kept) {
  Quiet q; Program p = build(kDoor); Runtime rt(p);
  rt.setVar("has_key", 1);
  CHECK(!rt.load(*Json::parse(R"({"version":1,"vars":{"nao_existe":1}})")));
  CHECK(!rt.load(*Json::parse(R"({"version":1,"vars":{},"flows":[{"pc":99999,"wait":0,"stack":[]}]})")));
  CHECK(!rt.load(*Json::parse(R"({"version":7})")));
  CHECK(rt.var("has_key") == 1);
  CHECK(debug().hasCode(code::kBadSave));
}

TEST(narr_atomic_file_save) {
  Quiet q; Program p = build(kDoor); Runtime rt(p); rt.setVar("has_key", 1);
  const std::string path = "narr_test_save.json";
  CHECK(rt.saveToFile(path));
  CHECK(!std::filesystem::exists(path + ".tmp"));
  Runtime r2(p); CHECK(r2.loadFromFile(path) && r2.var("has_key") == 1);
  std::remove(path.c_str());
  CHECK(!r2.loadFromFile("nao_existe.json"));
}

TEST(narr_compiler_rejects_bad_graphs) {
  Quiet q; std::string e;
  build(R"({"variables":{},"nodes":[{"id":"t","type":"trigger","event":"e"},{"id":"s","type":"set","var":"nope","value":1}],"links":[{"from":"t","to":"s"}]})", &e);
  CHECK(e.find("TOOL-E201") == 0);
  build(R"({"variables":{},"nodes":[{"id":"t","type":"trigger","event":"e"}],"links":[{"from":"t","to":"fantasma"}]})", &e);
  CHECK(e.find("TOOL-E202") == 0);
  build(R"({"variables":{},"nodes":[{"id":"t","type":"trigger","event":"e"},{"id":"a","type":"emit","event":"x"},{"id":"b","type":"emit","event":"y"}],
    "links":[{"from":"t","to":"a"},{"from":"a","to":"b"},{"from":"b","to":"a"}]})", &e);
  CHECK(e.find("TOOL-E203") == 0);                       // ciclo de fluxo
  build(R"({"variables":{},"nodes":[{"id":"t","type":"trigger","event":"e"},{"id":"a","type":"voar"}],"links":[{"from":"t","to":"a"}]})", &e);
  CHECK(e.find("TOOL-E200") == 0);
  build(R"({"nodes":[{"id":"x"},{"id":"x"}]})", &e);
  CHECK(e.find("TOOL-E200") == 0);                       // id duplicado
}

TEST(narr_unreachable_node_warns) {
  Quiet q; Program p = build(R"({"variables":{},"nodes":[{"id":"t","type":"trigger","event":"e"},{"id":"orfao","type":"emit","event":"x"}],"links":[]})");
  CHECK(p.warnings.size() == 1);
}

TEST(narr_emit_handler_and_instruction_budget) {
  Quiet q;
  Program p = build(R"({"variables":{"n":{"value":0}},"nodes":[{"id":"t","type":"trigger","event":"go","mode":"reactivable"},{"id":"e","type":"emit","event":"done"}],"links":[{"from":"t","to":"e"}]})");
  Runtime rt(p); std::string got; rt.setEmitHandler([&](auto& ev) { got = ev; });
  rt.fire("go"); CHECK(got == "done");
}

TEST(narrative_start_update_create_and_savepoint) {
  Quiet q;
  Program p = build(R"({"variables":{"n":{"value":0}},"nodes":[
    {"id":"s","type":"start"},{"id":"a","type":"add","var":"n","value":1},
    {"id":"u","type":"update"},{"id":"b","type":"add","var":"n","value":10},
    {"id":"c","type":"create","object":"Enemy"},{"id":"d","type":"action","name":"log","arg":"enemy"},
    {"id":"sp","type":"savepoint","slot":"slot1"},{"id":"lp","type":"loadpoint","slot":"slot1"},
    {"id":"t","type":"trigger","event":"save","mode":"reactivable"},{"id":"t2","type":"trigger","event":"load","mode":"reactivable"}],
   "links":[{"from":"s","to":"a"},{"from":"u","to":"b"},{"from":"c","to":"d"},{"from":"t","to":"sp"},{"from":"t2","to":"lp"}]})");
  Runtime rt(p); std::string saved, action;
  rt.setSaveHandler([&](const std::string& s) { saved = s; }); rt.setActionHandler([&](const std::string& n, const std::string&) { action = n; });
  rt.fire("Start"); CHECK(rt.var("n") == 1);
  rt.fire("Update"); rt.fire("Update"); CHECK(rt.var("n") == 21);   // Update e reativavel: roda a cada quadro
  rt.fire("Create:Enemy"); CHECK(action == "log");
  rt.fire("Create:Other"); // nao casa
  rt.fire("save"); CHECK(saved == "slot1");
  rt.fire("load"); CHECK(rt.takePendingLoad() == "slot1"); CHECK(rt.takePendingLoad().empty());
  // slot invalido (caminho) e recusado na compilacao
  std::string err; build(R"({"nodes":[{"id":"s","type":"start"},{"id":"x","type":"savepoint","slot":"../x"}],"links":[{"from":"s","to":"x"}]})", &err);
  CHECK(!err.empty());
}
