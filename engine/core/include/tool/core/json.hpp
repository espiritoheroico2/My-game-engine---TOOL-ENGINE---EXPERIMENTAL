// ToolEngine - JSON minimo e seguro (saves, manifestos de mods, grafos narrativos).
// Limites rigidos de profundidade/tamanho: entrada hostil nunca causa estouro de pilha.
#pragma once
#include "tool/core/types.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tool {

// Valor JSON (null, bool, numero, string, array ou objeto) com parser e serializador. Fail-closed: erro de parse vira nullopt.
class Json {
public:
  // Tipo do valor armazenado.
  enum class Type : u8 { Null, Bool, Number, String, Array, Object };
  using Array = std::vector<Json>;
  using Object = std::map<std::string, Json>;

  // Cria um valor null.
  Json() = default;
  // Cria um valor null a partir de nullptr.
  Json(std::nullptr_t) {}
  // Cria um valor booleano.
  Json(bool b) : type_(Type::Bool), b_(b) {}
  // Cria um valor numerico (double).
  Json(double n) : type_(Type::Number), n_(n) {}
  // Cria um valor numerico a partir de int.
  Json(int n) : type_(Type::Number), n_(n) {}
  // Cria uma string a partir de um literal C.
  Json(const char* s) : type_(Type::String), s_(s) {}
  // Cria uma string (move o conteudo).
  Json(std::string s) : type_(Type::String), s_(std::move(s)) {}
  // Cria um array vazio.
  static Json array() { Json j; j.type_ = Type::Array; return j; }
  // Cria um objeto vazio.
  static Json object() { Json j; j.type_ = Type::Object; return j; }

  // Tipo atual do valor.
  Type type() const { return type_; }
  // True se o valor e null.
  bool isNull() const { return type_ == Type::Null; }
  // True se o valor e objeto.
  bool isObject() const { return type_ == Type::Object; }
  // True se o valor e array.
  bool isArray() const { return type_ == Type::Array; }
  // True se o valor e string.
  bool isString() const { return type_ == Type::String; }
  // True se o valor e numero.
  bool isNumber() const { return type_ == Type::Number; }
  // True se o valor e booleano.
  bool isBool() const { return type_ == Type::Bool; }

  // Valor booleano, ou d se o tipo nao for bool.
  bool asBool(bool d = false) const { return type_ == Type::Bool ? b_ : d; }
  // Valor numerico, ou d se o tipo nao for numero.
  double asNumber(double d = 0) const { return type_ == Type::Number ? n_ : d; }
  // Texto da string, ou string vazia se o tipo nao for string.
  const std::string& asString() const { static const std::string e; return type_ == Type::String ? s_ : e; }
  // Elementos do array, ou array vazio se o tipo nao for array.
  const Array& items() const { static const Array e; return type_ == Type::Array ? a_ : e; }
  // Membros do objeto, ou mapa vazio se o tipo nao for objeto.
  const Object& members() const { static const Object e; return type_ == Type::Object ? o_ : e; }

  // Acessa membro por chave. Devolve um null estatico se nao for objeto ou a chave nao existir.
  const Json& operator[](const std::string& k) const {
    static const Json nul;
    if (type_ != Type::Object) return nul;
    auto it = o_.find(k);
    return it == o_.end() ? nul : it->second;
  }
  // True se o objeto tem a chave k.
  bool has(const std::string& k) const { return type_ == Type::Object && o_.count(k); }
  // Define o membro k (converte o valor em objeto se necessario). Devolve referencia ao valor guardado.
  Json& set(const std::string& k, Json v) { if (type_ != Type::Object) *this = object(); return o_[k] = std::move(v); }
  // Adiciona v ao fim do array (converte em array se necessario). Devolve referencia ao item criado.
  Json& push(Json v) { if (type_ != Type::Array) *this = array(); a_.push_back(std::move(v)); return a_.back(); }

  // Remove a chave k de um objeto. True se ela existia.
  bool erase(const std::string& k) { return type_ == Type::Object && o_.erase(k) > 0; }
  // Item do array na posicao i. Lanca std::out_of_range se i estiver fora do limite.
  Json& at(size_t i) { return a_.at(i); }
  // Remove o item da posicao i de um array. Nada acontece se o indice for invalido.
  void removeAt(size_t i) { if (type_ == Type::Array && i < a_.size()) a_.erase(a_.begin() + static_cast<std::ptrdiff_t>(i)); }
  // Ponteiro para o membro k, ou nullptr se nao existir ou o valor nao for objeto.
  Json* find(const std::string& k) { if (type_ != Type::Object) return nullptr; auto it = o_.find(k); return it == o_.end() ? nullptr : &it->second; }

  // Serializa o valor em string JSON compacta.
  std::string dump() const { std::string out; write(out); return out; }

  // Limites do parser: profundidade maxima de aninhamento e tamanho maximo do texto em bytes.
  struct Limits { size_t maxDepth = 64; size_t maxBytes = 16u << 20; };

  // Contramedidas configuraveis (F-06). Padrao = comportamento anterior.
  //   rejectDuplicateKeys: true recusa objeto com a mesma chave duas vezes (o padrao mantem o ultimo valor).
  // Nota: o tipo e Json::Options (publico), nao Parser::Options, porque Parser e interno.
  struct Options { bool rejectDuplicateKeys = false; };

  // Retorna nullopt para qualquer erro (fail-closed).
  static std::optional<Json> parse(std::string_view text) { return parse(text, Limits{}, Options{}); }
  // Parse com limites personalizados e opcoes padrao.
  static std::optional<Json> parse(std::string_view text, Limits lim) { return parse(text, lim, Options{}); }
  // Parse completo: retorna nullopt se o texto for invalido, exceder os limites ou tiver lixo no fim.
  static std::optional<Json> parse(std::string_view text, Limits lim, Options opt) {
    if (text.size() > lim.maxBytes) return std::nullopt;
    Parser p{text, 0, lim.maxDepth, opt};
    p.ws();
    auto v = p.value(0);
    if (!v) return std::nullopt;
    p.ws();
    if (p.i != text.size()) return std::nullopt;
    return v;
  }

private:
  // Parser recursivo interno (descendente) sobre a string de entrada; guarda a posicao atual i.
  struct Parser {
    std::string_view t; size_t i; size_t maxDepth; Options opt;
    // Pula espacos em branco (espaco, tab, quebra de linha).
    void ws() { while (i < t.size() && (t[i] == ' ' || t[i] == '\n' || t[i] == '\t' || t[i] == '\r')) ++i; }
    // Consome a palavra w se estiver na posicao atual.
    bool lit(std::string_view w) { if (t.substr(i, w.size()) == w) { i += w.size(); return true; } return false; }
    // Le um valor a partir da posicao atual. depth controla o limite de aninhamento.
    std::optional<Json> value(size_t depth) {
      if (depth > maxDepth || i >= t.size()) return std::nullopt;
      const char c = t[i];
      if (c == '{') return object(depth);
      if (c == '[') return arr(depth);
      if (c == '"') { auto s = str(); if (!s) return std::nullopt; return Json(std::move(*s)); }
      if (lit("true")) return Json(true);
      if (lit("false")) return Json(false);
      if (lit("null")) return Json();
      return num();
    }
    // True se a posicao k existe e contem um digito.
    bool dig(size_t k) const { return k < t.size() && t[k] >= '0' && t[k] <= '9'; }
    // Gramatica JSON estrita (F-06): -? (0 | [1-9][0-9]*) (\.[0-9]+)? ([eE][+-]?[0-9]+)?
    // Rejeita zero a esquerda, '+' inicial, '.' ou expoente sem digito.
    // Le um numero conforme a gramatica JSON estrita. nullopt se for invalido ou nao finito.
    std::optional<Json> num() {
      const size_t s = i;
      if (i < t.size() && t[i] == '-') ++i;
      if (!dig(i)) return std::nullopt;
      if (t[i] == '0') { ++i; if (dig(i)) return std::nullopt; }
      else while (dig(i)) ++i;
      if (i < t.size() && t[i] == '.') { ++i; if (!dig(i)) return std::nullopt; while (dig(i)) ++i; }
      if (i < t.size() && (t[i] == 'e' || t[i] == 'E')) {
        ++i;
        if (i < t.size() && (t[i] == '+' || t[i] == '-')) ++i;
        if (!dig(i)) return std::nullopt;
        while (dig(i)) ++i;
      }
      // Atenção: a conversao usa strtod, que depende do locale (separador decimal). Em locale com virgula decimal o numero e recusado (falha segura).
      const std::string tmp(t.substr(s, i - s));
      char* end = nullptr;
      const double d = std::strtod(tmp.c_str(), &end);
      if (end != tmp.c_str() + tmp.size() || !std::isfinite(d)) return std::nullopt;
      return Json(d);
    }
    // Le 4 digitos hexadecimais em cp. False se faltarem digitos ou algum nao for hex.
    bool hex4(unsigned& cp) {
      if (i + 4 > t.size()) return false;
      cp = 0;
      for (int k = 0; k < 4; ++k) {
        const char h = t[i++]; cp <<= 4;
        if (h >= '0' && h <= '9') cp |= static_cast<unsigned>(h - '0');
        else if (h >= 'a' && h <= 'f') cp |= static_cast<unsigned>(h - 'a' + 10);
        else if (h >= 'A' && h <= 'F') cp |= static_cast<unsigned>(h - 'A' + 10);
        else return false;
      }
      return true;
    }
    // Acrescenta o codepoint cp em UTF-8 ao texto out.
    static void appendUtf8(std::string& out, unsigned cp) {
      if (cp < 0x80) { out += static_cast<char>(cp); return; }
      if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); return; }
      if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F));
        return;
      }
      out += static_cast<char>(0xF0 | (cp >> 18)); out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F));
    }
    // Le uma string (a partir da aspa de abertura). Trata escapes e pares UTF-16. nullopt se mal formada.
    std::optional<std::string> str() {
      ++i; // "
      std::string out;
      while (i < t.size()) {
        const char c = t[i++];
        if (c == '"') return out;
        if (static_cast<unsigned char>(c) < 0x20) return std::nullopt;
        if (c != '\\') { out += c; continue; }
        if (i >= t.size()) return std::nullopt;
        const char e = t[i++];
        switch (e) {
        case '"': out += '"'; break; case '\\': out += '\\'; break; case '/': out += '/'; break;
        case 'n': out += '\n'; break; case 't': out += '\t'; break; case 'r': out += '\r'; break;
        case 'b': out += '\b'; break; case 'f': out += '\f'; break;
        case 'u': {
          // UTF-16 (F-06): alto D800-DBFF exige baixo DC00-DFFF logo depois; baixo isolado ou alto sem baixo e erro.
          unsigned cp = 0;
          if (!hex4(cp)) return std::nullopt;
          if (cp >= 0xD800 && cp <= 0xDBFF) {
            unsigned lo = 0;
            if (i + 2 > t.size() || t[i] != '\\' || t[i + 1] != 'u') return std::nullopt;
            i += 2;
            if (!hex4(lo) || lo < 0xDC00 || lo > 0xDFFF) return std::nullopt;
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
          } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            return std::nullopt;
          }
          appendUtf8(out, cp);
          break;
        }
        default: return std::nullopt;
        }
      }
      return std::nullopt; // string nao terminada
    }
    // Le um array; depth e o nivel de aninhamento atual.
    std::optional<Json> arr(size_t depth) {
      ++i; Json j = Json::array(); ws();
      if (i < t.size() && t[i] == ']') { ++i; return j; }
      while (true) {
        ws(); auto v = value(depth + 1); if (!v) return std::nullopt;
        j.a_.push_back(std::move(*v)); ws();
        if (i >= t.size()) return std::nullopt;
        if (t[i] == ',') { ++i; continue; }
        if (t[i] == ']') { ++i; return j; }
        return std::nullopt;
      }
    }
    // Le um objeto; depth e o nivel de aninhamento atual. Chaves duplicadas so sao recusadas se rejectDuplicateKeys estiver ligado.
    std::optional<Json> object(size_t depth) {
      ++i; Json j = Json::object(); ws();
      if (i < t.size() && t[i] == '}') { ++i; return j; }
      while (true) {
        ws();
        if (i >= t.size() || t[i] != '"') return std::nullopt;
        auto k = str(); if (!k) return std::nullopt;
        if (opt.rejectDuplicateKeys && j.o_.count(*k)) return std::nullopt;
        ws();
        if (i >= t.size() || t[i] != ':') return std::nullopt;
        ++i; ws();
        auto v = value(depth + 1); if (!v) return std::nullopt;
        j.o_[*k] = std::move(*v); ws();
        if (i >= t.size()) return std::nullopt;
        if (t[i] == ',') { ++i; continue; }
        if (t[i] == '}') { ++i; return j; }
        return std::nullopt;
      }
    }
  };

  // Escreve a string s entre aspas, com escapes JSON (caracteres de controle viram \uXXXX).
  static void esc(std::string& o, const std::string& s) {
    o += '"';
    for (unsigned char c : s) {
      switch (c) {
      case '"': o += "\\\""; break; case '\\': o += "\\\\"; break; case '\n': o += "\\n"; break;
      case '\t': o += "\\t"; break; case '\r': o += "\\r"; break;
      default:
        if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; } else o += static_cast<char>(c);
      }
    }
    o += '"';
  }
  // Serializa este valor em o. Atenção: numeros infinitos ou NaN sao escritos como 'inf'/'nan', que nao sao JSON valido.
  void write(std::string& o) const {
    switch (type_) {
    case Type::Null: o += "null"; break;
    case Type::Bool: o += b_ ? "true" : "false"; break;
    case Type::Number: {
      char b[40];
      // Atenção: numeros nao finitos (inf/NaN) saem como texto invalido; quem monta o JSON deve evitar esses valores.
      if (n_ == std::floor(n_) && std::abs(n_) < 1e15) std::snprintf(b, sizeof b, "%lld", static_cast<long long>(n_));
      else std::snprintf(b, sizeof b, "%.17g", n_);
      o += b; break;
    }
    case Type::String: esc(o, s_); break;
    case Type::Array: { o += '['; bool f = true; for (auto& v : a_) { if (!f) o += ','; f = false; v.write(o); } o += ']'; break; }
    case Type::Object: { o += '{'; bool f = true; for (auto& [k, v] : o_) { if (!f) o += ','; f = false; esc(o, k); o += ':'; v.write(o); } o += '}'; break; }
    }
  }

  Type type_ = Type::Null;
  bool b_ = false;
  double n_ = 0;
  std::string s_;
  Array a_;
  Object o_;
};

} // namespace tool
