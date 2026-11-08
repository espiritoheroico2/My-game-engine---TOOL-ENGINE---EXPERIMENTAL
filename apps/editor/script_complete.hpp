// script_complete.hpp - Logica pura do editor de script (sem ImGui, testavel pelo selftest):
//  * apiItems():        nomes da API de script (funcoes, metodos e propriedades), lidos do proprio toolengine.d.ts;
//  * completionWord():  a palavra (com pontos, ex.: "input.do") antes do cursor;
//  * completionsFor():  sugestoes que comecam com essa palavra;
//  * applyCompletion(): troca a palavra pelo texto escolhido e devolve o novo texto e o novo cursor;
//  * scriptSnippets():  os exemplos do Help (que o selftest roda) viram snippets; snippetsJson() os exporta ao VS Code.
#pragma once

#include "help_content.hpp"
#include "script_kit.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace editor {

// kind: "function" (global), "method" (metodo de objeto), "property" (campo de objeto).
// ins: o texto inserido no lugar da palavra (funcoes e metodos ja trazem o "(").
struct ApiItem { std::string name, kind, sig, doc, ins; };

namespace complete_detail {
inline std::string trimRight(std::string s) { while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back(); return s; }
inline std::string trimLeft(const std::string& s) { size_t i = 0; while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i; return s.substr(i); }
inline bool startsWith(const std::string& s, const std::string& p) { return s.size() >= p.size() && s.compare(0, p.size(), p) == 0; }
inline bool isWordChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.'; }
} // namespace complete_detail

// Lista da API lida do toolengine.d.ts (a mesma fonte do VS Code). Calculada uma vez.
inline const std::vector<ApiItem>& apiItems() {
  static const std::vector<ApiItem> items = [] {
    using namespace complete_detail;
    std::vector<ApiItem> out;
    const std::string dts = scriptkit_detail::kDts;
    std::vector<std::string> lines;
    for (size_t p = 0; p <= dts.size();) { size_t e = dts.find('\n', p); if (e == std::string::npos) e = dts.size(); lines.push_back(dts.substr(p, e - p)); p = e + 1; }
    std::string owner; bool inObject = false; std::string prevDoc;
    for (const std::string& raw : lines) {
      const std::string line = trimRight(raw);
      const std::string t = trimLeft(line);
      std::string doc;
      if (startsWith(t, "/**") && t.size() > 5 && t.substr(t.size() - 2) == "*/") { doc = trimLeft(t.substr(3, t.size() - 5)); prevDoc = doc; }
      else if (!t.empty() && !startsWith(t, "/**")) { /* doc de uma linha: so vale para a linha seguinte */ }
      if (inObject) {
        if (line == "};") { inObject = false; prevDoc.clear(); continue; }
        // membro: "  nome(...)" (metodo) ou "  readonly nome: tipo;" / "  nome: tipo;" (propriedade)
        if (line.size() < 3 || line[0] != ' ' || line[1] != ' ' || line[2] == ' ') { prevDoc.clear(); continue; }
        std::string body = line.substr(2);
        if (startsWith(body, "readonly ")) body = body.substr(9);
        size_t k = 0; while (k < body.size() && (std::isalnum(static_cast<unsigned char>(body[k])) || body[k] == '_')) ++k;
        if (k == 0) { prevDoc.clear(); continue; }
        const std::string member = body.substr(0, k);
        std::string sig = body; if (!sig.empty() && sig.back() == ';') sig.pop_back();
        const bool method = k < body.size() && body[k] == '(';
        ApiItem it;
        it.name = owner + "." + member; it.kind = method ? "method" : "property";
        it.sig = owner + "." + sig; it.doc = prevDoc; it.ins = method ? it.name + "(" : it.name;
        out.push_back(it); prevDoc.clear();
        continue;
      }
      if (startsWith(t, "declare function ")) {
        std::string rest = t.substr(17);
        const size_t paren = rest.find('(');
        if (paren == std::string::npos) continue;
        ApiItem it;
        it.name = rest.substr(0, paren); it.kind = "function";
        std::string sig = "function " + rest; if (!sig.empty() && sig.back() == ';') sig.pop_back();
        it.sig = sig; it.doc = prevDoc; it.ins = it.name + "(";
        out.push_back(it); prevDoc.clear();
        continue;
      }
      if (startsWith(t, "declare const ") && t.size() > 2 && t.back() == '{') {
        const size_t colon = t.find(':', 14);
        if (colon != std::string::npos) { owner = t.substr(14, colon - 14); inObject = true; prevDoc.clear(); }
        continue;
      }
      if (!t.empty() && !startsWith(t, "/**") && !startsWith(t, "*")) prevDoc.clear();
    }
    // campos do objeto que o script recebe (interface ToolObject)
    for (const char* n : {"obj.name", "obj.transform.pos", "obj.transform.rot", "obj.transform.scale"}) {
      ApiItem it; it.name = n; it.kind = "property"; it.sig = std::string(n) + ": (ToolObject)"; it.doc = "campo do objeto recebido por update/start"; it.ins = n;
      out.push_back(it);
    }
    return out;
  }();
  return items;
}

// Palavra que termina no cursor (letras, digitos, '_' e '.'). Vazia se nada.
inline std::string completionWord(const std::string& text, size_t cursor) {
  if (cursor > text.size()) cursor = text.size();
  size_t s = cursor;
  while (s > 0 && complete_detail::isWordChar(text[s - 1])) --s;
  return text.substr(s, cursor - s);
}

// Sugestoes para a palavra: as que comecam com ela (sem diferenciar maiusculas). Palavra vazia = nada, a menos que `all`.
inline std::vector<ApiItem> completionsFor(const std::string& word, bool all = false, size_t limit = 40) {
  auto lower = [](std::string s) { for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; };
  const std::string w = lower(word);
  std::vector<ApiItem> out;
  if (w.empty() && !all) return out;
  for (const ApiItem& it : apiItems()) {
    if (lower(it.name).rfind(w, 0) == 0 && it.name != word) out.push_back(it);
  }
  std::stable_sort(out.begin(), out.end(), [](const ApiItem& a, const ApiItem& b) { return a.name.size() < b.name.size(); });
  if (out.size() > limit) out.resize(limit);
  return out;
}

struct TextEdit { std::string text; size_t cursor = 0; };

// Troca a palavra antes do cursor por `ins`.
inline TextEdit applyCompletion(const std::string& text, size_t cursor, const std::string& ins) {
  if (cursor > text.size()) cursor = text.size();
  const size_t start = cursor - completionWord(text, cursor).size();
  TextEdit r;
  r.text = text.substr(0, start) + ins + text.substr(cursor);
  r.cursor = start + ins.size();
  return r;
}

// Insere `s` na posicao do cursor (snippets).
inline TextEdit insertAt(const std::string& text, size_t cursor, const std::string& s) {
  if (cursor > text.size()) cursor = text.size();
  TextEdit r;
  r.text = text.substr(0, cursor) + s + text.substr(cursor);
  r.cursor = cursor + s.size();
  return r;
}

// Snippet = um exemplo do Help (aba "script_examples"): numero, titulo e codigo.
struct Snippet { int n = 0; std::string title, code; };

// Exemplos do Help com `function` no codigo (os mesmos que o selftest roda). Titulo vem do "# N. Titulo" acima.
inline std::vector<Snippet> scriptSnippets() {
  std::vector<Snippet> out;
  for (const HelpTopic& t : helpTopics()) {
    if (std::string(t.id) != "script_examples") continue;
    const std::string body = t.bodyEn;
    std::string title; int headingNo = 0;
    bool inCode = false; std::string code;
    for (size_t p = 0; p < body.size();) {
      size_t e = body.find('\n', p); if (e == std::string::npos) e = body.size();
      const std::string line = body.substr(p, e - p); p = e + 1;
      if (line.rfind("```", 0) == 0) {
        if (!inCode) { inCode = true; code.clear(); continue; }
        inCode = false;
        if (code.find("function") != std::string::npos) {
          Snippet s; s.n = headingNo; s.title = title; s.code = code;
          while (!s.code.empty() && s.code.back() == '\n') s.code.pop_back();
          out.push_back(s);
        }
        continue;
      }
      if (inCode) { code += line + "\n"; continue; }
      if (line.rfind("# ", 0) == 0) {
        // "# 12. Titulo" -> numero 12 e titulo
        const std::string rest = line.substr(2);
        const size_t dot = rest.find(". ");
        if (dot != std::string::npos && dot > 0 && std::all_of(rest.begin(), rest.begin() + static_cast<std::ptrdiff_t>(dot), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; })) {
          headingNo = std::stoi(rest.substr(0, dot)); title = rest.substr(dot + 2);
        }
      }
    }
  }
  return out;
}

// JSON de snippets do VS Code, gerado dos exemplos do Help (nao existe mais uma lista escrita a mao).
inline std::string snippetsJson() {
  auto esc = [](const std::string& s) {
    std::string o;
    for (char c : s) {
      switch (c) {
        case '"': o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\r': break;
        case '\t': o += "\\t"; break;
        default: o += c;
      }
    }
    return o;
  };
  std::string out = "{\n";
  const auto snips = scriptSnippets();
  for (size_t i = 0; i < snips.size(); ++i) {
    const Snippet& s = snips[i];
    out += "  \"ToolEngine example " + std::to_string(s.n) + ": " + esc(s.title) + "\": {\n";
    out += "    \"prefix\": \"tool" + std::to_string(s.n) + "\",\n";
    out += "    \"scope\": \"javascript\",\n";
    out += "    \"description\": \"" + esc(s.title) + "\",\n";
    out += "    \"body\": [\n";
    size_t p = 0; bool first = true;
    while (p <= s.code.size()) {
      size_t e = s.code.find('\n', p); if (e == std::string::npos) e = s.code.size();
      out += first ? "      \"" : ",\n      \"";
      out += esc(s.code.substr(p, e - p)) + "\"";
      first = false;
      p = e + 1;
      if (e == s.code.size()) break;
    }
    out += "\n    ]\n  }";
    out += i + 1 < snips.size() ? ",\n" : "\n";
  }
  out += "}\n";
  return out;
}

// Escreve .vscode/toolengine.code-snippets na raiz do projeto (so se mudou). Nao lanca excecao.
inline void writeVsCodeSnippets(const std::filesystem::path& root) {
  if (root.empty()) return;
  scriptkit_detail::writeIfChanged(root / ".vscode" / "toolengine.code-snippets", snippetsJson());
}

} // namespace editor
