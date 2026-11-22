#pragma once

// Adaptador host: representa elementos da SpriteUI no ECS da ToolEngine.
// O nucleo da SpriteUI continua independente de ECS; este registro liga entidades,
// componentes, layout e extracao de quadro sem guardar ponteiros do ECS na biblioteca.

#include "tool/ecs/world.hpp"

#include "SpriteUi/Layout/LayoutBridge.hpp"
#include "SpriteUi/Layout/LayoutTree.hpp"

#include <unordered_map>
#include <vector>

namespace editor::spriteui_ecs {

namespace SU = ToolEngine::SpriteUi;
namespace SR = ToolEngine::SpriteUi::Rendering;

struct UINode {
  SU::UIHandle handle{};
  SU::UIHandle parent{};
  tool::Entity ecsParent{};
};

struct UIRectTransform {
  SU::FlexItem style{};
  bool dirty = true;
};

struct UIVisualState {
  SU::ElementVisual visual{};
  bool dirty = true;
};

struct UIVisibility {
  bool visible = true;
  bool dirty = true;
};

class Registry {
 public:
  explicit Registry(tool::World& world) : world_{world} {}

  tool::Entity Create(
      SU::FlexItem style = {},
      tool::Entity ecsParent = {},
      SU::ElementVisual visual = {},
      bool visible = true) {
    SU::UIHandle parent = HandleOf(ecsParent);
    const SU::UIHandle handle = tree_.Create(parent, style);
    if (!handle.IsValid()) return {};

    tool::Entity entity = world_.create();
    world_.add<UINode>(entity, UINode{handle, parent, ecsParent});
    world_.add<UIRectTransform>(entity, UIRectTransform{style, false});
    world_.add<UIVisualState>(entity, UIVisualState{visual, true});
    world_.add<UIVisibility>(entity, UIVisibility{visible, true});
    entityToHandle_[entity.index] = handle;
    handleToEntity_[handle.Pack()] = entity;
    return entity;
  }

  bool Destroy(tool::Entity entity) {
    if (!world_.alive(entity)) return false;
    UINode* node = world_.get<UINode>(entity);
    if (!node) {
      return world_.destroy(entity);
    }

    std::vector<tool::Entity> subtree;
    CollectSubtree(node->handle, subtree);
    for (tool::Entity child : subtree) {
      if (UINode* childNode = world_.get<UINode>(child)) {
        bridge_.RemoveVisual(childNode->handle);
        entityToHandle_.erase(child.index);
        handleToEntity_.erase(childNode->handle.Pack());
      }
    }

    tree_.Remove(node->handle);
    for (tool::Entity child : subtree) {
      if (world_.alive(child)) world_.destroy(child);
    }
    return true;
  }

  [[nodiscard]] SU::UIHandle HandleOf(tool::Entity entity) const {
    if (!world_.alive(entity)) return SU::InvalidUIHandle;
    if (const UINode* node = world_.get<UINode>(entity)) return node->handle;
    auto it = entityToHandle_.find(entity.index);
    return it == entityToHandle_.end() ? SU::InvalidUIHandle : it->second;
  }

  [[nodiscard]] tool::Entity EntityOf(SU::UIHandle handle) const {
    auto it = handleToEntity_.find(handle.Pack());
    return it == handleToEntity_.end() ? tool::Entity{} : it->second;
  }

  bool SetStyle(tool::Entity entity, const SU::FlexItem& style) {
    if (!world_.alive(entity)) return false;
    UIRectTransform* transform = world_.get<UIRectTransform>(entity);
    UINode* node = world_.get<UINode>(entity);
    if (!transform || !node) return false;
    transform->style = style;
    transform->dirty = true;
    if (tree_.SetStyle(node->handle, style)) {
      transform->dirty = false;
      return true;
    }
    return false;
  }

  bool SetVisual(tool::Entity entity, const SU::ElementVisual& visual) {
    if (!world_.alive(entity)) return false;
    UIVisualState* state = world_.get<UIVisualState>(entity);
    UINode* node = world_.get<UINode>(entity);
    if (!state || !node) return false;
    state->visual = visual;
    bridge_.SetVisual(node->handle, visual);
    state->dirty = false;
    return true;
  }

  bool SetVisible(tool::Entity entity, bool visible) {
    if (!world_.alive(entity)) return false;
    UIVisibility* state = world_.get<UIVisibility>(entity);
    UINode* node = world_.get<UINode>(entity);
    if (!state || !node) return false;
    state->visible = visible;
    state->dirty = true;
    if (tree_.SetVisible(node->handle, visible)) {
      state->dirty = false;
      return true;
    }
    return false;
  }

  void SyncDirty() {
    world_.forEachDense<UIRectTransform>([&](tool::Entity entity, UIRectTransform& transform) {
      if (!transform.dirty) return;
      if (UINode* node = world_.get<UINode>(entity)) {
        transform.dirty = !tree_.SetStyle(node->handle, transform.style);
      }
    });
    world_.forEachDense<UIVisualState>([&](tool::Entity entity, UIVisualState& state) {
      if (!state.dirty) return;
      if (UINode* node = world_.get<UINode>(entity)) {
        bridge_.SetVisual(node->handle, state.visual);
        state.dirty = false;
      }
    });
    world_.forEachDense<UIVisibility>([&](tool::Entity entity, UIVisibility& state) {
      if (!state.dirty) return;
      if (UINode* node = world_.get<UINode>(entity)) {
        state.dirty = !tree_.SetVisible(node->handle, state.visible);
      }
    });
  }

  void Layout(const SU::Rect& viewport, SU::LayoutStats* stats = nullptr) {
    SyncDirty();
    tree_.Layout(viewport, stats);
  }

  // Remove no SpriteUI e o visual cuja entidade ECS foi destruida diretamente,
  // sem passar pelo Registry. Chamado pelo host quando houver reuso/pooling.
  void PruneDestroyed() {
    std::vector<std::pair<tool::Entity, SU::UIHandle>> stale;
    stale.reserve(handleToEntity_.size());
    for (const auto& [packed, entity] : handleToEntity_) {
      if (!world_.alive(entity)) stale.push_back({entity, SU::UIHandle::Unpack(packed)});
    }

    for (const auto& [entity, handle] : stale) {
      bridge_.RemoveVisual(handle);
      tree_.Remove(handle);
      entityToHandle_.erase(entity.index);
      handleToEntity_.erase(handle.Pack());
    }
  }

  [[nodiscard]] SU::SceneExtractionResult Extract() const {
    return bridge_.Extract(tree_);
  }

  [[nodiscard]] SU::LayoutTree& Tree() noexcept { return tree_; }
  [[nodiscard]] const SU::LayoutTree& Tree() const noexcept { return tree_; }
  [[nodiscard]] std::size_t Count() const { return world_.activeCount<UINode>(); }

 private:
  void CollectSubtree(SU::UIHandle handle, std::vector<tool::Entity>& out) const {
    auto it = handleToEntity_.find(handle.Pack());
    if (it != handleToEntity_.end()) out.push_back(it->second);
    const SU::LayoutElement* element = tree_.Get(handle);
    if (!element) return;
    for (SU::UIHandle child : element->children) CollectSubtree(child, out);
  }

  tool::World& world_;
  SU::LayoutTree tree_;
  SU::LayoutSceneBridge bridge_;
  std::unordered_map<std::uint32_t, SU::UIHandle> entityToHandle_;
  std::unordered_map<std::uint64_t, tool::Entity> handleToEntity_;
};

} // namespace editor::spriteui_ecs
