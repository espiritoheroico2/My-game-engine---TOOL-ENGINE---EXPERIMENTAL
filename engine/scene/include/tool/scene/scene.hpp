// ToolEngine - Scene + ToolObject + DeleteAndReuse (pool de reaproveitamento).
#pragma once
#include "tool/ecs/world.hpp"
#include "tool/events/events.hpp"

#include <algorithm>
#include <functional>
#include <limits>
#include <numeric>
#include <string>
#include <unordered_map>
#include <vector>

namespace tool {

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

  std::string signature() const {
    std::vector<ComponentId> ids;
    ids.reserve(steps_.size());
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
  double maxIdleSeconds = 30.0;
  size_t maxPerSignature = 256;
  size_t maxTotal = 4096;
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
  template <class T, class... Args> T* add(Args&&... args) const;
  template <class T, class... Args> T* tryAdd(Args&&... args) const;
  template <class T> bool remove() const;
  template <class T> T* component() const;
  template <class T> T* module() const { return component<T>(); }
  void addTag(TagId t) const;
  bool hasTag(TagId t) const;
  void destroy();
  void recycle();
  void deleteAndReuse();
  void DeleteAndReuse();

private:
  Scene* scene_ = nullptr;
  Entity e_;
};

class Scene {
public:
  World& world() { return world_; }
  const World& world() const { return world_; }
  TagRegistry& tags() { return tags_; }
  EventBus& events() { return events_; }
  PoolStats stats() const { return stats_; }

  [[nodiscard]] ToolObject object(Entity entity) {
    return world_.alive(entity) ? ToolObject{this, entity} : ToolObject{};
  }
  void setPoolPolicy(PoolPolicy p) { policy_ = p; }
  double clock() const { return clock_; }
  void setClock(double t) { clock_ = t; }
  TagId tag(std::string_view name) { return tags_.intern(name); }

  ToolObject spawn(const Blueprint& bp, double now = 0) {
    const std::string sig = bp.signature();
    auto it = pool_.find(sig);
    if (it != pool_.end() && !it->second.empty()) {
      Dormant dormant = it->second.back();
      it->second.pop_back();
      --dormantCount_;

      Entity e = world_.renew(dormant.entity);
      keepScratch_.clear();
      keepScratch_.reserve(bp.steps().size());
      for (const auto& step : bp.steps()) keepScratch_.push_back(step.id);
      world_.resetTo(e, keepScratch_, bp.mask());
      for (const auto& step : bp.steps()) step.apply(world_, e);
      world_.setDormant(e, false);
      world_.setSimulating(e, true);
      ++stats_.reused;
      signatureByEntity_[e.index] = sig;
      (void)now;
      return {this, e};
    }

    Entity e = world_.create();
    for (const auto& step : bp.steps()) step.apply(world_, e);
    for (TagId tag = 0; tag < kMaxTags; ++tag)
      if (bp.mask().test(tag)) world_.addTag(e, tag);
    signatureByEntity_[e.index] = sig;
    return {this, e};
  }

  void recycle(ToolObject o, double now) {
    if (!o.valid()) {
      debug().report(diag::kStaleHandle, Severity::Warning, "recycle() com handle obsoleto");
      return;
    }

    const Entity e = o.entity();
    const auto sigIt = signatureByEntity_.find(e.index);
    if (sigIt == signatureByEntity_.end()) {
      destroy(o);
      return;
    }

    const Entity fresh = world_.renew(e);
    world_.setDormant(fresh, true);
    pool_[sigIt->second].push_back({fresh, now});
    ++dormantCount_;
    ++stats_.dormant;
  }

  void deleteAndReuse(ToolObject o, double now) { recycle(o, now); }

  void destroy(ToolObject o) {
    if (!o.valid()) return;
    const Entity e = o.entity();
    signatureByEntity_.erase(e.index);
    world_.destroy(e);
  }

  void collectPool(double now) {
    for (auto& [signature, entries] : pool_) {
      size_t write = 0;
      for (size_t read = 0; read < entries.size(); ++read) {
        if (now - entries[read].since >= policy_.maxIdleSeconds) {
          freeDormant(entries[read]);
        } else {
          if (write != read) entries[write] = entries[read];
          ++write;
        }
      }
      entries.resize(write);

      if (entries.size() <= policy_.maxPerSignature) continue;
      const size_t excess = entries.size() - policy_.maxPerSignature;
      orderScratch_.resize(entries.size());
      std::iota(orderScratch_.begin(), orderScratch_.end(), size_t{0});
      std::stable_sort(orderScratch_.begin(), orderScratch_.end(),
                       [&](size_t a, size_t b) { return entries[a].since < entries[b].since; });
      dropScratch_.assign(entries.size(), 0);
      for (size_t k = 0; k < excess; ++k) {
        dropScratch_[orderScratch_[k]] = 1;
        freeDormant(entries[orderScratch_[k]]);
      }
      write = 0;
      for (size_t read = 0; read < entries.size(); ++read) {
        if (dropScratch_[read]) continue;
        if (write != read) entries[write] = entries[read];
        ++write;
      }
      entries.resize(write);
    }

    while (dormantCount_ > policy_.maxTotal) {
      std::string oldestSignature;
      size_t oldestIndex = 0;
      double oldestTime = std::numeric_limits<double>::infinity();

      for (auto& [signature, entries] : pool_) {
        for (size_t i = 0; i < entries.size(); ++i) {
          if (entries[i].since < oldestTime) {
            oldestTime = entries[i].since;
            oldestSignature = signature;
            oldestIndex = i;
          }
        }
      }

      if (oldestSignature.empty()) break;
      debug().report(diag::kPoolPressure, Severity::Warning, "limite total do pool excedido; apagando o mais antigo");
      auto& entries = pool_[oldestSignature];
      freeDormant(entries[oldestIndex]);
      if (oldestIndex + 1 < entries.size()) {
        entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(oldestIndex));
      } else {
        entries.pop_back();
      }
    }
  }

  void flushPool() {
    for (auto& [signature, entries] : pool_) {
      for (const auto& dormant : entries) freeDormant(dormant);
      entries.clear();
    }
  }

  size_t dormantCount() const { return dormantCount_; }

private:
  struct Dormant {
    Entity entity;
    double since;
  };

  void freeDormant(const Dormant& d) {
    signatureByEntity_.erase(d.entity.index);
    world_.setDormant(d.entity, false);
    world_.destroy(d.entity);
    --dormantCount_;
    ++stats_.freed;
  }

  World world_;
  TagRegistry tags_;
  EventBus events_;
  PoolPolicy policy_;
  PoolStats stats_;
  std::unordered_map<std::string, std::vector<Dormant>> pool_;
  std::unordered_map<u32, std::string> signatureByEntity_;
  std::vector<ComponentId> keepScratch_;
  std::vector<size_t> orderScratch_;
  std::vector<u8> dropScratch_;
  size_t dormantCount_ = 0;
  double clock_ = 0;
};

inline bool ToolObject::valid() const { return scene_ && scene_->world().alive(e_) && !scene_->world().isDormant(e_); }
template <class T> T* ToolObject::get() const { return valid() ? scene_->world().get<T>(e_) : nullptr; }
template <class T> bool ToolObject::has() const { return valid() && scene_->world().has<T>(e_); }
template <class T, class... Args> T* ToolObject::add(Args&&... args) const {
  return valid() ? scene_->world().tryAdd<T>(e_, std::forward<Args>(args)...) : nullptr;
}
template <class T, class... Args> T* ToolObject::tryAdd(Args&&... args) const {
  return valid() ? scene_->world().tryAdd<T>(e_, std::forward<Args>(args)...) : nullptr;
}
template <class T> bool ToolObject::remove() const {
  if (!valid()) return false;
  if (!scene_->world().has<T>(e_)) return false;
  scene_->world().remove<T>(e_);
  return true;
}
template <class T> T* ToolObject::component() const { return get<T>(); }
inline void ToolObject::addTag(TagId t) const {
  if (valid()) scene_->world().addTag(e_, t);
}
inline bool ToolObject::hasTag(TagId t) const { return valid() && scene_->world().hasTag(e_, t); }
inline void ToolObject::destroy() {
  if (scene_) scene_->destroy(*this);
}
inline void ToolObject::recycle() {
  if (scene_) scene_->recycle(*this, scene_->clock());
}
inline void ToolObject::deleteAndReuse() { recycle(); }
inline void ToolObject::DeleteAndReuse() { recycle(); }

} // namespace tool