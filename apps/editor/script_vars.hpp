// script_vars.hpp - Atributo [visible] dos scripts (M9): variaveis do script que aparecem no Inspector.
//
// SINTAXE ESCOLHIDA: um comentario-diretiva "//[visible]" (ou "// [visible]") SOZINHO na linha logo acima da
// declaracao. Motivo: e comentario valido em JavaScript (o QuickJS ignora; "@visible" nao e JS valido e quebraria o
// script), e o parser le linha a linha, sem precisar de um analisador de JS.
//
//   //[visible]
//   let speed = 3;            // numero  -> campo numerico
//   //[visible]
//   let title = "Hello";      // texto   -> campo de texto
//   //[visible]
//   let active = true;        // booleano -> caixa de marcar
//   //[visible]
//   let tint = "#FF8800";     // texto no formato #RRGGBB -> amostra de cor
//
// A declaracao aceita let, var ou const (com "export" opcional), um literal simples (numero, texto entre aspas,
// true/false) e ";" opcional. Qualquer outra forma e ignorada (o script continua valendo, so nao aparece no Inspector).
//
// FLUXO: ao carregar/editar o script, syncScriptVars() le as variaveis e guarda o valor de cada uma no componente
// ScriptVars do objeto (salvo na cena). O Inspector edita esse componente. Ao dar Play, applyScriptVars() troca o literal
// de cada declaracao no TEXTO do script (so em memoria, na mesma linha: os numeros de linha dos erros nao mudam),
// entao a variavel comeca o jogo com o valor do Inspector. Se o literal no codigo mudar depois (o usuario editou o
// script), o valor do codigo vence e o do Inspector e descartado.
#pragma once

#include "components.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace editor {

// Uma declaracao [visible] encontrada no texto do script.
struct VisibleDecl {
  std::string name;
  ScriptVarType type = ScriptVarType::Number;
  std::string literal;          // literal como esta no codigo (com as aspas, se texto)
  size_t litBegin = 0, litEnd = 0;   // intervalo do literal no texto do script
  int line = 0;                 // linha (1 = primeira) da declaracao
};

namespace script_vars_detail {

inline bool isIdStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$'; }
inline bool isIdChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$'; }

// Linha sem espacos nas pontas.
inline std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
  return s.substr(a, b - a);
}

// true se a linha e a diretiva "//[visible]" (aceita espaco depois de "//").
inline bool isDirective(const std::string& line) {
  const std::string t = trim(line);
  if (t.size() < 2 || t[0] != '/' || t[1] != '/') return false;
  return trim(t.substr(2)) == "[visible]";
}

// Valor de um literal de texto ('..' ou "..") ja sem aspas, com os escapes comuns resolvidos.
inline std::string unquote(const std::string& lit) {
  std::string out;
  for (size_t i = 1; i + 1 < lit.size(); ++i) {
    if (lit[i] == '\\' && i + 2 < lit.size()) {
      const char n = lit[++i];
      out += n == 'n' ? '\n' : n == 't' ? '\t' : n;
    } else out += lit[i];
  }
  return out;
}

// "#RRGGBB" -> rgb 0..1.
inline bool parseHexColor(const std::string& s, float rgb[3]) {
  if (s.size() != 7 || s[0] != '#') return false;
  unsigned v = 0;
  for (size_t i = 1; i < 7; ++i) {
    const char c = s[i];
    unsigned d;
    if (c >= '0' && c <= '9') d = static_cast<unsigned>(c - '0');
    else if (c >= 'a' && c <= 'f') d = static_cast<unsigned>(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F') d = static_cast<unsigned>(c - 'A' + 10);
    else return false;
    v = v * 16u + d;
  }
  rgb[0] = static_cast<float>((v >> 16) & 0xFFu) / 255.f; rgb[1] = static_cast<float>((v >> 8) & 0xFFu) / 255.f; rgb[2] = static_cast<float>(v & 0xFFu) / 255.f;
  return true;
}

inline std::string hexOf(const float rgb[3]) {
  char b[12];
  const auto ch = [](float c) { return static_cast<unsigned>(std::lround(std::clamp(c, 0.f, 1.f) * 255.f)); };
  std::snprintf(b, sizeof b, "#%02X%02X%02X", ch(rgb[0]), ch(rgb[1]), ch(rgb[2]));
  return b;
}

// Tenta ler a declaracao em `line` (a partir do inicio da linha no texto, `base`). Preenche nome, tipo e literal.
inline bool parseDecl(const std::string& line, size_t base, VisibleDecl& d) {
  size_t i = 0;
  const auto skipWs = [&] { while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i; };
  const auto word = [&](const char* w) {
    const size_t n = std::char_traits<char>::length(w);
    if (line.compare(i, n, w) != 0 || (i + n < line.size() && isIdChar(line[i + n]))) return false;
    i += n; return true;
  };
  skipWs();
  if (word("export")) skipWs();
  if (!(word("let") || word("var") || word("const"))) return false;
  skipWs();
  if (i >= line.size() || !isIdStart(line[i])) return false;
  const size_t nb = i;
  while (i < line.size() && isIdChar(line[i])) ++i;
  d.name = line.substr(nb, i - nb);
  skipWs();
  if (i >= line.size() || line[i] != '=' || (i + 1 < line.size() && line[i + 1] == '=')) return false;
  ++i; skipWs();
  if (i >= line.size()) return false;
  const size_t lb = i;
  if (line[i] == '"' || line[i] == '\'') {   // texto: termina na mesma aspa (aceita \ escapando)
    const char q = line[i++];
    while (i < line.size() && line[i] != q) { if (line[i] == '\\') ++i; ++i; }
    if (i >= line.size()) return false;
    ++i;
    d.literal = line.substr(lb, i - lb);
    float rgb[3];
    d.type = parseHexColor(unquote(d.literal), rgb) ? ScriptVarType::Color : ScriptVarType::Text;
  } else if (word("true") || word("false")) {
    d.literal = line.substr(lb, i - lb); d.type = ScriptVarType::Bool;
  } else {   // numero: -?digitos[.digitos][e[+-]digitos]
    if (line[i] == '-' || line[i] == '+') ++i;
    const size_t ds = i;
    while (i < line.size() && (std::isdigit(static_cast<unsigned char>(line[i])) || line[i] == '.')) ++i;
    if (i == ds) return false;
    if (i < line.size() && (line[i] == 'e' || line[i] == 'E')) {
      ++i; if (i < line.size() && (line[i] == '-' || line[i] == '+')) ++i;
      while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i]))) ++i;
    }
    d.literal = line.substr(lb, i - lb); d.type = ScriptVarType::Number;
  }
  // Depois do literal so pode haver ";" e/ou comentario: "let x = 3 + 4;" nao e literal simples e e ignorado.
  skipWs();
  if (i < line.size() && line[i] == ';') { ++i; skipWs(); }
  if (i < line.size() && !(line[i] == '/' && i + 1 < line.size() && line[i + 1] == '/')) return false;
  d.litBegin = base + lb; d.litEnd = base + lb + d.literal.size();
  return true;
}

} // namespace script_vars_detail

// Declaracoes [visible] do texto do script (no maximo kMaxScriptVars, sem nomes repetidos).
inline std::vector<VisibleDecl> parseVisibleVars(const std::string& src) {
  namespace d = script_vars_detail;
  std::vector<VisibleDecl> out;
  size_t pos = 0; int lineNo = 0;
  bool armed = false;   // a linha anterior (nao vazia) era a diretiva
  while (pos <= src.size()) {
    size_t nl = src.find('\n', pos);
    if (nl == std::string::npos) nl = src.size();
    const std::string line = src.substr(pos, nl - pos);
    ++lineNo;
    if (d::isDirective(line)) armed = true;
    else if (!d::trim(line).empty()) {
      if (armed && out.size() < static_cast<size_t>(kMaxScriptVars)) {
        VisibleDecl dc; dc.line = lineNo;
        bool dup = false;
        if (d::parseDecl(line, pos, dc)) {
          for (const VisibleDecl& o : out) if (o.name == dc.name) dup = true;
          if (!dup) out.push_back(dc);
        }
      }
      armed = false;
    }
    pos = nl + 1;
  }
  return out;
}

// Valor do literal no formato guardado: copia o literal do codigo em `v.def` e le o valor para o campo certo.
inline void scriptVarFromDecl(const VisibleDecl& d, ScriptVar& v) {
  namespace sd = script_vars_detail;
  std::snprintf(v.name, sizeof v.name, "%s", d.name.c_str());
  std::snprintf(v.def, sizeof v.def, "%s", d.literal.c_str());
  v.type = static_cast<int>(d.type);
  switch (d.type) {
    case ScriptVarType::Number: v.num = static_cast<float>(std::strtod(d.literal.c_str(), nullptr)); break;
    case ScriptVarType::Bool: v.flag = d.literal == "true"; break;
    case ScriptVarType::Text: std::snprintf(v.text, sizeof v.text, "%s", sd::unquote(d.literal).c_str()); break;
    case ScriptVarType::Color: sd::parseHexColor(sd::unquote(d.literal), v.rgb); break;
  }
}

// Atualiza a lista do objeto com o que o script declara agora. Variavel que continua igual (mesmo nome, tipo e
// literal do codigo) mantem o valor do Inspector; variavel nova ou cujo literal mudou no codigo recebe o valor do
// codigo; variavel que sumiu do script sai da lista. Retorna true se a lista mudou.
inline bool syncScriptVars(ScriptVars& sv, const std::vector<VisibleDecl>& decls) {
  std::vector<ScriptVar> out;
  for (const VisibleDecl& d : decls) {
    ScriptVar v;
    const ScriptVar* keep = nullptr;
    for (const ScriptVar& o : sv.items) if (d.name == o.name && o.type == static_cast<int>(d.type) && d.literal == o.def) { keep = &o; break; }
    if (keep) v = *keep; else scriptVarFromDecl(d, v);
    out.push_back(v);
  }
  bool same = out.size() == sv.items.size();
  for (size_t i = 0; same && i < out.size(); ++i) same = std::memcmp(&out[i], &sv.items[i], sizeof(ScriptVar)) == 0;
  if (same) return false;
  sv.items = std::move(out);
  return true;
}

// Literal de um valor, no formato do JavaScript (texto entre aspas duplas, escapando \ " e quebra de linha).
inline std::string scriptVarLiteral(const ScriptVar& v) {
  namespace sd = script_vars_detail;
  switch (static_cast<ScriptVarType>(v.type)) {
    case ScriptVarType::Number: { char b[40]; std::snprintf(b, sizeof b, "%.9g", static_cast<double>(v.num)); return b; }
    case ScriptVarType::Bool: return v.flag ? "true" : "false";
    case ScriptVarType::Color: return "\"" + sd::hexOf(v.rgb) + "\"";
    case ScriptVarType::Text: {
      std::string s = "\"";
      for (const char c : std::string(v.text)) {
        if (c == '\\' || c == '"') { s += '\\'; s += c; }
        else if (c == '\n') s += "\\n";
        else if (c == '\r') {}
        else if (c == '\t') s += "\\t";
        else s += c;
      }
      return s + "\"";
    }
  }
  return {};
}

// Devolve o texto do script com o literal de cada variavel [visible] trocado pelo valor do Inspector.
// So troca onde a variavel existe na lista e o literal do codigo ainda e o que estava quando o valor foi lido.
inline std::string applyScriptVars(const std::string& src, const ScriptVars& sv) {
  if (sv.items.empty()) return src;
  const std::vector<VisibleDecl> decls = parseVisibleVars(src);
  std::string out = src;
  for (size_t k = decls.size(); k-- > 0;) {   // de tras para frente: os intervalos anteriores continuam validos
    const VisibleDecl& d = decls[k];
    for (const ScriptVar& v : sv.items) {
      if (d.name != v.name || v.type != static_cast<int>(d.type) || d.literal != v.def) continue;
      out.replace(d.litBegin, d.litEnd - d.litBegin, scriptVarLiteral(v));
      break;
    }
  }
  return out;
}

} // namespace editor
