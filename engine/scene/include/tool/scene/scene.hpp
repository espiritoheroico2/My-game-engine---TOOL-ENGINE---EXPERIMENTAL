// ToolEngine - Scene + ToolObject + DeleteAndReuse (pool de reaproveitamento).
//
// ToolObject = handle leve para uma entidade ECS.
// DeleteAndReuse(): o objeto some (dormente, handle antigo invalido); se for criado
// outro identico (mesma assinatura = tags + componentes) ele e reaproveitado.
// Dormentes sem uso expiram por tempo / limite por assinatura / limite total.
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

// Receita de criacao de um objeto. A assinatura identifica "objetos identicos".
class Blueprint {
public:
  // Adiciona a tag t a receita.
  Blueprint& tag(TagId t) {
    mask_.set(t);
    return *this;
  }
  // Adiciona um componente T (com os argumentos dados) a receita. Os argumentos sao copiados.
  template <class T, class... Args> Blueprint& with(Args... args) {
    steps_.push_back({componentId<T>(), [=](World& w, Entity e) { w.add<T>(e, args...); }});
    return *this;
  }

  // Um passo da receita: id do componente e a funcao que o cria na entidade.
  struct Step {
    ComponentId id;
    std::function<void(World&, Entity)> apply;
  };
  // Passos da receita, na ordem em que foram definidos.
  const std::vector<Step>& steps() const { return steps_; }
  // Mascara de tags da receita.
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

// Politica do pool de dormentes: tempo maximo ocioso, limite por assinatura e limite total.
struct PoolPolicy {
  double maxIdleSeconds = 30.0;  // dormente sem uso por tanto tempo e apagado
  size_t maxPerSignature = 256;  // limite por assinatura
  size_t maxTotal = 4096;        // limite global
};

// Contadores do pool: dormentes atuais, reaproveitados e liberados.
struct PoolStats {
  size_t dormant = 0, reused = 0, freed = 0;
};

class Scene;

// Wrapper NAO e proprietario da entidade: e um handle leve (cena + Entity). Copias do wrapper nao
// controlam a vida do objeto; destroy() apaga a entidade via Scene e as copias ficam obsoletas
// (valid() = false). Quem quer a limpeza completa (assinatura e pool) deve usar Scene::destroy.
class ToolObject {
public:
  // Cria um ToolObject vazio (invalido).
  ToolObject() = default;
  // Cria o objeto que aponta para a entidade e na cena dada.
  ToolObject(Scene* s, Entity e) : scene_(s), e_(e) {}

  // True se a cena existe, a entidade esta viva e nao esta dormente.
  bool valid() const;
  // Handle ECS da entidade.
  Entity entity() const { return e_; }
  // Ponteiro para o componente T, ou nullptr se o objeto for invalido ou nao tiver T.
  template <class T> T* get() const;
  // True se o objeto e valido e tem componente T.
  template <class T> bool has() const;
  // Adiciona um componente T ao objeto. Devolve ponteiro ao componente, ou nullptr se o objeto for invalido.
  template <class T, class... Args> T* add(Args&&... args) const;
  // Igual a add (nao lanca excecao).
  template <class T, class... Args> T* tryAdd(Args&&... args) const;
  // Remove o componente T. True se ele existia.
  template <class T> bool remove() const;
  // Ponteiro para o componente T (igual a get).
  template <class T> T* component() const;
  // Atalho de component<T>() para sistemas de modulo.
  template <class T> T* module() const { return component<T>(); }
  // Liga a tag t no objeto (se valido).
  void addTag(TagId t) const;
  // True se o objeto e valido e tem a tag t.
  bool hasTag(TagId t) const;
  void destroy();          // apaga de verdade
  void recycle();          // some agora, fica no pool para reuso
  void deleteAndReuse();   // alias compatibilidade (novo nome canonico)
  void DeleteAndReuse();   // alias legado (compatibilidade)

private:
  Scene* scene_ = nullptr;
  Entity e_;
};

// Cena: dona do mundo ECS, das tags, do barramento de eventos e do pool de objetos dormentes (DeleteAndReuse).
class Scene {
public:
  // Mundo ECS da cena.
  World& world() { return world_; }
  // Registro de nomes de tags da cena.
  TagRegistry& tags() { return tags_; }
  // Barramento de eventos da cena.
  EventBus& events() { return events_; }
  // Contadores atuais do pool.
  PoolStats stats() const { return stats_; }
  // Devolve o ToolObject da entidade, ou um objeto invalido se o handle ja nao estiver vivo.
  [[nodiscard]] ToolObject object(Entity entity) {
    return world_.alive(entity) ? ToolObject{this, entity} : ToolObject{};
  }
  // Troca a politica do pool de dormentes.
  void setPoolPolicy(PoolPolicy p) { policy_ = p; }
  // Relogio da cena (segundos), atualizado pelo laco principal.
  double clock() const { return clock_; }
  // Atualiza o relogio da cena (em segundos).
  void setClock(double t) { clock_ = t; }

  // Id da tag com o nome dado (criada se necessario).
  TagId tag(std::string_view name) { return tags_.intern(name); }

  // Cria um objeto; reaproveita um dormente identico se existir.
  // Ao reaproveitar, o estado de simulacao e restaurado para ativo.
  ToolObject spawn(const Blueprint& bp, double now = 0) {
    const std::string sig = bp.signature();
    auto it = pool_.find(sig);
    if (it != pool_.end() && !it->second.empty()) {
      Dormant dormant = it->second.back();
      it->second.pop_back();
      --dormantCount_;

      Entity e = world_.renew(dormant.entity); // novo handle, o antigo continua invalido
      std::vector<ComponentId> keep;
      keep.reserve(bp.steps().size());
      for (const auto& step : bp.steps()) keep.push_back(step.id);
      world_.resetTo(e, keep, bp.mask());            // remove extras e restaura tags
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

  // Manda o objeto para o pool de dormentes, com o instante now para a expiracao. Objetos sem receita sao destruidos. Handles antigos ficam invalidos.
  void recycle(ToolObject o, double now) {
    if (!o.valid()) {
      debug().report(diag::kStaleHandle, Severity::Warning, "recycle() com handle obsoleto");
      return;
    }

    const Entity e = o.entity();
    const auto sigIt = signatureByEntity_.find(e.index);
    if (sigIt == signatureByEntity_.end()) { // nao veio de um Blueprint: apaga normalmente
      destroy(o);
      return;
    }

    const Entity fresh = world_.renew(e); // invalida todos os handles antigos
    world_.setDormant(fresh, true);
    pool_[sigIt->second].push_back({fresh, now});
    ++dormantCount_;
    ++stats_.dormant;
  }

  // Alias de recycle (nome canonico).
  void deleteAndReuse(ToolObject o, double now) { recycle(o, now); }

  // F-13: ponto UNICO de destruicao real de um objeto vivo. Apaga a entidade (nao vai ao pool) e
  // remove a assinatura, para que um indice reaproveitado depois nao herde a assinatura antiga.
  // Dormentes nao sao handles validos (ToolObject::valid), entao nunca chegam aqui: quem limpa o
  // pool e collectPool/flushPool.
  void destroy(ToolObject o) {
    if (!o.valid()) return;
    const Entity e = o.entity();
    signatureByEntity_.erase(e.index);
    world_.destroy(e);
  }

  // Etapa de limpeza (chamar no fim do quadro): expira dormentes.
  // Etapa 1: expira dormentes ociosos por tempo.
  // Etapa 2: se ainda houver mais que maxPerSignature, libera SOMENTE o excedente,
  // começando pelos mais antigos (menor `since`). Os demais continuam reutilizaveis.
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
      std::vector<size_t> order(entries.size());
      std::iota(order.begin(), order.end(), size_t{0});
      std::stable_sort(order.begin(), order.end(),
                       [&](size_t a, size_t b) { return entries[a].since < entries[b].since; });
      std::vector<char> drop(entries.size(), 0);
      for (size_t k = 0; k < excess; ++k) {
        drop[order[k]] = 1;
        freeDormant(entries[order[k]]);
      }
      write = 0;
      for (size_t read = 0; read < entries.size(); ++read) {
        if (drop[read]) continue;
        if (write != read) entries[write] = entries[read];
        ++write;
      }
      entries.resize(write);
    }

    // limite global: apaga os mais antigos
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

  // Pressao de memoria: libera tudo que esta dormente.
  void flushPool() {
    for (auto& [signature, entries] : pool_) {
      for (const auto& dormant : entries) freeDormant(dormant);
      entries.clear();
    }
  }

  // Quantidade de objetos dormentes no pool.
  size_t dormantCount() const { return dormantCount_; }

private:
  // Entrada do pool: entidade dormente e o instante em que foi reciclada.
  struct Dormant {
    Entity entity;
    double since;
  };

  // Destroi de vez um dormente do pool (libera a entidade e a assinatura).
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
  size_t dormantCount_ = 0;
  double clock_ = 0;
};

// True se o objeto aponta para uma cena, a entidade esta viva e nao esta dormente.
inline bool ToolObject::valid() const { return scene_ && scene_->world().alive(e_) && !scene_->world().isDormant(e_); }
// Ponteiro para o componente T, ou nullptr se o objeto for invalido.
template <class T> T* ToolObject::get() const { return valid() ? scene_->world().get<T>(e_) : nullptr; }
// True se o objeto e valido e tem componente T.
template <class T> bool ToolObject::has() const { return valid() && scene_->world().has<T>(e_); }
// Adiciona um componente T (argumentos repassados). Devolve nullptr se o objeto for invalido.
template <class T, class... Args> T* ToolObject::add(Args&&... args) const {
  return valid() ? scene_->world().tryAdd<T>(e_, std::forward<Args>(args)...) : nullptr;
}
// Igual a add (nao lanca excecao).
template <class T, class... Args> T* ToolObject::tryAdd(Args&&... args) const {
  return valid() ? scene_->world().tryAdd<T>(e_, std::forward<Args>(args)...) : nullptr;
}
// Remove o componente T do objeto. True se ele existia.
template <class T> bool ToolObject::remove() const {
  if (!valid()) return false;
  if (!scene_->world().has<T>(e_)) return false;
  scene_->world().remove<T>(e_);
  return true;
}
// Ponteiro para o componente T (igual a get).
template <class T> T* ToolObject::component() const { return get<T>(); }
// Liga a tag t no objeto (se valido).
inline void ToolObject::addTag(TagId t) const {
  if (valid()) scene_->world().addTag(e_, t);
}
// True se o objeto e valido e tem a tag t.
inline bool ToolObject::hasTag(TagId t) const { return valid() && scene_->world().hasTag(e_, t); }
// Destroi a entidade de verdade, via a cena (limpa assinatura e nao vai ao pool).
inline void ToolObject::destroy() {
  if (scene_) scene_->destroy(*this);
}
// Manda o objeto para o pool usando o relogio da cena.
inline void ToolObject::recycle() {
  if (scene_) scene_->recycle(*this, scene_->clock());
}

// Alias de recycle (nome canonico).
inline void ToolObject::deleteAndReuse() {
  recycle();
}

// Alias legado de recycle (mantido por compatibilidade).
inline void ToolObject::DeleteAndReuse() {
  recycle();
}

} // namespace tool
