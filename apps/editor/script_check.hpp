// script_check.hpp - Verificador de scripts ToolObject. Roda enquanto voce digita (Inspector > ToolObject Script),
// ao salvar, ao dar Play e pelo botao "Verificar scripts" do Console.
//
// Dois niveis:
//   1. Sintaxe: o QuickJS compila o codigo SEM executar (erro com linha e coluna).   Codigo TOOL-S001.
//   2. Regras de uso: um tokenizador le o codigo (sem comentarios nem texto entre aspas) e acusa os erros
//      que costumam fazer um objeto "nao funcionar" sem mensagem: obj.position (nao existe), class, this,
//      console.log, nome de funcao digitado errado (com sugestao), numero errado de argumentos, tecla invalida.
//
// Codigos (documentados em docs/SCRIPT_ERRORS.md):
//   TOOL-S001 erro de sintaxe / codigo que nao carrega     TOOL-S002 erro durante start/update/evento
//   TOOL-S003 objeto sem start/update/OnEventTrigger        TOOL-S004 nome, argumento ou tecla invalidos
//   TOOL-S005 comando para objeto que nao existe (Play)     TOOL-S006 valor invalido ignorado (NaN, fora da faixa)
//   TOOL-S007 propriedade que nao existe em obj             TOOL-S008 recurso nao suportado (class, import, this)
#pragma once
#include "script_host.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace editor {

struct ScriptIssue {
  int line = 0, col = 0;
  tool::Severity sev = tool::Severity::Warning;
  std::string code, msg;
};

namespace scriptcheck_detail {

struct Tok {
  enum Kind : uint8_t { Ident, Str, Punct, Num };
  Kind kind = Punct;
  std::string text;   // Ident/Punct: o texto; Str: o conteudo entre aspas (sem as aspas)
  int line = 1, col = 1;
};

// Tokeniza o codigo. Pula comentarios // e /* */. Strings ('...', "...", `...`) viram um token Str.
inline std::vector<Tok> tokenize(const std::string& s) {
  std::vector<Tok> out;
  int line = 1, col = 1;
  size_t i = 0;
  auto adv = [&](size_t n) { for (size_t k = 0; k < n && i < s.size(); ++k, ++i) { if (s[i] == '\n') { ++line; col = 1; } else ++col; } };
  while (i < s.size()) {
    const char c = s[i];
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { adv(1); continue; }
    if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') { while (i < s.size() && s[i] != '\n') adv(1); continue; }
    if (c == '/' && i + 1 < s.size() && s[i + 1] == '*') {
      adv(2);
      while (i + 1 < s.size() && !(s[i] == '*' && s[i + 1] == '/')) adv(1);
      adv(2);
      continue;
    }
    Tok t; t.line = line; t.col = col;
    if (c == '"' || c == '\'' || c == '`') {
      const char q = c;
      adv(1);
      std::string body;
      while (i < s.size() && s[i] != q && (q == '`' || s[i] != '\n')) {
        if (s[i] == '\\' && i + 1 < s.size()) { body += s[i]; adv(1); }
        body += s[i]; adv(1);
      }
      if (i < s.size() && s[i] == q) adv(1);
      t.kind = Tok::Str; t.text = body;
      out.push_back(t);
      continue;
    }
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$') {
      std::string id;
      while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_' || s[i] == '$')) { id += s[i]; adv(1); }
      t.kind = Tok::Ident; t.text = id;
      out.push_back(t);
      continue;
    }
    if (std::isdigit(static_cast<unsigned char>(c))) {
      std::string num;
      while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '.')) { num += s[i]; adv(1); }
      t.kind = Tok::Num; t.text = num;
      out.push_back(t);
      continue;
    }
    t.kind = Tok::Punct; t.text = std::string(1, c);
    adv(1);
    out.push_back(t);
  }
  return out;
}

inline int editDistance(const std::string& a, const std::string& b) {
  std::vector<int> prev(b.size() + 1), cur(b.size() + 1);
  for (size_t j = 0; j <= b.size(); ++j) prev[j] = static_cast<int>(j);
  for (size_t i = 1; i <= a.size(); ++i) {
    cur[0] = static_cast<int>(i);
    for (size_t j = 1; j <= b.size(); ++j) {
      const int sub = prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1);
      cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, sub});
    }
    std::swap(prev, cur);
  }
  return prev[b.size()];
}

inline std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

// Sugestao para um nome desconhecido: mesmo nome sem diferenca de maiusculas, ou erro de digitacao pequeno.
inline std::string suggest(const std::string& word, const std::vector<std::string>& options) {
  const std::string lw = lower(word);
  for (const auto& o : options) if (lower(o) == lw) return o;
  // "setpos" -> "setPosition": o que foi digitado e o comeco de um nome longo o bastante para ser o mesmo
  if (lw.size() >= 4) for (const auto& o : options) if (o.size() > lw.size() && lower(o).compare(0, lw.size(), lw) == 0) return o;
  std::string best; int bestD = 3;
  for (const auto& o : options) {
    const int d = editDistance(lw, lower(o));
    if (d < bestD && d <= static_cast<int>(o.size() / 3) + 1) { best = o; bestD = d; }
  }
  return best;
}

// Funcoes da API (nome -> quantidade de argumentos aceita, [min, max]).
struct ApiFn { const char* name; int min, max; const char* sig; };
inline const std::vector<ApiFn>& apiFns() {
  static const std::vector<ApiFn> fns = {
    {"send", 2, 2, "send(target, event)"},
    {"setActive", 2, 2, "setActive(target, on)"},
    {"instantiate", 1, 4, "instantiate(template, x, y, z)"},
    {"destroy", 1, 2, "destroy(target, seconds)"},
    {"addComponent", 2, 2, "addComponent(target, type)"},
    {"removeComponent", 2, 2, "removeComponent(target, type)"},
    {"setPosition", 4, 4, "setPosition(target, x, y, z)"},
    {"setRotation", 4, 4, "setRotation(target, x, y, z)"},
    {"setColor", 4, 4, "setColor(target, r, g, b)"},
    {"position", 1, 1, "position(name)"},
  };
  return fns;
}

// Nomes que podem ser chamados sem declaracao no script (API + builtins do JavaScript usados em scripts).
inline const std::set<std::string>& knownGlobals() {
  static const std::set<std::string> g = {
    "send", "setActive", "instantiate", "destroy", "addComponent", "removeComponent", "setPosition", "setRotation",
    "setColor", "position", "input", "debug", "time", "vec3", "Math", "JSON", "Number", "String", "Boolean", "Array",
    "Object", "Date", "RegExp", "Error", "TypeError", "RangeError", "Symbol", "Map", "Set", "Promise", "parseInt",
    "parseFloat", "isNaN", "isFinite", "undefined", "NaN", "Infinity", "globalThis"};
  return g;
}

inline const std::set<std::string>& keywords() {
  static const std::set<std::string> k = {"if", "for", "while", "switch", "catch", "function", "return", "typeof", "new",
    "delete", "void", "in", "of", "else", "do", "try", "throw", "instanceof", "let", "const", "var", "yield", "await", "super"};
  return k;
}

inline const std::vector<std::string>& keyNames() {
  static const std::vector<std::string> k = {"W", "A", "S", "D", "Q", "E", "R", "F", "1", "2", "Space", "Shift", "Ctrl", "Alt",
    "Up", "Down", "Left", "Right", "Enter", "Escape", "Mouse0", "Mouse1"};
  return k;
}

// Conta argumentos a partir do '(' em toks[open]. Devolve -1 se nao achar o ')'.
inline int countArgs(const std::vector<Tok>& toks, size_t open, bool& empty) {
  int depth = 0, commas = 0;
  bool any = false;
  for (size_t k = open; k < toks.size(); ++k) {
    const Tok& t = toks[k];
    if (t.kind == Tok::Punct) {
      if (t.text == "(" || t.text == "[" || t.text == "{") { ++depth; if (depth == 1) continue; }
      else if (t.text == ")" || t.text == "]" || t.text == "}") { --depth; if (depth == 0) { empty = !any; return any ? commas + 1 : 0; } }
      else if (t.text == "," && depth == 1) { ++commas; continue; }
    }
    if (depth >= 1) any = true;
  }
  return -1;
}

} // namespace scriptcheck_detail

// Verifica um script (texto do arquivo). Devolve os problemas ordenados por linha. Sem erros = script valido para o runtime.
inline std::vector<ScriptIssue> checkScriptSource(const std::string& source, const std::string& name) {
  using scriptcheck_detail::Tok;
  std::vector<ScriptIssue> out;
  auto add = [&](int line, int col, tool::Severity sev, const char* code, const std::string& msg) {
    ScriptIssue is; is.line = line; is.col = col; is.sev = sev; is.code = code; is.msg = msg;
    out.push_back(is);
  };
  using S = tool::Severity;

  // 1. sintaxe (o QuickJS compila sem executar)
  int sl = 0, sc = 0; std::string smsg;
  if (ScriptHost::compiledIn() && !ScriptHost::compileCheck(source, name, sl, sc, smsg)) {
    add(sl > 0 ? sl : 1, sc > 0 ? sc : 1, S::Error, "TOOL-S001", smsg);
  }

  // 2. regras de uso (tokens)
  const std::vector<Tok> toks = scriptcheck_detail::tokenize(source);
  const auto& api = scriptcheck_detail::apiFns();
  const auto& globals = scriptcheck_detail::knownGlobals();
  const auto& kw = scriptcheck_detail::keywords();

  // nomes declarados no proprio script (funcoes, variaveis, parametros de funcoes)
  std::set<std::string> declared;
  for (size_t k = 0; k + 1 < toks.size(); ++k) {
    if (toks[k].kind != Tok::Ident) continue;
    const std::string& w = toks[k].text;
    if ((w == "let" || w == "const" || w == "var" || w == "function" || w == "class") && toks[k + 1].kind == Tok::Ident)
      declared.insert(toks[k + 1].text);
    if (w == "function") {   // parametros: identificadores dentro dos parenteses, antes de ',' ')' ou '='
      size_t p = k + 1; while (p < toks.size() && toks[p].kind != Tok::Punct) ++p;   // nome da funcao (se houver)
      while (p < toks.size() && !(toks[p].kind == Tok::Punct && toks[p].text == "(")) { if (toks[p].text == "{") break; ++p; }
      for (size_t q = p + 1; q < toks.size() && !(toks[q].kind == Tok::Punct && toks[q].text == ")"); ++q)
        if (toks[q].kind == Tok::Ident) declared.insert(toks[q].text);
    }
  }
  // "export" no inicio da linha e aceito; "import" no inicio da linha nao e
  for (size_t k = 0; k < toks.size(); ++k) {
    const Tok& t = toks[k];
    if (t.kind == Tok::Ident && t.text == "import" && (k == 0 || toks[k - 1].line < t.line)) {
      add(t.line, t.col, S::Error, "TOOL-S008", "import is not available yet (script libraries are planned). Put the code in this file.");
    }
    if (t.kind == Tok::Ident && t.text == "class" && k + 1 < toks.size() && toks[k + 1].kind == Tok::Ident) {
      add(t.line, t.col, S::Error, "TOOL-S008", "classes do not work in scripts. Use functions: export function update(obj, dt) { ... }");
    }
    if (t.kind == Tok::Ident && t.text == "this" && k + 1 < toks.size() && toks[k + 1].text == ".") {
      add(t.line, t.col, S::Warning, "TOOL-S008", "'this' is not used in scripts: the object is the first parameter, obj (obj.transform.pos[0] ...)");
    }
  }

  bool hasEntry = false;
  for (size_t k = 0; k < toks.size(); ++k) {
    const Tok& t = toks[k];
    if (t.kind != Tok::Ident) continue;
    const bool afterDot = k > 0 && toks[k - 1].kind == Tok::Punct && toks[k - 1].text == ".";
    const bool afterFunction = k > 0 && toks[k - 1].kind == Tok::Ident && toks[k - 1].text == "function";
    const bool isCall = k + 1 < toks.size() && toks[k + 1].kind == Tok::Punct && toks[k + 1].text == "(";
    const bool memberOf = k + 2 < toks.size() && toks[k + 1].kind == Tok::Punct && toks[k + 1].text == "." && toks[k + 2].kind == Tok::Ident;

    // declaracoes das funcoes de entrada
    if (afterFunction && (t.text == "start" || t.text == "update" || t.text == "OnEventTrigger")) {
      hasEntry = true;
      if (t.text == "update" && k + 2 < toks.size() && toks[k + 2].text == ")")
        add(t.line, t.col, S::Warning, "TOOL-S004", "update needs the object parameter: export function update(obj, dt) { ... }");
    }
    if (afterFunction) {   // nomes parecidos com os da Unity ou com diferenca de maiusculas
      static const std::set<std::string> unity = {"Start", "Update", "Awake", "OnUpdate", "Init", "FixedUpdate", "LateUpdate",
        "OnDestroy", "OnEnable", "OnStart", "OnEventtrigger", "onstart", "onupdate"};
      const std::string& fn = t.text;
      if (unity.count(fn) || fn == "onEventTrigger" || fn == "onEventtrigger")
        add(t.line, t.col, S::Warning, "TOOL-S004", "'" + fn + "' is not called by the engine. The entry points are start(obj), update(obj, dt) and OnEventTrigger(obj, name), with these exact names");
    }
    if (afterFunction) continue;   // declaracao, nao chamada

    // obj.position, obj.rotation... (o certo e obj.transform.pos / rot / scale)
    if (t.text == "obj" && memberOf) {
      const std::string& p = toks[k + 2].text;
      std::string fix;
      if (p == "position" || p == "pos" || p == "translation") fix = "obj.transform.pos";
      else if (p == "rotation" || p == "rot") fix = "obj.transform.rot";
      else if (p == "scale") fix = "obj.transform.scale";
      if (!fix.empty())
        add(t.line, t.col, S::Error, "TOOL-S007", "obj." + p + " does not exist. Use " + fix + " (an array [x, y, z]; change it in place or assign a new array)");
    }
    // obj.transform.pos.x -> arrays usam [0] [1] [2]
    if ((t.text == "x" || t.text == "y" || t.text == "z") && afterDot && k >= 3 && toks[k - 1].text == "." &&
        toks[k - 2].kind == Tok::Ident && (toks[k - 2].text == "pos" || toks[k - 2].text == "rot" || toks[k - 2].text == "scale")) {
      const std::string idx = t.text == "x" ? "[0]" : t.text == "y" ? "[1]" : "[2]";
      add(t.line, t.col, S::Error, "TOOL-S007", "transform." + toks[k - 2].text + "." + t.text + " does not exist. Arrays use " + idx);
    }
    // console.log, input.X, debug.X, time.X
    if (memberOf && !afterDot) {
      const std::string& obj = t.text;
      const std::string& prop = toks[k + 2].text;
      if (obj == "console") add(t.line, t.col, S::Error, "TOOL-S004", "console.log does not exist. Use debug.console(message)");
      else if (obj == "Input" || obj == "Debug" || obj == "Time")
        add(t.line, t.col, S::Error, "TOOL-S004", "'" + obj + "' must be lowercase: " + scriptcheck_detail::lower(obj));
      else if (obj == "Scene" || obj == "Physics" || obj == "Narrative" || obj == "Audio")
        add(t.line, t.col, S::Warning, "TOOL-S004", obj + "." + prop + " is not available in scripts yet (see Help > Script API)");
      else if (obj == "input" && prop != "down" && prop != "pressed" && prop != "mouseDelta")
        add(toks[k + 2].line, toks[k + 2].col, S::Warning, "TOOL-S004", "input." + prop + " does not exist. Use input.down(key), input.pressed(key) or input.mouseDelta()");
      else if (obj == "debug" && prop != "console" && prop != "warn" && prop != "error")
        add(toks[k + 2].line, toks[k + 2].col, S::Warning, "TOOL-S004", "debug." + prop + " does not exist. Use debug.console(message), debug.warn(message) or debug.error(message)");
      else if (obj == "time" && prop != "now" && prop != "dt")
        add(toks[k + 2].line, toks[k + 2].col, S::Warning, "TOOL-S004", "time." + prop + " does not exist. Use time.now or time.dt");
    }
    // input.down("W") / input.pressed("w"): nome de tecla
    if ((t.text == "down" || t.text == "pressed") && afterDot && k >= 2 && toks[k - 2].text == "input" &&
        k + 2 < toks.size() && toks[k + 1].text == "(" && toks[k + 2].kind == Tok::Str) {
      const std::string& key = toks[k + 2].text;
      const auto& names = scriptcheck_detail::keyNames();
      if (std::find(names.begin(), names.end(), key) == names.end()) {
        const std::string closest = scriptcheck_detail::suggest(key, names);
        add(toks[k + 2].line, toks[k + 2].col, S::Warning, "TOOL-S004",
            "'" + key + "' is not a key name" + (closest.empty() ? std::string() : ". Did you mean \"" + closest + "\"?") +
            " Keys: W A S D Q E R F 1 2 Space Shift Ctrl Alt Up Down Left Right Enter Escape Mouse0 Mouse1");
      }
    }
    if (afterDot || !isCall || kw.count(t.text)) continue;
    // chamadas de funcao: API conhecida, numero de argumentos, nome desconhecido
    const scriptcheck_detail::ApiFn* hit = nullptr;
    for (const auto& f : api) if (t.text == f.name) hit = &f;
    if (hit) {
      bool empty = false;
      const int n = scriptcheck_detail::countArgs(toks, k + 1, empty);
      if (n >= 0) {
        const bool okCount = n >= hit->min && n <= hit->max && !(std::string(hit->name) == "instantiate" && (n == 2 || n == 3));
        if (!okCount) {
          const std::string why = std::string(hit->name) == "instantiate" && (n == 2 || n == 3)
              ? "give all three coordinates (x, y, z) or none"
              : "takes " + std::string(hit->min == hit->max ? "" : "between ") + (hit->min == hit->max ? std::to_string(hit->min) : std::to_string(hit->min) + " and " + std::to_string(hit->max)) + " arguments";
          add(t.line, t.col, S::Warning, "TOOL-S004", std::string(hit->sig) + ": " + why + ", found " + std::to_string(n));
        }
      }
      continue;
    }
    if (globals.count(t.text) || declared.count(t.text)) continue;
    std::vector<std::string> names;
    for (const auto& f : api) names.push_back(f.name);
    const std::string closest = scriptcheck_detail::suggest(t.text, names);
    add(t.line, t.col, S::Warning, "TOOL-S004",
        "unknown function '" + t.text + "'" + (closest.empty()
            ? std::string(". If you wrote it, declare it: function " + t.text + "() { ... }")
            : ". Did you mean " + closest + "?"));
  }
  if (!hasEntry) {
    add(1, 1, S::Warning, "TOOL-S003", "no start, update or OnEventTrigger function: this object will do nothing. Add: export function update(obj, dt) {}");
  }

  std::stable_sort(out.begin(), out.end(), [](const ScriptIssue& a, const ScriptIssue& b) {
    if (a.line != b.line) return a.line < b.line;
    return a.col < b.col;
  });
  (void)name;
  return out;
}

// Resumo curto para a barra de status: "2 erros, 1 aviso" ou "OK".
inline std::string issueSummary(const std::vector<ScriptIssue>& v) {
  int e = 0, w = 0;
  for (const auto& i : v) (i.sev == tool::Severity::Error ? e : w)++;
  if (e == 0 && w == 0) return "OK";
  return std::to_string(e) + (e == 1 ? " error" : " errors") + ", " + std::to_string(w) + (w == 1 ? " warning" : " warnings");
}

} // namespace editor
