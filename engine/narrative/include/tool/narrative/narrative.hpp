// ToolEngine - Controlador Narrativo (plugin de scripts visuais por nos).
//   grafo (.json, formato nativo)  --compile-->  Program (IR compacto)  --run-->  NarrativeRuntime
// O editor visual (imgui-node-editor) edita o mesmo .json. O jogo exportado so carrega o Program.
// Gatilhos: evento nomeado ou variavel observada (com histerese). Modos: OneShot, Session, Reactivable.
#pragma once
#include "tool/core/json.hpp"
#include "tool/debug/debug.hpp"

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace tool::narr {

enum class Mode : u8 { OneShot, Session, Reactivable };
enum class Cmp : u8 { Eq, Ne, Lt, Le, Gt, Ge };

namespace code { // codigos de diagnostico do compilador
inline constexpr const char* kBadGraph = "TOOL-E200";
inline constexpr const char* kUnknownVar = "TOOL-E201";
inline constexpr const char* kUnknownNode = "TOOL-E202";
inline constexpr const char* kFlowCycle = "TOOL-E203";
inline constexpr const char* kCallDepth = "TOOL-E204";
inline constexpr const char* kBadSave = "TOOL-E205";
inline constexpr const char* kUnreachable = "TOOL-W201";
} // namespace code

// ---- IR ----------------------------------------------------------------------------------
enum class Op : u8 { SetVar, AddVar, Emit, Action, JumpIfNot, Call, Ret, Delay, SavePoint, LoadPoint };
struct Instr { Op op; i32 a = 0; i32 b = 0; double v = 0; };

enum class ExprOp : u8 { PushVar, PushConst, Cmp, And, Or, Not };
struct ExprInstr { ExprOp op; i32 a = 0; double v = 0; };

struct Entry {
  bool onVar = false;
  std::string event;            // gatilho por evento
  i32 var = -1; Cmp cmp = Cmp::Lt; double threshold = 0, hysteresis = 0; // gatilho por variavel
  Mode mode = Mode::OneShot;
  i32 group = 0;                // exclusao mutua por tick (0 = nenhum)
  i32 pc = -1;                  // onde comeca o fluxo (-1 = sem acoes)
  std::string id;               // id do no (para o save)
};

struct Program {
  std::vector<std::string> varNames;
  std::vector<double> varInit;
  std::vector<std::string> strings; // eventos / nomes de acao / argumentos
  std::vector<Instr> code;
  std::vector<std::vector<ExprInstr>> exprs;
  std::vector<Entry> entries;
  std::vector<std::string> warnings;
};

// ---- Compilador (grafo -> Program) -------------------------------------------------------------
class Compiler {
public:
  // Retorna false e preenche `error` se o grafo for invalido. Nunca lanca.
  bool compile(const Json& g, Program& out, std::string& error) {
    out = {}; g_ = &g; p_ = &out; err_ = &error; error.clear();
    if (!g.isObject() || !g["nodes"].isArray()) return fail(code::kBadGraph, "grafo sem lista de nos");
    for (auto& [name, v] : g["variables"].members()) {
      varIdx_[name] = static_cast<i32>(p_->varNames.size());
      p_->varNames.push_back(name);
      p_->varInit.push_back(v["value"].isBool() ? (v["value"].asBool() ? 1.0 : 0.0) : v["value"].asNumber());
    }
    for (auto& n : g["nodes"].items()) {
      const std::string id = n["id"].asString();
      if (id.empty() || nodes_.count(id)) return fail(code::kBadGraph, "no sem id ou id duplicado: '" + id + "'");
      nodes_[id] = &n;
    }
    for (auto& l : g["links"].items()) {
      if (!nodes_.count(l["from"].asString()) || !nodes_.count(l["to"].asString()))
        return fail(code::kUnknownNode, "link para no inexistente: " + l["from"].asString() + " -> " + l["to"].asString());
      out_[l["from"].asString() + "|" + l["port"].asString()].push_back(l["to"].asString());
    }
    std::set<std::string> reached;
    for (auto& n : g["nodes"].items()) {
      const std::string type = n["type"].asString();
      if (!isEntry(type)) continue;
      Entry e;
      e.id = n["id"].asString();
      const std::string m = n["mode"].asString();
      e.mode = m == "reactivable" ? Mode::Reactivable : m == "session" ? Mode::Session : Mode::OneShot;
      if (type == "update") e.mode = Mode::Reactivable; // roda todo quadro
      e.group = static_cast<i32>(n["group"].asNumber());
      if (type == "trigger") e.event = n["event"].asString();
      else if (type == "start") e.event = "Start";                                   // ao iniciar o jogo (Play)
      else if (type == "update") e.event = "Update";                                  // todo quadro
      else if (type == "create") e.event = n["object"].asString().empty() ? "Create" : "Create:" + n["object"].asString(); // quando um objeto (com esse nome) e criado
      else {
        e.onVar = true;
        if (!lookupVar(n["var"].asString(), e.var)) return false;
        if (!parseCmp(n["op"].asString(), e.cmp)) return fail(code::kBadGraph, "operador invalido em " + e.id);
        e.threshold = n["value"].asNumber(); e.hysteresis = n["hysteresis"].asNumber();
      }
      stack_.clear();
      e.pc = children(e.id, "", reached);
      if (!err_->empty()) return false;
      p_->entries.push_back(e);
    }
    for (auto& n : g["nodes"].items())
      if (!isEntry(n["type"].asString()) && !isCondition(n["type"].asString()) &&
          !reached.count(n["id"].asString()))
        p_->warnings.push_back(std::string(code::kUnreachable) + " no inalcancavel: " + n["id"].asString());
    return true;
  }

private:
  bool fail(const char* c, const std::string& m) { *err_ = std::string(c) + " " + m; return false; }
  static bool isEntry(const std::string& t) { return t == "trigger" || t == "onvar" || t == "start" || t == "update" || t == "create"; }
  static bool isCondition(const std::string& t) { return t == "compare" || t == "and" || t == "or" || t == "not"; }
  bool lookupVar(const std::string& n, i32& idx) {
    auto it = varIdx_.find(n);
    if (it == varIdx_.end()) { fail(code::kUnknownVar, "variavel nao declarada: '" + n + "'"); return false; }
    idx = it->second; return true;
  }
  static bool parseCmp(const std::string& s, Cmp& c) {
    if (s == "==") c = Cmp::Eq; else if (s == "!=") c = Cmp::Ne; else if (s == "<") c = Cmp::Lt;
    else if (s == "<=") c = Cmp::Le; else if (s == ">") c = Cmp::Gt; else if (s == ">=") c = Cmp::Ge; else return false;
    return true;
  }
  i32 str(const std::string& s) {
    for (size_t i = 0; i < p_->strings.size(); ++i) if (p_->strings[i] == s) return static_cast<i32>(i);
    p_->strings.push_back(s); return static_cast<i32>(p_->strings.size() - 1);
  }

  // Emite os filhos de (id, porta) como sequencia de Calls num bloco proprio. Retorna o pc (ou -1 se nao ha filhos).
  i32 children(const std::string& id, const std::string& port, std::set<std::string>& reached) {
    auto it = out_.find(id + "|" + port);
    if (it == out_.end() || it->second.empty()) return -1;
    std::vector<i32> targets;
    for (auto& c : it->second) { i32 pc = block(c, reached); if (!err_->empty()) return -1; targets.push_back(pc); }
    const i32 start = static_cast<i32>(p_->code.size());
    for (i32 t : targets) p_->code.push_back({Op::Call, t});
    p_->code.push_back({Op::Ret});
    return start;
  }

  i32 block(const std::string& id, std::set<std::string>& reached) {
    if (auto it = pcOf_.find(id); it != pcOf_.end()) return it->second;
    for (auto& s : stack_) if (s == id) { fail(code::kFlowCycle, "ciclo no fluxo passando por '" + id + "'"); return -1; }
    if (stack_.size() > 256) { fail(code::kCallDepth, "grafo profundo demais"); return -1; }
    stack_.push_back(id);
    reached.insert(id);
    const Json& n = *nodes_[id];
    const std::string type = n["type"].asString();
    i32 pc = -1;
    // 1) filhos primeiro (para saber os pcs), depois o corpo deste no
    if (type == "branch") {
      i32 thenPc = children(id, "then", reached), elsePc = children(id, "else", reached);
      if (err_->empty()) {
        i32 expr = condExpr(n["cond"].asString());
        if (err_->empty()) {
          pc = static_cast<i32>(p_->code.size());
          p_->code.push_back({Op::JumpIfNot, expr, pc + 3}); // se falso, pula o "then"
          p_->code.push_back({Op::Call, thenPc});
          p_->code.push_back({Op::Ret});
          p_->code.push_back({Op::Call, elsePc});
          p_->code.push_back({Op::Ret});
        }
      }
    } else {
      i32 next = children(id, "", reached);
      if (err_->empty()) {
        pc = static_cast<i32>(p_->code.size());
        if (type == "set" || type == "add") {
          i32 v; if (lookupVar(n["var"].asString(), v)) p_->code.push_back({type == "set" ? Op::SetVar : Op::AddVar, v, 0, n["value"].isBool() ? (n["value"].asBool() ? 1.0 : 0.0) : n["value"].asNumber()});
        } else if (type == "emit") p_->code.push_back({Op::Emit, str(n["event"].asString())});
        else if (type == "action") p_->code.push_back({Op::Action, str(n["name"].asString()), str(n["arg"].asString())});
        else if (type == "savepoint" || type == "loadpoint") {
          const std::string slot = n["slot"].asString().empty() ? "slot1" : n["slot"].asString();
          for (char c : slot) if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')) { fail(code::kBadSave, "nome de slot invalido em " + id); break; }
          p_->code.push_back({type == "savepoint" ? Op::SavePoint : Op::LoadPoint, str(slot)});
        }
        else if (type == "delay") p_->code.push_back({Op::Delay, 0, 0, n["seconds"].asNumber()});
        else fail(code::kBadGraph, "tipo de no desconhecido: '" + type + "' (" + id + ")");
        p_->code.push_back({Op::Call, next});
        p_->code.push_back({Op::Ret});
      }
    }
    stack_.pop_back();
    pcOf_[id] = pc;
    return pc;
  }

  // Compila a arvore de condicoes em pos-fixa.
  i32 condExpr(const std::string& id) {
    std::vector<ExprInstr> e;
    emitCond(id, e, 0);
    if (!err_->empty()) return -1;
    p_->exprs.push_back(std::move(e));
    return static_cast<i32>(p_->exprs.size() - 1);
  }
  void emitCond(const std::string& id, std::vector<ExprInstr>& e, int depth) {
    if (!err_->empty()) return;
    auto it = nodes_.find(id);
    if (it == nodes_.end()) { fail(code::kUnknownNode, "condicao inexistente: '" + id + "'"); return; }
    if (depth > 64) { fail(code::kFlowCycle, "condicoes aninhadas demais / ciclo em '" + id + "'"); return; }
    const Json& n = *it->second;
    const std::string t = n["type"].asString();
    if (t == "compare") {
      i32 v; Cmp c;
      if (!lookupVar(n["var"].asString(), v)) return;
      if (!parseCmp(n["op"].asString(), c)) { fail(code::kBadGraph, "operador invalido em " + id); return; }
      e.push_back({ExprOp::PushVar, v}); e.push_back({ExprOp::PushConst, 0, n["value"].isBool() ? (n["value"].asBool() ? 1.0 : 0.0) : n["value"].asNumber()});
      e.push_back({ExprOp::Cmp, static_cast<i32>(c)});
    } else if (t == "and" || t == "or") {
      const auto& in = n["inputs"].items();
      if (in.empty()) { fail(code::kBadGraph, "'" + id + "' sem entradas"); return; }
      for (size_t i = 0; i < in.size(); ++i) { emitCond(in[i].asString(), e, depth + 1); if (i) e.push_back({t == "and" ? ExprOp::And : ExprOp::Or}); }
    } else if (t == "not") {
      emitCond(n["input"].asString(), e, depth + 1); e.push_back({ExprOp::Not});
    } else fail(code::kBadGraph, "'" + id + "' nao e um no de condicao");
  }

  const Json* g_ = nullptr; Program* p_ = nullptr; std::string* err_ = nullptr;
  std::unordered_map<std::string, i32> varIdx_;
  std::unordered_map<std::string, const Json*> nodes_;
  std::unordered_map<std::string, std::vector<std::string>> out_;
  std::unordered_map<std::string, i32> pcOf_;
  std::vector<std::string> stack_;
};

// ---- Runtime ----------------------------------------------------------------------------------
class Runtime {
public:
  using ActionFn = std::function<void(const std::string& name, const std::string& arg)>;
  using EmitFn = std::function<void(const std::string& event)>;
  using SaveFn = std::function<void(const std::string& slot)>;   // SavePoint: quem hospeda grava o slot (o runtime so guarda nomes validos)

  explicit Runtime(const Program& p) : p_(p), vars_(p.varInit), armed_(p.entries.size(), true) {}

  void setActionHandler(ActionFn f) { action_ = std::move(f); }
  void setEmitHandler(EmitFn f) { emit_ = std::move(f); }
  void setSaveHandler(SaveFn f) { saveFn_ = std::move(f); }
  // LoadPoint nao troca o estado no meio da execucao (seguranca): marca o slot e o hospedeiro carrega depois do update.
  std::string takePendingLoad() { std::string s; s.swap(pendingLoad_); return s; }
  const std::vector<std::string>& varNames() const { return p_.varNames; }
  double varAt(size_t i) const { return vars_[i]; }

  void newSession() { for (size_t i = 0; i < p_.entries.size(); ++i) if (p_.entries[i].mode == Mode::Session) fired_.erase(p_.entries[i].id); }
  void beginTick() { busy_.clear(); }

  double var(const std::string& n) const { for (size_t i = 0; i < p_.varNames.size(); ++i) if (p_.varNames[i] == n) return vars_[i]; return 0; }

  // Variavel observavel: re-avalia gatilhos por variavel (com histerese).
  void setVar(const std::string& n, double v) {
    for (size_t i = 0; i < p_.varNames.size(); ++i) if (p_.varNames[i] == n) { vars_[i] = v; observe(static_cast<i32>(i)); return; }
    debug().report(code::kUnknownVar, Severity::Warning, "setVar em variavel inexistente: " + n);
  }

  // Dispara um evento de jogo. Retorna quantos fluxos iniciaram.
  int fire(const std::string& event) {
    int started = 0;
    for (size_t i = 0; i < p_.entries.size(); ++i) {
      const Entry& e = p_.entries[i];
      if (e.onVar || e.event != event) continue;
      if (tryStart(i)) ++started;
    }
    return started;
  }

  // Avanca tempo: retoma fluxos em espera.
  void update(double dt) {
    for (size_t i = 0; i < threads_.size();) {
      Thread& t = threads_[i];
      if (t.wait > 0) { t.wait -= dt; if (t.wait > 0) { ++i; continue; } t.wait = 0; }
      run(t);
      if (t.done) threads_.erase(threads_.begin() + static_cast<std::ptrdiff_t>(i)); else ++i;
    }
  }
  size_t runningFlows() const { return threads_.size(); }

  // ---- save nativo (.json) ----
  Json save() const {
    Json j = Json::object();
    j.set("version", 1);
    Json vs = Json::object();
    for (size_t i = 0; i < vars_.size(); ++i) vs.set(p_.varNames[i], vars_[i]);
    j.set("vars", vs);
    Json f = Json::array();
    for (auto& id : fired_) f.push(id);
    j.set("fired", f);
    Json a = Json::array();
    for (size_t i = 0; i < armed_.size(); ++i) if (!armed_[i]) a.push(p_.entries[i].id);
    j.set("disarmed", a);
    Json ts = Json::array();
    for (auto& t : threads_) {
      Json o = Json::object(); o.set("pc", t.pc); o.set("wait", t.wait);
      Json st = Json::array(); for (i32 r : t.stack) st.push(r);
      o.set("stack", st); ts.push(o);
    }
    j.set("flows", ts);
    return j;
  }
  // Valida tudo antes de aplicar (fail-closed): save corrompido nao altera o estado.
  bool load(const Json& j) {
    if (!j.isObject() || j["version"].asNumber() != 1) return badSave("versao de save invalida");
    std::vector<double> nv = vars_;
    for (auto& [k, v] : j["vars"].members()) {
      bool ok = false;
      for (size_t i = 0; i < p_.varNames.size(); ++i) if (p_.varNames[i] == k && v.isNumber()) { nv[i] = v.asNumber(); ok = true; }
      if (!ok) return badSave("variavel desconhecida no save: " + k);
    }
    std::set<std::string> nf;
    for (auto& v : j["fired"].items()) nf.insert(v.asString());
    std::vector<bool> na(p_.entries.size(), true);
    for (auto& v : j["disarmed"].items())
      for (size_t i = 0; i < p_.entries.size(); ++i) if (p_.entries[i].id == v.asString()) na[i] = false;
    std::vector<Thread> nt;
    for (auto& o : j["flows"].items()) {
      Thread t; t.pc = static_cast<i32>(o["pc"].asNumber()); t.wait = o["wait"].asNumber();
      if (t.pc < 0 || t.pc >= static_cast<i32>(p_.code.size())) return badSave("pc fora do programa");
      for (auto& r : o["stack"].items()) {
        const i32 pc = static_cast<i32>(r.asNumber());
        if (pc < 0 || pc >= static_cast<i32>(p_.code.size())) return badSave("retorno fora do programa");
        t.stack.push_back(pc);
      }
      if (t.stack.size() > kMaxCall) return badSave("pilha grande demais");
      nt.push_back(std::move(t));
    }
    vars_ = nv; fired_ = nf; armed_ = na; threads_ = nt;
    return true;
  }

  // Escrita atomica: grava em .tmp e renomeia, entao um crash nunca deixa um save pela metade.
  bool saveToFile(const std::string& path) const {
    const std::string tmp = path + ".tmp";
    { std::ofstream f(tmp, std::ios::binary | std::ios::trunc); if (!f) return false; f << save().dump(); if (!f.good()) return false; }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    return !ec;
  }
  bool loadFromFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary); if (!f) return false;
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    auto j = Json::parse(s);
    return j && load(*j);
  }

private:
  static constexpr size_t kMaxCall = 64;
  struct Thread { i32 pc = 0; std::vector<i32> stack; double wait = 0; bool done = false; };

  bool badSave(const std::string& m) { debug().report(code::kBadSave, Severity::Error, m); return false; }

  bool tryStart(size_t ei) {
    const Entry& e = p_.entries[ei];
    if ((e.mode == Mode::OneShot || e.mode == Mode::Session) && fired_.count(e.id)) return false;
    if (e.group != 0 && busy_.count(e.group)) {
      debug().report(diag::kEventConflict, Severity::Warning, "gatilho '" + e.id + "' bloqueado: grupo " + std::to_string(e.group) + " ocupado neste tick");
      return false;
    }
    // consumo atomico ANTES de executar: reentrada nao dispara de novo
    if (e.mode != Mode::Reactivable) fired_.insert(e.id);
    if (e.group != 0) busy_.insert(e.group);
    if (e.pc >= 0) { Thread t; t.pc = e.pc; threads_.push_back(t); run(threads_.back()); if (threads_.back().done) threads_.pop_back(); }
    return true;
  }

  static bool cmp(Cmp c, double a, double b) {
    switch (c) { case Cmp::Eq: return a == b; case Cmp::Ne: return a != b; case Cmp::Lt: return a < b;
                 case Cmp::Le: return a <= b; case Cmp::Gt: return a > b; case Cmp::Ge: return a >= b; }
    return false;
  }

  void observe(i32 varIdx) {
    for (size_t i = 0; i < p_.entries.size(); ++i) {
      const Entry& e = p_.entries[i];
      if (!e.onVar || e.var != varIdx) continue;
      const double v = vars_[static_cast<size_t>(varIdx)];
      if (armed_[i]) {
        if (cmp(e.cmp, v, e.threshold)) { armed_[i] = false; if (!tryStart(i)) { /* bloqueado: rearma para tentar de novo */ armed_[i] = true; } }
      } else if (e.mode == Mode::Reactivable) {
        bool rearm = false;
        switch (e.cmp) {
        case Cmp::Lt: case Cmp::Le: rearm = v > e.threshold + e.hysteresis; break;
        case Cmp::Gt: case Cmp::Ge: rearm = v < e.threshold - e.hysteresis; break;
        case Cmp::Eq: rearm = v != e.threshold; break;
        case Cmp::Ne: rearm = v == e.threshold; break;
        }
        if (rearm) armed_[i] = true;
      }
    }
  }

  bool eval(i32 exprIdx) const {
    std::vector<double> st;
    for (const ExprInstr& x : p_.exprs[static_cast<size_t>(exprIdx)]) {
      switch (x.op) {
      case ExprOp::PushVar: st.push_back(vars_[static_cast<size_t>(x.a)]); break;
      case ExprOp::PushConst: st.push_back(x.v); break;
      case ExprOp::Cmp: { double b = st.back(); st.pop_back(); double a = st.back(); st.back() = cmp(static_cast<Cmp>(x.a), a, b) ? 1 : 0; break; }
      case ExprOp::And: { double b = st.back(); st.pop_back(); st.back() = (st.back() != 0 && b != 0) ? 1 : 0; break; }
      case ExprOp::Or: { double b = st.back(); st.pop_back(); st.back() = (st.back() != 0 || b != 0) ? 1 : 0; break; }
      case ExprOp::Not: st.back() = st.back() == 0 ? 1 : 0; break;
      }
    }
    return !st.empty() && st.back() != 0;
  }

  void run(Thread& t) {
    int budget = 100000; // limite de instrucoes por fatia: nenhum grafo trava o jogo
    while (!t.done && t.wait <= 0) {
      if (--budget < 0) { debug().report(diag::kWatchdog, Severity::Error, "fluxo narrativo excedeu o orcamento de instrucoes"); t.done = true; return; }
      if (t.pc < 0 || t.pc >= static_cast<i32>(p_.code.size())) { t.done = true; return; }
      const Instr& in = p_.code[static_cast<size_t>(t.pc)];
      switch (in.op) {
      case Op::SetVar: vars_[static_cast<size_t>(in.a)] = in.v; observe(in.a); ++t.pc; break;
      case Op::AddVar: vars_[static_cast<size_t>(in.a)] += in.v; observe(in.a); ++t.pc; break;
      case Op::Emit: if (emit_) emit_(p_.strings[static_cast<size_t>(in.a)]); ++t.pc; break;
      case Op::Action: if (action_) action_(p_.strings[static_cast<size_t>(in.a)], p_.strings[static_cast<size_t>(in.b)]); ++t.pc; break;
      case Op::JumpIfNot: t.pc = eval(in.a) ? t.pc + 1 : in.b; break;
      case Op::Delay: t.wait = in.v; ++t.pc; break;
      case Op::SavePoint: ++t.pc; if (saveFn_) saveFn_(p_.strings[static_cast<size_t>(in.a)]); break;
      case Op::LoadPoint: pendingLoad_ = p_.strings[static_cast<size_t>(in.a)]; t.done = true; return;
      case Op::Call:
        if (in.a < 0) { ++t.pc; break; } // sem filhos
        if (t.stack.size() >= kMaxCall) { debug().report(code::kCallDepth, Severity::Error, "pilha do fluxo narrativo estourou"); t.done = true; return; }
        t.stack.push_back(t.pc + 1); t.pc = in.a; break;
      case Op::Ret:
        if (t.stack.empty()) { t.done = true; return; }
        t.pc = t.stack.back(); t.stack.pop_back(); break;
      }
    }
  }

  const Program& p_;
  std::vector<double> vars_;
  std::vector<bool> armed_;
  std::set<std::string> fired_;
  std::set<i32> busy_;
  std::vector<Thread> threads_;
  ActionFn action_;
  EmitFn emit_;
  SaveFn saveFn_;
  std::string pendingLoad_;
};

} // namespace tool::narr
