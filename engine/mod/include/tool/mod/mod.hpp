// ToolEngine - ModTool: mods com permissao, overrideTool (por tag) e modFunction (funcoes substituiveis).
// Regras de seguranca: tags "seladas" nao podem ser sobrescritas; permissoes limitadas pelo nivel de
// confianca; mod que falha ou estoura o orcamento de tempo e desligado (fail-closed) e desfeito.
#pragma once
#include "tool/core/json.hpp"
#include "tool/debug/debug.hpp"

#include <chrono>
#include <exception>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace tool::mod {

enum class Trust : u8 { Untrusted, Community, Verified, Core };

enum Perm : u32 {
  kPermNone = 0, kPermReadWorld = 1, kPermWriteWorld = 2, kPermSpawn = 4, kPermAudio = 8,
  kPermNetwork = 16, kPermFileRead = 32, kPermScript = 64
};

// O que cada nivel de confianca pode receber, no maximo.
inline u32 maxPermissions(Trust t) {
  switch (t) {
  case Trust::Untrusted: return kPermReadWorld | kPermAudio;
  case Trust::Community: return kPermReadWorld | kPermWriteWorld | kPermSpawn | kPermAudio | kPermScript;
  case Trust::Verified: return kPermReadWorld | kPermWriteWorld | kPermSpawn | kPermAudio | kPermScript | kPermFileRead;
  case Trust::Core: return ~0u;
  }
  return 0;
}

namespace code {
inline constexpr const char* kBadManifest = "TOOL-E210";
inline constexpr const char* kPermDenied = "TOOL-E211";
inline constexpr const char* kSealed = "TOOL-E212";
inline constexpr const char* kModFault = "TOOL-E213";
inline constexpr const char* kModDisabled = "TOOL-E214";
inline constexpr const char* kPriorityTie = "TOOL-W210";
inline constexpr const char* kSlow = "TOOL-W211";
} // namespace code

struct Manifest {
  std::string id, name, version;
  u32 apiVersion = 1;
  u32 permissions = 0;
};

inline bool validId(const std::string& s) {
  if (s.empty() || s.size() > 64) return false;
  for (char c : s) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return false;
  return true;
}
inline bool validSemver(const std::string& v) {
  int dots = 0; bool digit = false;
  for (char c : v) { if (c == '.') { if (!digit) return false; ++dots; digit = false; } else if (c >= '0' && c <= '9') digit = true; else return false; }
  return dots == 2 && digit && v.size() <= 20;
}

inline u32 parsePerm(const std::string& s) {
  static const std::map<std::string, u32> m = {{"read_world", kPermReadWorld}, {"write_world", kPermWriteWorld}, {"spawn", kPermSpawn},
      {"audio", kPermAudio}, {"network", kPermNetwork}, {"file_read", kPermFileRead}, {"script", kPermScript}};
  auto it = m.find(s); return it == m.end() ? 0xFFFFFFFFu : it->second;
}

// Estado de um mod carregado.
struct ModInfo {
  Manifest manifest;
  Trust trust = Trust::Untrusted;
  bool enabled = true;
  int faults = 0;
  size_t loadOrder = 0;
};

using Args = Json;
using ToolHandler = std::function<void(Json& ctx)>;
using FunctionImpl = std::function<Json(const Args&)>;
using ModFunctionImpl = std::function<Json(const Args&, const FunctionImpl& next)>; // next = original/proximo mod

class ModTool {
public:
  static constexpr int kMaxFaults = 3;
  static constexpr u32 kEngineApi = 1;

  void setCallBudgetMs(double ms) { budgetMs_ = ms; }

  // ---- host define o que existe e o que pode ser modificado ----
  void defineTool(const std::string& tag, ToolHandler base, bool moddable = true) { tools_[tag].base = std::move(base); tools_[tag].moddable = moddable; }
  void defineFunction(const std::string& name, FunctionImpl impl, bool moddable = true) { funcs_[name].base = std::move(impl); funcs_[name].moddable = moddable; }
  void sealTag(const std::string& tag) { tools_[tag].moddable = false; }

  // ---- carregamento ----
  bool parseManifest(const Json& j, Trust trust, Manifest& out) {
    if (!j.isObject() || !validId(j["id"].asString()) || !validSemver(j["version"].asString())) return bad("manifesto: id/versao invalidos");
    out = {};
    out.id = j["id"].asString(); out.name = j["name"].asString().substr(0, 80); out.version = j["version"].asString();
    out.apiVersion = static_cast<u32>(j["api"].asNumber(1));
    if (out.apiVersion != kEngineApi) return bad("manifesto: api " + std::to_string(out.apiVersion) + " incompativel com a engine (" + std::to_string(kEngineApi) + ")");
    for (auto& p : j["permissions"].items()) {
      const u32 v = parsePerm(p.asString());
      if (v == 0xFFFFFFFFu) return bad("manifesto: permissao desconhecida '" + p.asString() + "'");
      out.permissions |= v;
    }
    if (out.permissions & ~maxPermissions(trust)) {
      debug().report(code::kPermDenied, Severity::Error, "mod '" + out.id + "' pede permissoes acima do nivel de confianca");
      return false;
    }
    return true;
  }

  bool load(const Manifest& m, Trust trust) {
    if (mods_.count(m.id)) return bad("mod ja carregado: " + m.id);
    ModInfo mi; mi.manifest = m; mi.trust = trust; mi.loadOrder = nextOrder_++;
    mods_[m.id] = mi;
    return true;
  }

  void unload(const std::string& modId) {
    mods_.erase(modId);
    for (auto& [t, e] : tools_) std::erase_if(e.overrides, [&](const Override& o) { return o.mod == modId; });
    for (auto& [n, f] : funcs_) std::erase_if(f.mods, [&](const FnMod& o) { return o.mod == modId; });
  }

  bool has(const std::string& modId) const { return mods_.count(modId); }
  bool enabled(const std::string& modId) const { auto it = mods_.find(modId); return it != mods_.end() && it->second.enabled; }
  int faults(const std::string& modId) const { auto it = mods_.find(modId); return it == mods_.end() ? 0 : it->second.faults; }

  bool hasPermission(const std::string& modId, Perm p) const {
    auto it = mods_.find(modId);
    return it != mods_.end() && it->second.enabled && (it->second.manifest.permissions & p) == p;
  }
  // API do host chama isto antes de qualquer acao privilegiada em nome de um mod.
  bool require(const std::string& modId, Perm p) {
    if (hasPermission(modId, p)) return true;
    debug().report(code::kPermDenied, Severity::Error, "mod '" + modId + "' sem permissao para esta acao");
    return false;
  }

  // ---- overrideTool: substitui o comportamento de uma tag ----
  bool overrideTool(const std::string& modId, const std::string& tag, ToolHandler h, int priority = 0) {
    if (!require(modId, kPermWriteWorld)) return false;
    auto it = tools_.find(tag);
    if (it == tools_.end()) return bad("overrideTool: tag desconhecida '" + tag + "'");
    if (!it->second.moddable) { debug().report(code::kSealed, Severity::Error, "tag '" + tag + "' e selada: mod '" + modId + "' nao pode sobrescrever"); return false; }
    it->second.overrides.push_back({modId, std::move(h), priority, mods_[modId].loadOrder});
    return true;
  }

  // Executa o comportamento da tag: maior prioridade vence; empate -> o carregado por ultimo, com aviso.
  // `ctx` e o contexto (JSON). Retorna false se a tag nao existe.
  bool runTool(const std::string& tag, Json& ctx) {
    auto it = tools_.find(tag);
    if (it == tools_.end()) return false;
    Entry& e = it->second;
    const Override* win = nullptr;
    bool tie = false;
    for (auto& o : e.overrides) {
      if (!enabled(o.mod)) continue;
      if (!win || o.priority > win->priority || (o.priority == win->priority && o.order > win->order)) { tie = win && o.priority == win->priority; win = &o; }
      else if (o.priority == win->priority) tie = true;
    }
    if (tie) debug().report(code::kPriorityTie, Severity::Warning, "empate de prioridade em '" + tag + "'; vence o mod carregado por ultimo");
    if (win) {
      const std::string modId = win->mod; ToolHandler h = win->handler;
      Json backup = ctx;
      if (guarded(modId, [&] { h(ctx); })) return true;
      ctx = backup; // desfaz efeitos parciais do mod que falhou e cai no comportamento base
    }
    if (e.base) e.base(ctx);
    return true;
  }

  // ---- modFunction: funcoes substituiveis em cadeia ----
  bool modFunction(const std::string& modId, const std::string& name, ModFunctionImpl impl, int priority = 0) {
    if (!require(modId, kPermScript)) return false;
    auto it = funcs_.find(name);
    if (it == funcs_.end()) return bad("modFunction: funcao desconhecida '" + name + "'");
    if (!it->second.moddable) { debug().report(code::kSealed, Severity::Error, "funcao '" + name + "' e selada"); return false; }
    it->second.mods.push_back({modId, std::move(impl), priority, mods_[modId].loadOrder});
    return true;
  }

  Json call(const std::string& name, const Args& args) {
    auto it = funcs_.find(name);
    if (it == funcs_.end()) { bad("funcao inexistente: " + name); return Json(); }
    std::vector<const FnMod*> chain;
    for (auto& m : it->second.mods) if (enabled(m.mod)) chain.push_back(&m);
    std::sort(chain.begin(), chain.end(), [](auto* a, auto* b) { return a->priority != b->priority ? a->priority > b->priority : a->order > b->order; });
    return invoke(it->second, chain, 0, args);
  }

private:
  struct Override { std::string mod; ToolHandler handler; int priority; size_t order; };
  struct FnMod { std::string mod; ModFunctionImpl impl; int priority; size_t order; };
  struct Entry { ToolHandler base; bool moddable = true; std::vector<Override> overrides; };
  struct Func { FunctionImpl base; bool moddable = true; std::vector<FnMod> mods; };

  bool bad(const std::string& m) { debug().report(code::kBadManifest, Severity::Error, m); return false; }

  Json invoke(Func& f, const std::vector<const FnMod*>& chain, size_t i, const Args& args) {
    if (i >= chain.size()) return f.base ? f.base(args) : Json();
    const FnMod* m = chain[i];
    Json result; bool ok = true;
    FunctionImpl next = [&, i](const Args& a) { return invoke(f, chain, i + 1, a); };
    ok = guarded(m->mod, [&] { result = m->impl(args, next); });
    return ok ? result : invoke(f, chain, i + 1, args); // mod falhou: pula para o proximo/original
  }

  // Executa codigo de mod com captura de excecao e medicao de tempo. 3 falhas => mod desligado.
  template <class F> bool guarded(const std::string& modId, F&& fn) {
    const auto t0 = std::chrono::steady_clock::now();
    bool ok = true;
    try { fn(); } catch (const std::exception& e) { ok = false; fault(modId, std::string("excecao: ") + e.what()); } catch (...) { ok = false; fault(modId, "excecao desconhecida"); }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (ok && budgetMs_ > 0 && ms > budgetMs_) { fault(modId, "estourou o orcamento de tempo (" + std::to_string(ms) + " ms)"); debug().report(code::kSlow, Severity::Warning, "mod '" + modId + "' lento"); }
    return ok;
  }
  void fault(const std::string& modId, const std::string& why) {
    auto it = mods_.find(modId);
    if (it == mods_.end()) return;
    ++it->second.faults;
    debug().report(code::kModFault, Severity::Error, "mod '" + modId + "': " + why);
    if (it->second.faults >= kMaxFaults && it->second.enabled) {
      it->second.enabled = false;
      debug().report(code::kModDisabled, Severity::Error, "mod '" + modId + "' desligado apos " + std::to_string(kMaxFaults) + " falhas");
    }
  }

  std::map<std::string, ModInfo> mods_;
  std::map<std::string, Entry> tools_;
  std::map<std::string, Func> funcs_;
  size_t nextOrder_ = 0;
  double budgetMs_ = 0;
};

} // namespace tool::mod
