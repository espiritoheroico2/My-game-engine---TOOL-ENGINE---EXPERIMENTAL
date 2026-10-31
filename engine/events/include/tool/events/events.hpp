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

enum class Verdict : u8 { Allowed, AlreadyConsumed, Cooldown, GroupBusy, DepthExceeded, Cycle, Duplicate };

struct EventRule {
  u32 group = 0;        // 0 = sem grupo; mesmo grupo = exclusao mutua no tick
  bool oneShot = false; // dispara uma unica vez (consumo atomico)
  double cooldown = 0;  // segundos entre disparos
};

class EventArbiter {
public:
  static constexpr size_t kMaxDepth = 16;

  void setRule(EventId id, EventRule r) { rules_[id] = r; }

  // Chamar no inicio de cada tick: libera os grupos e ids de mensagem do tick anterior.
  void beginTick() {
    busyGroups_.clear();
    seenMessages_.clear();
  }

  // Reinicia regras one-shot (ex.: nova sessao/reativavel).
  void resetOneShot(EventId id) { consumed_.erase(id); }

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

  void push(EventId id) { chain_.push_back(id); }
  void pop() {
    if (!chain_.empty()) chain_.pop_back();
  }
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

class EventBus {
public:
  using Handler = std::function<void(const std::any&)>;

  EventId intern(const std::string& name) {
    auto [it, ok] = ids_.try_emplace(name, static_cast<EventId>(ids_.size() + 1));
    (void)ok;
    return it->second;
  }

  size_t subscribe(EventId id, Handler h) {
    handlers_[id].push_back(std::move(h));
    return handlers_[id].size();
  }

  EventArbiter& arbiter() { return arbiter_; }

  // Dispara imediatamente passando pelo arbitro. Handlers podem emitir (cadeia de causa).
  Verdict emit(EventId id, double now, std::any payload = {}, u64 messageId = 0) {
    const Verdict v = arbiter_.tryFire(id, now, messageId);
    if (v != Verdict::Allowed) return v;
    arbiter_.push(id);
    if (auto it = handlers_.find(id); it != handlers_.end())
      for (auto& h : std::vector<Handler>(it->second)) h(payload);
    arbiter_.pop();
    return v;
  }

private:
  std::unordered_map<std::string, EventId> ids_;
  std::unordered_map<EventId, std::vector<Handler>> handlers_;
  EventArbiter arbiter_;
};

} // namespace tool
