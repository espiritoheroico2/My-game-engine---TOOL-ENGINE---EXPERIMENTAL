// ToolEngine Editor - idiomas por pacotes.
//
// IDIOMA PADRAO = INGLES. A "chave" de cada texto e o proprio texto em ingles (T("Hierarchy")): se um pacote
// nao traduz uma frase, ela aparece em ingles em vez de quebrar, e sem nenhum arquivo a UI continua inteira.
// Pacotes sao arquivos JSON em assets/lang/<codigo>.json, sem precisar recompilar:
//   { "code": "pt-BR", "name": "Portugues (Brasil)", "strings": { "Hierarchy": "Hierarquia", ... } }
// "Export template" (Settings > Language) gera um pacote com todas as frases que a UI ja usou.
#pragma once
#include "tool/core/json.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace editor {

struct LangPack {
  std::string code, name, path;
};

class I18n {
public:
  static I18n& get() {
    static I18n i;
    return i;
  }

  // Le todos os *.json da pasta. O idioma-base (en) sempre existe, mesmo sem arquivo.
  void scan(const std::string& dir) {
    dir_ = dir;
    packs_.clear();
    packs_.push_back({"en", "English", ""});
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) return;
    for (auto& e : std::filesystem::directory_iterator(dir, ec)) {
      if (e.path().extension() != ".json") continue;
      std::ifstream f(e.path(), std::ios::binary);
      std::stringstream ss; ss << f.rdbuf();
      auto j = tool::Json::parse(ss.str());
      if (!j || !j->isObject() || j->operator[]("code").asString().empty()) continue;
      const std::string code = (*j)["code"].asString();
      if (code == "en") continue; // o ingles e o proprio texto-chave: nao ha o que traduzir
      packs_.push_back({code, (*j)["name"].asString().empty() ? code : (*j)["name"].asString(), e.path().generic_string()});
    }
    std::sort(packs_.begin() + 1, packs_.end(), [](const LangPack& a, const LangPack& b) { return a.name < b.name; });
  }

  // Tabela vazia = textos-chave (ingles).
  bool select(const std::string& code) {
    for (auto& p : packs_) {
      if (p.code != code) continue;
      table_.clear();
      code_ = p.code;
      if (p.path.empty()) return true;
      std::ifstream f(p.path, std::ios::binary);
      std::stringstream ss; ss << f.rdbuf();
      auto j = tool::Json::parse(ss.str());
      if (!j) return false;
      for (auto& [k, v] : (*j)["strings"].members())
        if (v.isString() && !v.asString().empty()) table_[k] = v.asString();
      return true;
    }
    return false;
  }

  const char* tr(const char* key) {
    seen_.insert(key);
    auto it = table_.find(key);
    return it == table_.end() ? key : it->second.c_str();
  }

  // Grava um pacote-modelo com todas as frases vistas ate agora (valor = texto em ingles, para traduzir).
  bool exportTemplate(const std::string& path) const {
    tool::Json root = tool::Json::object(), strings = tool::Json::object();
    root.set("code", "xx"); root.set("name", "New language");
    for (auto& k : seen_) {
      auto it = table_.find(k);
      strings.set(k, it == table_.end() ? k : it->second);
    }
    root.set("strings", strings);
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << root.dump();
    return static_cast<bool>(f);
  }

  const std::vector<LangPack>& packs() const { return packs_; }
  const std::string& code() const { return code_; }
  const std::string& dir() const { return dir_; }
  size_t phraseCount() const { return seen_.size(); }
  size_t translatedCount() const { size_t n = 0; for (auto& k : seen_) if (table_.count(k)) ++n; return n; }

private:
  std::string dir_ = "assets/lang", code_ = "en";
  std::vector<LangPack> packs_{{"en", "English", ""}};
  std::map<std::string, std::string> table_;
  std::set<std::string> seen_;
};

inline const char* T(const char* key) { return I18n::get().tr(key); }

// Texto formatado: o formato (traduzido) vem do pacote; os argumentos sao os mesmos em qualquer idioma.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
inline std::string F(const char* key, ...) {
  const char* fmt = I18n::get().tr(key);
  char buf[512];
  va_list ap; va_start(ap, key);
  std::vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  return buf;
}
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

// Titulo de janela com ID estavel: o texto muda com o idioma, o ID (layout salvo) nao.
inline std::string W(const char* key, const char* id) { return std::string(T(key)) + "###" + id; }

} // namespace editor
