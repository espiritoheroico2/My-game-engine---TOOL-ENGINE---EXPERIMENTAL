// script_host.hpp - Runtime dos scripts ToolObject com QuickJS-ng (MIT, https://github.com/quickjs-ng/quickjs, v0.17.0).
//
// Isolamento e limites:
//   - cada objeto com script ganha o SEU contexto JS (globais proprios; um script nao enxerga o outro);
//   - sem modulos, sem arquivos, sem rede: so as funcoes do host listadas abaixo existem;
//   - limites: memoria 16 MB (todos os scripts juntos), pilha 256 KB, tempo por chamada 5 ms (interrupt handler);
//   - um erro desliga o script daquele objeto (mensagem no Console e no Inspector); o resto continua;
//   - o script NUNCA mexe no mundo diretamente: ele PEDE (send, setActive, instantiate, destroy, addComponent,
//     removeComponent, setPosition). O editor aplica a fila depois que o script devolveu o controle.
//
// Forma do script (script classico; "export" no inicio da linha e aceito e ignorado):
//   export function start(obj) {}                 // uma vez, quando o objeto fica ativo no Play
//   export function update(obj, dt) {}            // todo quadro
//   export function OnEventTrigger(obj, name) {}  // cada evento enviado ao objeto (send / Narrative)
//
// Sem TOOL_WITH_QUICKJS o host existe, mas cada script aparece como "sem runtime nesta build".
#pragma once
#include "tool/core/log.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#ifdef TOOL_WITH_QUICKJS
#include "quickjs.h"
#endif

namespace editor {

class ScriptHost {
public:
  enum class CmdKind { Send, SetActive, Instantiate, Destroy, AddComp, RemoveComp, SetPos };
  struct Cmd {
    CmdKind kind; std::string a, b; bool flag = false; float delay = 0.f; float v[3] = {0.f, 0.f, 0.f};
  };
  struct Entry {                       // um script a carregar (o editor monta a lista)
    uint32_t entity = 0, gen = 0; std::string name, source; bool enabled = true;
  };
  struct Inst {
    uint32_t entity = 0, gen = 0; std::string name; bool started = false, failed = false; std::string error;
    ScriptHost* host = nullptr;
#ifdef TOOL_WITH_QUICKJS
    JSContext* ctx = nullptr;
    JSValue obj = JS_UNDEFINED, timeObj = JS_UNDEFINED, fStart = JS_UNDEFINED, fUpdate = JS_UNDEFINED, fInbox = JS_UNDEFINED;
#endif
  };

  static constexpr bool compiledIn() {
#ifdef TOOL_WITH_QUICKJS
    return true;
#else
    return false;
#endif
  }
  ScriptHost() = default;
  ScriptHost(const ScriptHost&) = delete;
  ScriptHost& operator=(const ScriptHost&) = delete;
  ~ScriptHost() { close(); }

  void open(const std::vector<Entry>& list);   // fecha o anterior e carrega todos (Play)
  bool add(const Entry& en);                   // carrega UM script sem reiniciar os outros (objeto instanciado no Play)
  void close();
  bool isOpen() const { return open_; }
  std::vector<std::unique_ptr<Inst>>& instances() { return insts_; }
  std::string statusOf(uint32_t entity, uint32_t gen) const;

  // Chamadas (false = nao rodou ou falhou; a falha fica em Inst::error)
  bool callStart(Inst& in);
  bool callUpdate(Inst& in, float dt);
  bool callEvent(Inst& in, const std::string& ev);
  void pushTransform(Inst& in, const float pos[3], const float rot[3], const float scale[3]);
  bool pullTransform(Inst& in, float pos[3], float rot[3], float scale[3]);

  std::vector<Cmd> cmds;   // fila preenchida pelos scripts; o editor consome depois do quadro
  double now_ = 0.0;       // tempo de jogo, exposto como time.now
  void setTime(double now) { now_ = now; }

private:
  bool open_ = false;
  std::vector<std::unique_ptr<Inst>> insts_;
#ifdef TOOL_WITH_QUICKJS
  JSRuntime* rt_ = nullptr;
  std::chrono::steady_clock::time_point deadline_{};
  static int interrupt(JSRuntime*, void* opaque);
  static Inst* instOf(JSContext* ctx) { return static_cast<Inst*>(JS_GetContextOpaque(ctx)); }
  static std::string strOf(JSContext* ctx, JSValueConst v);
  static std::string nameOf(JSContext* ctx, JSValueConst v);   // aceita string ou objeto com .name
  static JSValue hostLog(JSContext*, JSValueConst, int, JSValueConst*);
  static JSValue hostSend(JSContext*, JSValueConst, int, JSValueConst*);
  static JSValue hostSetActive(JSContext*, JSValueConst, int, JSValueConst*);
  static JSValue hostInstantiate(JSContext*, JSValueConst, int, JSValueConst*);
  static JSValue hostDestroy(JSContext*, JSValueConst, int, JSValueConst*);
  static JSValue hostAddComp(JSContext*, JSValueConst, int, JSValueConst*);
  static JSValue hostRemoveComp(JSContext*, JSValueConst, int, JSValueConst*);
  static JSValue hostSetPos(JSContext*, JSValueConst, int, JSValueConst*);
  void report(Inst& in);
  bool call(Inst& in, JSValue fn, int argc, JSValue* argv);
  void freeInst(Inst& in);
#endif
};

#ifdef TOOL_WITH_QUICKJS
// ------------------------------------------------------------------------------------------------ implementacao
inline int ScriptHost::interrupt(JSRuntime*, void* opaque) {
  return std::chrono::steady_clock::now() > static_cast<ScriptHost*>(opaque)->deadline_ ? 1 : 0;   // corta a chamada que estourou o tempo
}
inline std::string ScriptHost::strOf(JSContext* ctx, JSValueConst v) {
  const char* s = JS_ToCString(ctx, v);
  if (!s) return {};
  std::string out(s);
  JS_FreeCString(ctx, s);
  return out;
}
inline std::string ScriptHost::nameOf(JSContext* ctx, JSValueConst v) {
  if (JS_IsString(v)) return strOf(ctx, v);
  JSValue n = JS_GetPropertyStr(ctx, v, "name");
  std::string out = strOf(ctx, n);
  JS_FreeValue(ctx, n);
  return out;
}
inline JSValue ScriptHost::hostLog(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  Inst* in = instOf(ctx);
  if (argc > 0) tool::logInfo("[" + (in ? in->name : std::string("script")) + "] " + strOf(ctx, argv[0]));
  return JS_UNDEFINED;
}
inline JSValue ScriptHost::hostSend(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  Inst* in = instOf(ctx);
  if (!in || argc < 2) return JS_UNDEFINED;
  in->host->cmds.push_back({CmdKind::Send, nameOf(ctx, argv[0]), strOf(ctx, argv[1]), false, 0.f});
  return JS_UNDEFINED;
}
inline JSValue ScriptHost::hostSetActive(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  Inst* in = instOf(ctx);
  if (!in || argc < 2) return JS_UNDEFINED;
  in->host->cmds.push_back({CmdKind::SetActive, nameOf(ctx, argv[0]), {}, JS_ToBool(ctx, argv[1]) == 1, 0.f});
  return JS_UNDEFINED;
}
inline JSValue ScriptHost::hostInstantiate(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  Inst* in = instOf(ctx);
  if (!in || argc < 1) return JS_UNDEFINED;
  in->host->cmds.push_back({CmdKind::Instantiate, nameOf(ctx, argv[0]), {}, false, 0.f});
  return JS_UNDEFINED;
}
inline JSValue ScriptHost::hostDestroy(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  Inst* in = instOf(ctx);
  if (!in || argc < 1) return JS_UNDEFINED;
  double sec = 0.0;
  if (argc > 1) JS_ToFloat64(ctx, &sec, argv[1]);
  in->host->cmds.push_back({CmdKind::Destroy, nameOf(ctx, argv[0]), {}, false, static_cast<float>(sec < 0 ? 0.0 : sec)});
  return JS_UNDEFINED;
}
inline JSValue ScriptHost::hostAddComp(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  Inst* in = instOf(ctx);
  if (!in || argc < 2) return JS_UNDEFINED;
  in->host->cmds.push_back({CmdKind::AddComp, nameOf(ctx, argv[0]), strOf(ctx, argv[1]), false, 0.f});
  return JS_UNDEFINED;
}
inline JSValue ScriptHost::hostRemoveComp(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  Inst* in = instOf(ctx);
  if (!in || argc < 2) return JS_UNDEFINED;
  in->host->cmds.push_back({CmdKind::RemoveComp, nameOf(ctx, argv[0]), strOf(ctx, argv[1]), false, 0.f});
  return JS_UNDEFINED;
}
inline JSValue ScriptHost::hostSetPos(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  Inst* in = instOf(ctx);
  if (!in || argc < 4) return JS_UNDEFINED;
  Cmd c{CmdKind::SetPos, nameOf(ctx, argv[0]), {}, false, 0.f};
  for (int i = 0; i < 3; ++i) {
    double d = 0.0;
    JS_ToFloat64(ctx, &d, argv[i + 1]);
    c.v[i] = static_cast<float>(d);
  }
  in->host->cmds.push_back(c);
  return JS_UNDEFINED;
}

inline void ScriptHost::open(const std::vector<Entry>& list) {
  close();
  for (const Entry& en : list) add(en);
  open_ = true;
}

inline bool ScriptHost::add(const Entry& en) {
  if (!en.enabled) return false;
  if (!rt_) {
    rt_ = JS_NewRuntime();
    if (!rt_) { tool::logError("QuickJS: could not create the runtime."); return false; }
    JS_SetMemoryLimit(rt_, size_t{16} << 20);
    JS_SetMaxStackSize(rt_, size_t{256} << 10);
    JS_SetInterruptHandler(rt_, &ScriptHost::interrupt, this);
  }
  auto up = std::make_unique<Inst>();
  up->entity = en.entity; up->gen = en.gen; up->name = en.name; up->host = this;
  insts_.push_back(std::move(up));
  Inst& I = *insts_.back();   // endereco estavel (unique_ptr): erros ficam registrados nele
  open_ = true;
  I.ctx = JS_NewContext(rt_);
  if (!I.ctx) { I.failed = true; I.error = "could not create context"; return true; }
  JSContext* ctx = I.ctx;
  JS_SetContextOpaque(ctx, &I);

  JSValue g = JS_GetGlobalObject(ctx);
  JS_SetPropertyStr(ctx, g, "send", JS_NewCFunction(ctx, &ScriptHost::hostSend, "send", 2));
  JS_SetPropertyStr(ctx, g, "setActive", JS_NewCFunction(ctx, &ScriptHost::hostSetActive, "setActive", 2));
  JS_SetPropertyStr(ctx, g, "instantiate", JS_NewCFunction(ctx, &ScriptHost::hostInstantiate, "instantiate", 1));
  JS_SetPropertyStr(ctx, g, "destroy", JS_NewCFunction(ctx, &ScriptHost::hostDestroy, "destroy", 2));
  JS_SetPropertyStr(ctx, g, "addComponent", JS_NewCFunction(ctx, &ScriptHost::hostAddComp, "addComponent", 2));
  JS_SetPropertyStr(ctx, g, "removeComponent", JS_NewCFunction(ctx, &ScriptHost::hostRemoveComp, "removeComponent", 2));
  JS_SetPropertyStr(ctx, g, "setPosition", JS_NewCFunction(ctx, &ScriptHost::hostSetPos, "setPosition", 4));
  JSValue dbg = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, dbg, "console", JS_NewCFunction(ctx, &ScriptHost::hostLog, "console", 1));
  JS_SetPropertyStr(ctx, g, "debug", dbg);
  I.timeObj = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, g, "time", JS_DupValue(ctx, I.timeObj));
  I.obj = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, I.obj, "name", JS_NewStringLen(ctx, I.name.data(), I.name.size()));
  JSValue tr = JS_NewObject(ctx);
  for (const char* k : {"pos", "rot", "scale"}) JS_SetPropertyStr(ctx, tr, k, JS_NewArray(ctx));
  JS_SetPropertyStr(ctx, I.obj, "transform", tr);
  JS_FreeValue(ctx, g);

  { // "vec3(x,y,z)" -> [x,y,z], usado com setPosition
    static const char kPrelude[] = "function vec3(x, y, z) { return [x, y, z]; }";
    deadline_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
    JS_FreeValue(ctx, JS_Eval(ctx, kPrelude, sizeof kPrelude - 1, "prelude", JS_EVAL_TYPE_GLOBAL));
  }

  // Codigo: "export " no inicio da linha e removido (script classico); "import" e recusado (biblioteca ainda nao existe).
  std::string src; src.reserve(en.source.size()); bool bad = false; size_t p = 0;
  while (p <= en.source.size()) {
    size_t e = en.source.find('\n', p);
    if (e == std::string::npos) e = en.source.size();
    std::string line = en.source.substr(p, e - p);
    const size_t f = line.find_first_not_of(" \t");
    if (f != std::string::npos && line.compare(f, 7, "export ") == 0) line.erase(f, 7);
    if (f != std::string::npos && line.compare(f, 7, "import ") == 0) bad = true;
    src += line; src += '\n';
    p = e + 1;
  }
  if (bad) { I.failed = true; I.error = "import is not available yet (script library planned)"; tool::logError("[script " + I.name + "] " + I.error); return true; }

  deadline_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
  JSValue r = JS_Eval(ctx, src.c_str(), src.size(), (I.name + ".js").c_str(), JS_EVAL_TYPE_GLOBAL);
  if (JS_IsException(r)) { report(I); JS_FreeValue(ctx, r); return true; }
  JS_FreeValue(ctx, r);
  JSValue gl = JS_GetGlobalObject(ctx);
  I.fStart = JS_GetPropertyStr(ctx, gl, "start");
  I.fUpdate = JS_GetPropertyStr(ctx, gl, "update");
  I.fInbox = JS_GetPropertyStr(ctx, gl, "OnEventTrigger");
  JS_FreeValue(ctx, gl);
  if (!JS_IsFunction(ctx, I.fStart)) { JS_FreeValue(ctx, I.fStart); I.fStart = JS_UNDEFINED; }
  if (!JS_IsFunction(ctx, I.fUpdate)) { JS_FreeValue(ctx, I.fUpdate); I.fUpdate = JS_UNDEFINED; }
  if (!JS_IsFunction(ctx, I.fInbox)) { JS_FreeValue(ctx, I.fInbox); I.fInbox = JS_UNDEFINED; }
  return true;
}

inline void ScriptHost::freeInst(Inst& in) {
  if (!in.ctx) return;
  JSContext* ctx = in.ctx;
  for (JSValue* v : {&in.obj, &in.timeObj, &in.fStart, &in.fUpdate, &in.fInbox}) { JS_FreeValue(ctx, *v); *v = JS_UNDEFINED; }
  JS_FreeContext(ctx);
  in.ctx = nullptr;
}

inline void ScriptHost::close() {
  for (auto& i : insts_) freeInst(*i);
  insts_.clear();
  cmds.clear();
  if (rt_) { JS_FreeRuntime(rt_); rt_ = nullptr; }
  open_ = false;
}

inline void ScriptHost::report(Inst& in) {
  JSValue ex = JS_GetException(in.ctx);
  std::string msg = strOf(in.ctx, ex);
  JSValue stack = JS_GetPropertyStr(in.ctx, ex, "stack");
  if (!JS_IsUndefined(stack)) { std::string st = strOf(in.ctx, stack); if (!st.empty()) msg = st; }
  JS_FreeValue(in.ctx, stack);
  JS_FreeValue(in.ctx, ex);
  in.failed = true;
  in.error = msg.empty() ? "unknown error" : msg;
  tool::logError("[script " + in.name + "] " + in.error);
}

inline bool ScriptHost::call(Inst& in, JSValue fn, int argc, JSValue* argv) {
  if (in.failed || !in.ctx || JS_IsUndefined(fn)) return false;
  deadline_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(5);   // orcamento desta chamada
  JSValue r = JS_Call(in.ctx, fn, JS_UNDEFINED, argc, argv);
  if (JS_IsException(r)) { report(in); JS_FreeValue(in.ctx, r); return false; }
  JS_FreeValue(in.ctx, r);
  return true;
}

inline bool ScriptHost::callStart(Inst& in) {
  if (in.started || in.failed) return false;
  in.started = true;
  JS_SetPropertyStr(in.ctx, in.timeObj, "now", JS_NewFloat64(in.ctx, now_));
  JSValue a[1] = {in.obj};
  return call(in, in.fStart, 1, a);
}
inline bool ScriptHost::callUpdate(Inst& in, float dt) {
  if (in.failed || !in.ctx) return false;
  JS_SetPropertyStr(in.ctx, in.timeObj, "now", JS_NewFloat64(in.ctx, now_));
  JS_SetPropertyStr(in.ctx, in.timeObj, "dt", JS_NewFloat64(in.ctx, static_cast<double>(dt)));
  JSValue a[2] = {in.obj, JS_NewFloat64(in.ctx, static_cast<double>(dt))};
  const bool ok = call(in, in.fUpdate, 2, a);
  JS_FreeValue(in.ctx, a[1]);
  return ok;
}
inline bool ScriptHost::callEvent(Inst& in, const std::string& ev) {
  if (in.failed || !in.ctx) return false;
  JSValue s = JS_NewStringLen(in.ctx, ev.data(), ev.size());
  JSValue a[2] = {in.obj, s};
  const bool ok = call(in, in.fInbox, 2, a);
  JS_FreeValue(in.ctx, s);
  return ok;
}

inline void ScriptHost::pushTransform(Inst& in, const float pos[3], const float rot[3], const float scale[3]) {
  if (!in.ctx) return;
  JSValue tr = JS_GetPropertyStr(in.ctx, in.obj, "transform");
  const float* vals[3] = {pos, rot, scale};
  const char* keys[3] = {"pos", "rot", "scale"};
  for (int k = 0; k < 3; ++k) {
    JSValue arr = JS_GetPropertyStr(in.ctx, tr, keys[k]);
    for (uint32_t i = 0; i < 3; ++i) JS_SetPropertyUint32(in.ctx, arr, i, JS_NewFloat64(in.ctx, static_cast<double>(vals[k][i])));
    JS_FreeValue(in.ctx, arr);
  }
  JS_FreeValue(in.ctx, tr);
}
inline bool ScriptHost::pullTransform(Inst& in, float pos[3], float rot[3], float scale[3]) {
  if (!in.ctx) return false;
  JSValue tr = JS_GetPropertyStr(in.ctx, in.obj, "transform");
  float* vals[3] = {pos, rot, scale};
  const char* keys[3] = {"pos", "rot", "scale"};
  bool ok = true;
  for (int k = 0; k < 3; ++k) {
    JSValue arr = JS_GetPropertyStr(in.ctx, tr, keys[k]);
    for (uint32_t i = 0; i < 3; ++i) {
      JSValue v = JS_GetPropertyUint32(in.ctx, arr, i);
      double d = 0.0;
      if (JS_ToFloat64(in.ctx, &d, v) == 0 && d == d && d > -1e9 && d < 1e9) vals[k][i] = static_cast<float>(d);
      else ok = false;   // NaN ou fora da faixa: o Transform do objeto nao muda
      JS_FreeValue(in.ctx, v);
    }
    JS_FreeValue(in.ctx, arr);
  }
  JS_FreeValue(in.ctx, tr);
  return ok;
}
#else
inline void ScriptHost::open(const std::vector<Entry>& list) {
  close();
  for (const Entry& en : list) add(en);
  open_ = true;
}
inline bool ScriptHost::add(const Entry& en) {
  if (!en.enabled) return false;
  auto up = std::make_unique<Inst>();
  up->entity = en.entity; up->gen = en.gen; up->name = en.name; up->host = this;
  up->failed = true; up->error = "this build has no script runtime (QuickJS is off)";
  insts_.push_back(std::move(up));
  open_ = true;
  return true;
}
inline void ScriptHost::close() { insts_.clear(); cmds.clear(); open_ = false; }
inline bool ScriptHost::callStart(Inst&) { return false; }
inline bool ScriptHost::callUpdate(Inst&, float) { return false; }
inline bool ScriptHost::callEvent(Inst&, const std::string&) { return false; }
inline void ScriptHost::pushTransform(Inst&, const float[3], const float[3], const float[3]) {}
inline bool ScriptHost::pullTransform(Inst&, float[3], float[3], float[3]) { return false; }
#endif

inline std::string ScriptHost::statusOf(uint32_t entity, uint32_t gen) const {
  for (const auto& i : insts_) {
    if (i->entity != entity || i->gen != gen) continue;
    if (i->failed) return i->error.empty() ? "stopped" : "error: " + i->error;
    return i->started ? "running" : "waiting for Play";
  }
  return "not running";
}

} // namespace editor
