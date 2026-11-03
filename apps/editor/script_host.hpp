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
#include "tool/debug/debug.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#ifdef TOOL_WITH_QUICKJS
#include "quickjs.h"
#endif

namespace editor {

class ScriptHost {
public:
  enum class CmdKind { Send, SetActive, Instantiate, Destroy, AddComp, RemoveComp, SetPos, SetRot, SetColor };
  struct Cmd {
    CmdKind kind; std::string a, b; bool flag = false; float delay = 0.f; float v[3] = {0.f, 0.f, 0.f};
  };
  // Estado de entrada do quadro (preenchido pelo editor antes de cada update). Teclas por nome: "W", "Space", "Mouse0"...
  struct InputState {
    std::vector<std::string> held, pressed; float mouseDx = 0.f, mouseDy = 0.f;
  };
  struct Entry {                       // um script a carregar (o editor monta a lista)
    uint32_t entity = 0, gen = 0; std::string name, source; bool enabled = true;
  };
  struct Inst {
    uint32_t entity = 0, gen = 0; std::string name; bool started = false, failed = false; std::string error;
    std::string phase;          // funcao em execucao: "compile", "start", "update" ou "OnEventTrigger"
    int errLine = 0, errCol = 0; // linha/coluna do erro no script (0 = desconhecida)
    ScriptHost* host = nullptr;
#ifdef TOOL_WITH_QUICKJS
    JSContext* ctx = nullptr;
    JSValue obj = JS_UNDEFINED, timeObj = JS_UNDEFINED, fStart = JS_UNDEFINED, fUpdate = JS_UNDEFINED, fInbox = JS_UNDEFINED;
#endif
  };

  // Verificacao de sintaxe SEM executar (QuickJS, JS_EVAL_FLAG_COMPILE_ONLY). Devolve true se compila.
  // Com erro: line/col e msg preenchidos. Sem QuickJS na build: sempre true (quem checa e o analisador de texto).
  static bool compileCheck(const std::string& source, const std::string& name, int& line, int& col, std::string& msg);
  // Remove "export " e detecta "import" (mesmo tratamento do runtime). Mantem as linhas.
  static std::string stripModuleKeywords(const std::string& source, int& importLine);

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
  InputState input;        // entrada do quadro (input.down / pressed / mouseDelta)
  // Le a posicao de um objeto pelo nome (position("Nome")). Preenchido pelo editor; sem ele, position() devolve undefined.
  std::function<bool(const std::string&, float out[3])> lookup;
  uint32_t spawnSeq = 0;   // numera as copias criadas por instantiate: "Bullet#1", "Bullet#2"...
  // Confere se um objeto existe pelo nome (Preenchido pelo editor). Sem ele, nao ha aviso de nome errado.
  std::function<bool(const std::string&)> exists;
  // Objeto alvo que nao existe: aviso (uma vez por Play). Retorna true se existe ou se a checagem nao esta disponivel.
  bool checkTarget(Inst* in, const std::string& name, const char* fn);
  // Avisos do Play: cada chave (objeto + codigo + texto) aparece UMA vez por Play, para nao lotar o Console.
  void warn(Inst* in, const char* code, const std::string& msg);
  void error(Inst* in, const char* code, const std::string& msg, int line = 0);

private:
  bool open_ = false;
  std::vector<std::unique_ptr<Inst>> insts_;
  std::set<std::string> warned_;
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
  static JSValue hostSetRot(JSContext*, JSValueConst, int, JSValueConst*);
  static JSValue hostSetColor(JSContext*, JSValueConst, int, JSValueConst*);
  static JSValue hostPosition(JSContext*, JSValueConst, int, JSValueConst*);
  static JSValue hostInputDown(JSContext*, JSValueConst, int, JSValueConst*);
  static JSValue hostInputPressed(JSContext*, JSValueConst, int, JSValueConst*);
  static JSValue hostMouseDelta(JSContext*, JSValueConst, int, JSValueConst*);
  static JSValue hostConsoleWarn(JSContext*, JSValueConst, int, JSValueConst*);
  static JSValue hostConsoleError(JSContext*, JSValueConst, int, JSValueConst*);
  static bool vec3Args(JSContext* ctx, int argc, JSValueConst* argv, int from, float out[3]);
  // Argumento numerico / nome obrigatorios: falha lanca TypeError (o script pode usar try/catch).
  static bool numArg(JSContext* ctx, int argc, JSValueConst* argv, int idx, const char* fn, const char* what, double& out);
  static bool nameArg(JSContext* ctx, int argc, JSValueConst* argv, const char* fn, std::string& out);
  void report(Inst& in);
  bool call(Inst& in, JSValue fn, int argc, JSValue* argv);
  void freeInst(Inst& in);
#endif
};


// ------------------------------------------------------------------------------------------------ comum (com e sem QuickJS)
inline std::string ScriptHost::stripModuleKeywords(const std::string& source, int& importLine) {
  importLine = 0;
  std::string out; out.reserve(source.size());
  size_t p = 0; int lineNo = 0;
  while (p <= source.size()) {
    ++lineNo;
    size_t e = source.find('\n', p);
    if (e == std::string::npos) e = source.size();
    std::string line = source.substr(p, e - p);
    const size_t f = line.find_first_not_of(" \t");
    if (f != std::string::npos && line.compare(f, 7, "export ") == 0) line.erase(f, 7);
    if (f != std::string::npos && line.compare(f, 7, "import ") == 0 && importLine == 0) importLine = lineNo;
    out += line; out += '\n';
    p = e + 1;
  }
  return out;
}

// Procura "<arquivo>:LINHA:COLUNA" no stack do QuickJS. Sem achar, deixa 0.
inline void parseScriptLocation(const std::string& stack, const std::string& file, int& line, int& col) {
  line = 0; col = 0;
  const std::string tag = file + ":";
  const size_t at = stack.find(tag);
  if (at == std::string::npos) return;
  size_t i = at + tag.size();
  int l = 0, c = 0; bool any = false;
  while (i < stack.size() && stack[i] >= '0' && stack[i] <= '9') { l = l * 10 + (stack[i] - '0'); ++i; any = true; }
  if (!any) return;
  if (i < stack.size() && stack[i] == ':') {
    ++i;
    while (i < stack.size() && stack[i] >= '0' && stack[i] <= '9') { c = c * 10 + (stack[i] - '0'); ++i; }
  }
  line = l; col = c;
}

inline void ScriptHost::warn(Inst* in, const char* code, const std::string& msg) {
  const std::string who = in ? in->name : std::string("script");
  if (!warned_.insert(who + '|' + code + '|' + msg).second) return;   // uma vez por Play
  tool::debug().report(code, tool::Severity::Warning, "[" + who + "] " + msg);
}

inline void ScriptHost::error(Inst* in, const char* code, const std::string& msg, int line) {
  const std::string who = in ? in->name : std::string("script");
  std::string text = "[" + who + "] " + msg;
  if (line > 0) text += " (linha " + std::to_string(line) + ")";
  tool::debug().report(code, tool::Severity::Error, text);
}

// Confere se o alvo de um comando existe. Nome errado nao quebra o jogo, mas sem o aviso o erro some em silencio.
inline bool ScriptHost::checkTarget(Inst* in, const std::string& name, const char* fn) {
  if (!exists || exists(name)) return true;
  warn(in, "TOOL-S005", std::string(fn) + ": there is no object named '" + name + "' in the scene (check the name in the Hierarchy)");
  return false;
}

// Checagem de sintaxe sem executar. Com QuickJS: compila de verdade. Sem QuickJS: nao verifica (o analisador de texto ainda roda).
inline bool ScriptHost::compileCheck(const std::string& source, const std::string& name, int& line, int& col, std::string& msg) {
  line = 0; col = 0; msg.clear();
  int importLine = 0;
  const std::string src = stripModuleKeywords(source, importLine);
  if (importLine > 0) { line = importLine; msg = "import is not available yet (script library planned)"; return false; }
#ifdef TOOL_WITH_QUICKJS
  JSRuntime* rt = JS_NewRuntime();
  if (!rt) { msg = "could not create the runtime"; return false; }
  JSContext* ctx = JS_NewContext(rt);
  JSValue r = JS_Eval(ctx, src.c_str(), src.size(), (name + ".js").c_str(), JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY);
  const bool ok = !JS_IsException(r);
  if (ok) JS_FreeValue(ctx, r);
  else {
    JSValue ex = JS_GetException(ctx);
    msg = strOf(ctx, ex);
    JSValue st = JS_GetPropertyStr(ctx, ex, "stack");
    const std::string stack = JS_IsUndefined(st) ? std::string() : strOf(ctx, st);
    JS_FreeValue(ctx, st);
    JS_FreeValue(ctx, ex);
    parseScriptLocation(stack, name + ".js", line, col);
    if (msg.empty()) msg = "syntax error";
  }
  JS_FreeContext(ctx);
  JS_FreeRuntime(rt);
  return ok;
#else
  (void)name;
  return true;
#endif
}

#ifdef TOOL_WITH_QUICKJS
// ------------------------------------------------------------------------------------------------ implementacao

// Argumento numerico obrigatorio. Falta ou nao e numero finito -> TypeError (o script pode usar try/catch).
inline bool ScriptHost::numArg(JSContext* ctx, int argc, JSValueConst* argv, int idx, const char* fn, const char* what, double& out) {
  if (idx >= argc) { JS_ThrowTypeError(ctx, "%s: missing argument '%s'", fn, what); return false; }
  if (JS_ToFloat64(ctx, &out, argv[idx]) != 0 || !std::isfinite(out)) {
    JS_ThrowTypeError(ctx, "%s: '%s' must be a finite number", fn, what);
    return false;
  }
  return true;
}
inline bool ScriptHost::nameArg(JSContext* ctx, int argc, JSValueConst* argv, const char* fn, std::string& out) {
  if (argc < 1) { JS_ThrowTypeError(ctx, "%s: missing argument 'name'", fn); return false; }
  out = nameOf(ctx, argv[0]);
  return true;
}

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
  if (!in) return JS_UNDEFINED;
  if (argc < 2) return JS_ThrowTypeError(ctx, "send(target, event) needs 2 arguments");
  std::string tgt; if (!nameArg(ctx, argc, argv, "send", tgt)) return JS_EXCEPTION;
  if (tgt != "*" && tgt.rfind("tag:", 0) != 0) in->host->checkTarget(in, tgt, "send");
  in->host->cmds.push_back({CmdKind::Send, tgt, strOf(ctx, argv[1]), false, 0.f});
  return JS_UNDEFINED;
}
inline JSValue ScriptHost::hostSetActive(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  Inst* in = instOf(ctx);
  if (!in) return JS_UNDEFINED;
  if (argc < 2) return JS_ThrowTypeError(ctx, "setActive(target, on) needs 2 arguments");
  std::string tgt; if (!nameArg(ctx, argc, argv, "setActive", tgt)) return JS_EXCEPTION;
  in->host->checkTarget(in, tgt, "setActive");
  in->host->cmds.push_back({CmdKind::SetActive, tgt, {}, JS_ToBool(ctx, argv[1]) == 1, 0.f});
  return JS_UNDEFINED;
}
inline JSValue ScriptHost::hostInstantiate(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  Inst* in = instOf(ctx);
  if (!in) return JS_UNDEFINED;
  std::string tpl; if (!nameArg(ctx, argc, argv, "instantiate", tpl)) return JS_EXCEPTION;
  in->host->checkTarget(in, tpl, "instantiate");
  // instantiate("Bullet") ou instantiate("Bullet", x, y, z). Devolve o nome da copia, que ja vale para destroy/setPosition.
  const std::string copy = tpl + "#" + std::to_string(++in->host->spawnSeq);
  Cmd c{CmdKind::Instantiate, tpl, copy, false, 0.f};
  if (argc >= 2) {   // posicao: ou as 3 coordenadas, ou nenhuma (meio termo e erro)
    double d[3];
    const char* ax[3] = {"x", "y", "z"};
    if (argc < 4) return JS_ThrowTypeError(ctx, "instantiate(template, x, y, z): give all three coordinates or none");
    for (int i = 0; i < 3; ++i) { if (!numArg(ctx, argc, argv, i + 1, "instantiate", ax[i], d[i])) return JS_EXCEPTION; c.v[i] = static_cast<float>(d[i]); }
    c.flag = true;
  }
  in->host->cmds.push_back(c);
  return JS_NewStringLen(ctx, copy.data(), copy.size());
}
inline bool ScriptHost::vec3Args(JSContext* ctx, int argc, JSValueConst* argv, int from, float out[3]) {
  if (argc < from + 3) return false;
  for (int i = 0; i < 3; ++i) { double d = 0.0; JS_ToFloat64(ctx, &d, argv[from + i]); out[i] = static_cast<float>(d); }
  return true;
}
inline JSValue ScriptHost::hostSetRot(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  Inst* in = instOf(ctx);
  if (!in) return JS_UNDEFINED;
  if (argc < 4) return JS_ThrowTypeError(ctx, "setRotation(name, x, y, z) needs 4 arguments, got %d", argc);
  std::string tgt; if (!nameArg(ctx, argc, argv, "setRotation", tgt)) return JS_EXCEPTION;
  Cmd c{CmdKind::SetRot, tgt, {}, false, 0.f};
  const char* ax[3] = {"x", "y", "z"};
  for (int i = 0; i < 3; ++i) { double d; if (!numArg(ctx, argc, argv, i + 1, "setRotation", ax[i], d)) return JS_EXCEPTION; c.v[i] = static_cast<float>(d); }
  in->host->checkTarget(in, tgt, "setRotation");
  in->host->cmds.push_back(c);
  return JS_UNDEFINED;
}
inline JSValue ScriptHost::hostSetColor(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  Inst* in = instOf(ctx);
  if (!in) return JS_UNDEFINED;
  if (argc < 4) return JS_ThrowTypeError(ctx, "setColor(name, r, g, b) needs 4 arguments, got %d", argc);
  std::string tgt; if (!nameArg(ctx, argc, argv, "setColor", tgt)) return JS_EXCEPTION;
  Cmd c{CmdKind::SetColor, tgt, {}, false, 0.f};
  const char* ax[3] = {"r", "g", "b"};
  for (int i = 0; i < 3; ++i) {
    double d; if (!numArg(ctx, argc, argv, i + 1, "setColor", ax[i], d)) return JS_EXCEPTION;
    if (d < 0.0 || d > 1.0) in->host->warn(in, "TOOL-S006", std::string("setColor: '") + ax[i] + "' should be between 0 and 1 (it is clamped)");
    c.v[i] = static_cast<float>(d);
  }
  in->host->checkTarget(in, tgt, "setColor");
  in->host->cmds.push_back(c);
  return JS_UNDEFINED;
}
inline JSValue ScriptHost::hostPosition(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  Inst* in = instOf(ctx);
  if (!in) return JS_UNDEFINED;
  std::string tgt; if (!nameArg(ctx, argc, argv, "position", tgt)) return JS_EXCEPTION;
  if (!in->host->lookup) return JS_UNDEFINED;
  float p[3] = {0.f, 0.f, 0.f};
  if (!in->host->lookup(tgt, p)) return JS_UNDEFINED;   // nao existe: undefined (tambem sinaliza o erro de nome em checkTarget)
  JSValue arr = JS_NewArray(ctx);
  for (uint32_t i = 0; i < 3; ++i) JS_SetPropertyUint32(ctx, arr, i, JS_NewFloat64(ctx, static_cast<double>(p[i])));
  return arr;
}
inline JSValue ScriptHost::hostConsoleWarn(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  Inst* in = instOf(ctx);
  if (argc > 0) tool::logWarn("[" + (in ? in->name : std::string("script")) + "] " + strOf(ctx, argv[0]));
  return JS_UNDEFINED;
}
inline JSValue ScriptHost::hostConsoleError(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  Inst* in = instOf(ctx);
  if (argc > 0) tool::logError("[" + (in ? in->name : std::string("script")) + "] " + strOf(ctx, argv[0]));
  return JS_UNDEFINED;
}
inline JSValue ScriptHost::hostInputDown(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  Inst* in = instOf(ctx);
  if (!in || argc < 1) return JS_FALSE;
  const std::string k = strOf(ctx, argv[0]);
  for (const auto& h : in->host->input.held) if (h == k) return JS_TRUE;
  return JS_FALSE;
}
inline JSValue ScriptHost::hostInputPressed(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  Inst* in = instOf(ctx);
  if (!in || argc < 1) return JS_FALSE;
  const std::string k = strOf(ctx, argv[0]);
  for (const auto& h : in->host->input.pressed) if (h == k) return JS_TRUE;
  return JS_FALSE;
}
inline JSValue ScriptHost::hostMouseDelta(JSContext* ctx, JSValueConst, int, JSValueConst*) {
  Inst* in = instOf(ctx);
  JSValue arr = JS_NewArray(ctx);
  const double dx = in ? in->host->input.mouseDx : 0.0, dy = in ? in->host->input.mouseDy : 0.0;
  JS_SetPropertyUint32(ctx, arr, 0, JS_NewFloat64(ctx, dx));
  JS_SetPropertyUint32(ctx, arr, 1, JS_NewFloat64(ctx, dy));
  return arr;
}
inline JSValue ScriptHost::hostDestroy(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  Inst* in = instOf(ctx);
  if (!in) return JS_UNDEFINED;
  std::string tgt; if (!nameArg(ctx, argc, argv, "destroy", tgt)) return JS_EXCEPTION;
  double sec = 0.0;
  if (argc > 1 && !numArg(ctx, argc, argv, 1, "destroy", "seconds", sec)) return JS_EXCEPTION;
  if (sec < 0.0) in->host->warn(in, "TOOL-S006", "destroy: negative time, destroying now");
  in->host->checkTarget(in, tgt, "destroy");
  in->host->cmds.push_back({CmdKind::Destroy, tgt, {}, false, static_cast<float>(sec < 0 ? 0.0 : sec)});
  return JS_UNDEFINED;
}
inline JSValue ScriptHost::hostAddComp(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  Inst* in = instOf(ctx);
  if (!in) return JS_UNDEFINED;
  if (argc < 2) return JS_ThrowTypeError(ctx, "addComponent(target, type) needs 2 arguments");
  std::string tgt; if (!nameArg(ctx, argc, argv, "addComponent", tgt)) return JS_EXCEPTION;
  in->host->checkTarget(in, tgt, "addComponent");
  in->host->cmds.push_back({CmdKind::AddComp, tgt, strOf(ctx, argv[1]), false, 0.f});
  return JS_UNDEFINED;
}
inline JSValue ScriptHost::hostRemoveComp(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  Inst* in = instOf(ctx);
  if (!in) return JS_UNDEFINED;
  if (argc < 2) return JS_ThrowTypeError(ctx, "removeComponent(target, type) needs 2 arguments");
  std::string tgt; if (!nameArg(ctx, argc, argv, "removeComponent", tgt)) return JS_EXCEPTION;
  in->host->checkTarget(in, tgt, "removeComponent");
  in->host->cmds.push_back({CmdKind::RemoveComp, tgt, strOf(ctx, argv[1]), false, 0.f});
  return JS_UNDEFINED;
}
inline JSValue ScriptHost::hostSetPos(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  Inst* in = instOf(ctx);
  if (!in) return JS_UNDEFINED;
  if (argc < 4) return JS_ThrowTypeError(ctx, "setPosition(name, x, y, z) needs 4 arguments, got %d", argc);
  std::string tgt; if (!nameArg(ctx, argc, argv, "setPosition", tgt)) return JS_EXCEPTION;
  Cmd c{CmdKind::SetPos, tgt, {}, false, 0.f};
  const char* ax[3] = {"x", "y", "z"};
  for (int i = 0; i < 3; ++i) { double d; if (!numArg(ctx, argc, argv, i + 1, "setPosition", ax[i], d)) return JS_EXCEPTION; c.v[i] = static_cast<float>(d); }
  in->host->checkTarget(in, tgt, "setPosition");
  in->host->cmds.push_back(c);
  return JS_UNDEFINED;
}
inline void ScriptHost::open(const std::vector<Entry>& list) {
  close();
  warned_.clear();
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
  JS_SetPropertyStr(ctx, g, "setRotation", JS_NewCFunction(ctx, &ScriptHost::hostSetRot, "setRotation", 4));
  JS_SetPropertyStr(ctx, g, "setColor", JS_NewCFunction(ctx, &ScriptHost::hostSetColor, "setColor", 4));
  JS_SetPropertyStr(ctx, g, "position", JS_NewCFunction(ctx, &ScriptHost::hostPosition, "position", 1));
  JSValue inp = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, inp, "down", JS_NewCFunction(ctx, &ScriptHost::hostInputDown, "down", 1));
  JS_SetPropertyStr(ctx, inp, "pressed", JS_NewCFunction(ctx, &ScriptHost::hostInputPressed, "pressed", 1));
  JS_SetPropertyStr(ctx, inp, "mouseDelta", JS_NewCFunction(ctx, &ScriptHost::hostMouseDelta, "mouseDelta", 0));
  JS_SetPropertyStr(ctx, g, "input", inp);
  JSValue dbg = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, dbg, "console", JS_NewCFunction(ctx, &ScriptHost::hostLog, "console", 1));
  JS_SetPropertyStr(ctx, dbg, "warn", JS_NewCFunction(ctx, &ScriptHost::hostConsoleWarn, "warn", 1));
  JS_SetPropertyStr(ctx, dbg, "error", JS_NewCFunction(ctx, &ScriptHost::hostConsoleError, "error", 1));
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
  int importLine = 0;
  const std::string src = stripModuleKeywords(en.source, importLine);
  if (importLine > 0) {
    I.failed = true; I.phase = "load"; I.errLine = importLine;
    I.error = "import is not available yet (script library planned) (linha " + std::to_string(importLine) + ")";
    tool::debug().report("TOOL-S008", tool::Severity::Error, "[" + I.name + "] " + I.error);
    return true;
  }

  deadline_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
  I.phase = "load";
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
  // (o aviso "sem start/update/OnEventTrigger" vem do verificador de scripts, script_check.hpp, para nao repetir)
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
  JSValue st = JS_GetPropertyStr(in.ctx, ex, "stack");
  const std::string stack = JS_IsUndefined(st) ? std::string() : strOf(in.ctx, st);
  JS_FreeValue(in.ctx, st);
  JS_FreeValue(in.ctx, ex);
  int line = 0, col = 0;
  parseScriptLocation(stack, in.name + ".js", line, col);
  in.failed = true;
  in.errLine = line; in.errCol = col;
  const std::string where = line > 0 ? " (linha " + std::to_string(line) + (col > 0 ? ", coluna " + std::to_string(col) : std::string()) + ")" : std::string();
  in.error = (in.phase.empty() ? std::string() : in.phase + ": ") + (msg.empty() ? std::string("unknown error") : msg) + where;
  // TOOL-S001: o script nao carregou (erro de sintaxe ou de codigo no topo). TOOL-S002: erro durante start/update/evento.
  const char* code = in.phase == "load" ? "TOOL-S001" : "TOOL-S002";
  tool::debug().report(code, tool::Severity::Error, "[" + in.name + "] " + in.error);
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
  in.started = true; in.phase = "start";
  JS_SetPropertyStr(in.ctx, in.timeObj, "now", JS_NewFloat64(in.ctx, now_));
  JSValue a[1] = {in.obj};
  return call(in, in.fStart, 1, a);
}
inline bool ScriptHost::callUpdate(Inst& in, float dt) {
  if (in.failed || !in.ctx) return false;
  in.phase = "update";
  JS_SetPropertyStr(in.ctx, in.timeObj, "now", JS_NewFloat64(in.ctx, now_));
  JS_SetPropertyStr(in.ctx, in.timeObj, "dt", JS_NewFloat64(in.ctx, static_cast<double>(dt)));
  JSValue a[2] = {in.obj, JS_NewFloat64(in.ctx, static_cast<double>(dt))};
  const bool ok = call(in, in.fUpdate, 2, a);
  JS_FreeValue(in.ctx, a[1]);
  return ok;
}
inline bool ScriptHost::callEvent(Inst& in, const std::string& ev) {
  if (in.failed || !in.ctx) return false;
  in.phase = "OnEventTrigger";
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
  const char* axes[3] = {"[0]", "[1]", "[2]"};
  std::string bad;   // primeiro valor invalido encontrado (NaN, infinito ou fora da faixa)
  for (int k = 0; k < 3; ++k) {
    JSValue arr = JS_GetPropertyStr(in.ctx, tr, keys[k]);
    for (uint32_t i = 0; i < 3; ++i) {
      JSValue v = JS_GetPropertyUint32(in.ctx, arr, i);
      double d = 0.0;
      if (JS_ToFloat64(in.ctx, &d, v) == 0 && d == d && d > -1e9 && d < 1e9) vals[k][i] = static_cast<float>(d);
      else if (bad.empty()) bad = std::string("obj.transform.") + keys[k] + axes[i];
      JS_FreeValue(in.ctx, v);
    }
    JS_FreeValue(in.ctx, arr);
  }
  JS_FreeValue(in.ctx, tr);
  if (!bad.empty()) {   // o Transform desse quadro nao muda; o aviso diz qual campo
    warn(&in, "TOOL-S006", bad + " is not a number (NaN or too large): the change was ignored");
    return false;
  }
  return true;
}
#else
inline void ScriptHost::open(const std::vector<Entry>& list) {
  close();
  warned_.clear();
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
