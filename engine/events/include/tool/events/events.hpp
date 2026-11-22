// ToolEngine - EventBus + EventArbiter.
// Evita eventos disparando juntos por engano: grupos de exclusao mutua,
// one-shot atomico, cooldown, limite de profundidade, ciclos e deduplicacao.
#pragma once
#include "tool/core/types.hpp"
#include "tool/debug/debug.hpp"

#include <any>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace tool {

using EventId = u32;

// Resultado de tryFire: Allowed se o evento pode disparar, ou o motivo da recusa.
enum class Verdict : u8 { Allowed, AlreadyConsumed, Cooldown, GroupBusy, DepthExceeded, Cycle, Duplicate };

// Regras de um evento: grupo de exclusao mutua, disparo unico e intervalo minimo (cooldown).
struct EventRule {
  u32 group = 0;        // 0 = sem grupo; mesmo grupo = exclusao mutua no tick
  bool oneShot = false; // dispara uma unica vez (consumo atomico)
  double cooldown = 0;  // segundos entre disparos
};

// Arbitro de eventos: decide se um evento pode disparar agora, aplicando regras, grupos, cooldown e protecao contra ciclos.
class EventArbiter {
public:
  static constexpr size_t kMaxDepth = 16;

  // Define as regras do evento id.
  void setRule(EventId id, EventRule r) { rules_[id] = r; }

  // Chamar no inicio de cada tick: libera os grupos e ids de mensagem do tick anterior.
  void beginTick() {
    busyGroups_.clear();
    seenMessages_.clear();
  }

  // Reinicia regras one-shot (ex.: nova sessao/reativavel).
  void resetOneShot(EventId id) { consumed_.erase(id); }

  // Pede para disparar o evento id no instante now. Devolve o veredito. Se messageId != 0, descarta repeticoes no mesmo tick. Atenção: o messageId fica marcado como visto mesmo se o evento for recusado depois.
  Verdict tryFire(EventId id, double now, u64 messageId = 0) {
    if (messageId != 0 && !seenMessages_.insert(messageId).second) {
      debug().report(diag::kEventDup, Severity::Warning, "evento duplicado ignorado (msg " + std::to_string(messageId) + ")");
      return Verdict::Duplicate;
    }
    if (chain_.size() >= kMaxDepth) {
      debug().report(diag::kEventDepth, Severity::Error, "profundidade maxima de eventos excedida");
      return Verdict::DepthExceeded;
    }
    for (EventId c : chain_)
      if (c == id) {
        debug().report(diag::kEventLoop, Severity::Error, "ciclo de eventos detectado (causa: " + causeChain() + ")");
        return Verdict::Cycle;
      }
    const EventRule r = rules_.count(id) ? rules_[id] : EventRule{};
    if (r.oneShot && consumed_.count(id)) return Verdict::AlreadyConsumed;
    if (r.cooldown > 0) {
      auto it = lastFire_.find(id);
      if (it != lastFire_.end() && now - it->second < r.cooldown) return Verdict::Cooldown;
    }
    if (r.group != 0 && busyGroups_.count(r.group)) {
      debug().report(diag::kEventConflict, Severity::Warning, "evento " + std::to_string(id) + " bloqueado: grupo " + std::to_string(r.group) + " ja disparou neste tick");
      return Verdict::GroupBusy;
    }
    // Aprovado: consome atomicamente.
    if (r.oneShot) consumed_.insert(id);
    if (r.group != 0) busyGroups_.insert(r.group);
    if (r.cooldown > 0) lastFire_[id] = now;
    return Verdict::Allowed;
  }

  // Empilha o evento na cadeia de causa atual (usado ao entrar nos handlers).
  void push(EventId id) { chain_.push_back(id); }
  // Desempilha o evento no topo da cadeia de causa.
  void pop() {
    if (!chain_.empty()) chain_.pop_back();
  }
  // Texto com a cadeia de causa atual (ex.: "1 -> 4 -> 2"), para mensagens de diagnostico.
  std::string causeChain() const {
    std::string s;
    for (EventId c : chain_) s += (s.empty() ? "" : " -> ") + std::to_string(c);
    return s;
  }

private:
  std::unordered_map<EventId, EventRule> rules_;
  std::unordered_set<u32> busyGroups_;
  std::unordered_set<EventId> consumed_;
  std::unordered_set<u64> seenMessages_;
  std::unordered_map<EventId, double> lastFire_;
  std::vector<EventId> chain_;
};

// Barramento de eventos: nomes viram ids, handlers se inscrevem e emit() passa pelo arbitro antes de chamar os handlers.
class EventBus {
public:
  using Handler = std::function<void(const std::any&)>;

  // Devolve o id (>= 1) do nome, criando-o se ainda nao existir.
  EventId intern(const std::string& name) {
    auto [it, ok] = eventIds_.try_emplace(name, static_cast<EventId>(eventIds_.size() + 1));
    (void)ok;
    return it->second;
  }

  // Inscreve um handler no evento id. Devolve o numero de handlers do evento.
  size_t subscribe(EventId id, Handler h) {
    handlers_[id].push_back(std::move(h));
    return handlers_[id].size();
  }

  // Arbitro usado por este barramento.
  EventArbiter& arbiter() { return arbiter_; }

  // Dispara imediatamente passando pelo arbitro. Handlers podem emitir (cadeia de causa).
  // Atenção: se um handler lancar excecao, a cadeia de causa nao e desempilhada e fica inconsistente nas proximas chamadas.
  Verdict emit(EventId id, double now, std::any payload = {}, u64 messageId = 0) {
    const Verdict verdict = arbiter_.tryFire(id, now, messageId);
    if (verdict != Verdict::Allowed) return verdict;

    arbiter_.push(id);
    const auto handlersIt = handlers_.find(id);
    if (handlersIt != handlers_.end() && !handlersIt->second.empty()) {
      const auto snapshot = handlersIt->second;
      for (const auto& handler : snapshot) handler(payload);
    }
    arbiter_.pop();
    return verdict;
  }

private:
  std::unordered_map<std::string, EventId> eventIds_;
  std::unordered_map<EventId, std::vector<Handler>> handlers_;
  EventArbiter arbiter_;
};

} // namespace tool
