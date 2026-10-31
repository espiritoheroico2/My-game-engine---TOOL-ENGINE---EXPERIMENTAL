// ToolEngine - Scene + ToolObject + DeleteAndReuse (pool de reaproveitamento).
//
// ToolObject = handle leve para uma entidade ECS.
// DeleteAndReuse(): o objeto some (dormente, handle antigo invalido); se for criado
// outro identico (mesma assinatura = tags + componentes) ele e reaproveitado.
// Dormentes sem uso expiram por tempo / limite por assinatura / limite total.
#pragma once
#include "tool/ecs/world.hpp"
#include "tool/events/events.hpp"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace tool {

// Receita de criacao de um objeto. A assinatura identifica "objetos identicos".
class Blueprint {
public:
  Blueprint& tag(TagId t) {
    mask_.set(t);
    return *this;
  }
  template <class T, class... Args> Blueprint& with(Args... args) {
    steps_.push_back({componentId<T>(), [=](World& w, Entity e) { w.add<T>(e, args...); }});
    return *this;
  }

  struct Step {
    ComponentId id;
    std::function<void(World&, Entity)> apply;
  };
  const std::vector<Step>& steps() const { return steps_; }
  const TagMask& mask() const { return mask_; }

  // Assinatura estavel: componentes ordenados + mascara de tags.
  std::string signature() const {
    std::vector<ComponentId> ids;
    for (auto& s : steps_) ids.push_back(s.id);
    std::sort(ids.begin(), ids.end());
    std::string sig;
    for (auto i : ids) sig += std::to_string(i) + ",";
    return sig + "|" + std::to_string(mask_.lo) + ":" + std::to_string(mask_.hi);
  }

private:
  std::vector<Step> steps_;
  TagMask mask_;
};

struct PoolPolicy {
  double maxIdleSeconds = 30.0;  // dormente sem uso por tanto tempo e apagado
  size_t maxPerSignature = 256;  // limite por assinatura
  size_t maxTotal = 4096;        // limite global
};

struct PoolStats {
  size_t dormant = 0, reused = 0, freed = 0;
};

class Scene;

class ToolObject {
public:
  ToolObject() = default;
  ToolObject(Scene* s, Entity e) : scene_(s), e_(e) {}

  bool valid() const;
  Entity entity() const { return e_; }
  template <class T> T* get() const;
  template <class T> bool has() const;
  void addTag(TagId t) const;
  bool hasTag(TagId t) const;
  void destroy();          // apaga de verdade
  void DeleteAndReuse();   // some agora, fica no pool para reuso

private:
  Scene* scene_ = nullptr;
  Entity e_;
};

class Scene {
public:
  World& world() { return world_; }
  TagRegistry& tags() { return tags_; }
  EventBus& events() { return events_; }
  PoolStats stats() const { return stats_; }
  void setPoolPolicy(PoolPolicy p) { policy_ = p; }
  // Relogio da cena (segundos), atualizado pelo laco principal.
  double clock() const { return clock_; }
  void setClock(double t) { clock_ = t; }

  TagId tag(std::string_view name) { return tags_.intern(name); }

  // Cria um objeto; reaproveita um dormente identico se existir.
  ToolObject spawn(const Blueprint& bp, double now = 0) {
    const std::string sig = bp.signature();
    auto it = pool_.find(sig);
    if (it != pool_.end() && !it->second.empty()) {
      Dormant d = it->second.back();
      it->second.pop_back();
      --dormantTotal_;
      Entity e = world_.renew(d.entity); // novo handle, o antigo continua invalido
      std::vector<ComponentId> keep;
      for (auto& s : bp.steps()) keep.push_back(s.id);
      world_.resetTo(e, keep, bp.mask());            // remove extras e restaura tags
      for (auto& s : bp.steps()) s.apply(world_, e); // restaura valores de criacao
      world_.setDormant(e, false);
      ++stats_.reused;
      sigOf_[e.index] = sig;
      (void)now;
      return {this, e};
    }
    Entity e = world_.create();
    for (auto& s : bp.steps()) s.apply(world_, e);
    for (TagId t = 0; t < kMaxTags; ++t)
      if (bp.mask().test(t)) world_.addTag(e, t);
    sigOf_[e.index] = sig;
    return {this, e};
  }

  void deleteAndReuse(ToolObject o, double now) {
    if (!o.valid()) {
      debug().report(diag::kStaleHandle, Severity::Warning, "DeleteAndReuse() com handle obsoleto");
      return;
    }
    Entity e = o.entity();
    auto sit = sigOf_.find(e.index);
    if (sit == sigOf_.end()) { // nao veio de um Blueprint: apaga normalmente
      world_.destroy(e);
      return;
    }
    Entity fresh = world_.renew(e); // invalida todos os handles antigos
    world_.setDormant(fresh, true);
    pool_[sit->second].push_back({fresh, now});
    ++dormantTotal_;
    ++stats_.dormant;
  }

  // Etapa de limpeza (chamar no fim do quadro): expira dormentes.
  void collectPool(double now) {
    for (auto& [sig, vec] : pool_) {
      for (size_t i = 0; i < vec.size();) {
        const bool idle = now - vec[i].since >= policy_.maxIdleSeconds;
        const bool overSig = vec.size() > policy_.maxPerSignature;
        if (idle || overSig) {
          freeDormant(vec[i]);
          vec.erase(vec.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
          ++i;
        }
      }
    }
    // limite global: apaga os mais antigos
    while (dormantTotal_ > policy_.maxTotal) {
      auto* oldestVec = static_cast<std::vector<Dormant>*>(nullptr);
      size_t oldestIdx = 0;
      double oldest = 1e300;
      for (auto& [sig, vec] : pool_)
        for (size_t i = 0; i < vec.size(); ++i)
          if (vec[i].since < oldest) {
            oldest = vec[i].since;
            oldestVec = &vec;
            oldestIdx = i;
          }
      if (!oldestVec) break;
      debug().report(diag::kPoolPressure, Severity::Warning, "limite total do pool excedido; apagando o mais antigo");
      freeDormant((*oldestVec)[oldestIdx]);
      oldestVec->erase(oldestVec->begin() + static_cast<std::ptrdiff_t>(oldestIdx));
    }
  }

  // Pressao de memoria: libera tudo que esta dormente.
  void flushPool() {
    for (auto& [sig, vec] : pool_) {
      for (auto& d : vec) freeDormant(d);
      vec.clear();
    }
  }

  size_t dormantCount() const { return dormantTotal_; }

private:
  struct Dormant {
    Entity entity;
    double since;
  };

  void freeDormant(const Dormant& d) {
    sigOf_.erase(d.entity.index);
    world_.setDormant(d.entity, false);
    world_.destroy(d.entity);
    --dormantTotal_;
    ++stats_.freed;
  }

  World world_;
  TagRegistry tags_;
  EventBus events_;
  PoolPolicy policy_;
  PoolStats stats_;
  std::map<std::string, std::vector<Dormant>> pool_;
  std::unordered_map<u32, std::string> sigOf_;
  size_t dormantTotal_ = 0;
  double clock_ = 0;
};

inline bool ToolObject::valid() const { return scene_ && scene_->world().alive(e_) && !scene_->world().isDormant(e_); }
template <class T> T* ToolObject::get() const { return valid() ? scene_->world().get<T>(e_) : nullptr; }
template <class T> bool ToolObject::has() const { return valid() && scene_->world().has<T>(e_); }
inline void ToolObject::addTag(TagId t) const {
  if (valid()) scene_->world().addTag(e_, t);
}
inline bool ToolObject::hasTag(TagId t) const { return valid() && scene_->world().hasTag(e_, t); }
inline void ToolObject::destroy() {
  if (valid()) scene_->world().destroy(e_);
}
inline void ToolObject::DeleteAndReuse() {
  if (scene_) scene_->deleteAndReuse(*this, scene_->clock());
}

} // namespace tool
