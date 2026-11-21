#pragma once

// Docking das janelas do editor (SpriteUI). Modelo puro: uma arvore de UIDockNode (biblioteca SpriteUI) com as
// folhas, as abas e as regras de encaixe. Sem SDL e sem desenho: o host (main.cpp) desenha as molduras, a faixa de
// abas e as guias, e encaminha o arraste do grip para o DockDragController. Testado sem janela (Editor.DockLayout).
//
// Regras:
//  - Uma janela esta no dock (folha ou aba) ou fora dele (flutuante). Nunca nos dois.
//  - Folha com mais de uma janela mostra so a aba ativa; o conteudo fica abaixo da faixa de abas (kTabStripHeight).
//  - Solta na borda de uma folha divide a folha; solta no centro junta como aba; solta fora vira flutuante.
//  - Remover a ultima janela de um lado colapsa o pai (sem folhas vazias).
#include "SpriteUi/Core/Handle.hpp"
#include "SpriteUi/Core/Math.hpp"
#include "SpriteUi/Widgets/DockInteraction.hpp"
#include "SpriteUi/Widgets/DockOps.hpp"
#include "SpriteUi/Widgets/Docking.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

namespace editor::dock_layout {

namespace SU = ToolEngine::SpriteUi;
namespace SW = ToolEngine::SpriteUi::Widgets;

inline constexpr float kTabStripHeight = 24.f;   // faixa de abas no topo de uma folha com mais de uma janela
inline constexpr float kSideRatio = 0.22f;       // lateral padrao (Hierarquia, Assets)
inline constexpr float kBottomRatio = 0.28f;     // faixa inferior padrao (Console, Profiler)

class Model {
 public:
  Model() = default;
  Model(const Model&) = delete;
  Model& operator=(const Model&) = delete;

  // Area do dock (a janela sem a barra de menu). Recalcula as folhas. Chame a cada quadro.
  void setArea(SU::Rect area) {
    area_ = area;
    root_.Layout(area);
  }
  [[nodiscard]] SU::Rect area() const { return area_; }

  [[nodiscard]] bool empty() const { return !root_.content.IsValid() && root_.tabs.TabCount() == 0 && !root_.isSplit; }

  // Janela no dock (folha ou aba). False se ela esta flutuando.
  [[nodiscard]] bool contains(SU::UIHandle w) const { return find(root_, w) != nullptr; }

  // Retangulo da folha onde a janela esta (inclui a faixa de abas quando ha mais de uma janela na folha).
  [[nodiscard]] std::optional<SU::Rect> leafRect(SU::UIHandle w) const {
    const SW::UIDockNode* leaf = find(root_, w);
    if (!leaf) return std::nullopt;
    return leaf->bounds;
  }

  // Retangulo do conteudo da janela: a folha, menos a faixa de abas quando a folha tem abas.
  [[nodiscard]] std::optional<SU::Rect> contentRect(SU::UIHandle w) const {
    const SW::UIDockNode* leaf = find(root_, w);
    if (!leaf) return std::nullopt;
    SU::Rect r = leaf->bounds;
    if (leaf->tabs.TabCount() > 1) {
      r.position.y += kTabStripHeight;
      r.size.y = std::max(0.f, r.size.y - kTabStripHeight);
    }
    return r;
  }

  // Janela visivel no dock: esta no dock e e o conteudo visivel da folha (a aba ativa, ou a unica janela).
  [[nodiscard]] bool active(SU::UIHandle w) const {
    const SW::UIDockNode* leaf = find(root_, w);
    return leaf && leaf->VisibleContent() == w;
  }

  // Abas da folha onde esta a janela, na ordem. Vazio se a janela esta solta ou a folha tem so ela.
  [[nodiscard]] std::vector<SU::UIHandle> tabsOf(SU::UIHandle w) const {
    std::vector<SU::UIHandle> out;
    const SW::UIDockNode* leaf = find(root_, w);
    if (!leaf || leaf->tabs.TabCount() < 2) return out;
    for (const SW::UITab& t : leaf->tabs.GetTabs()) out.push_back(t.contentHandle);
    return out;
  }

  // Torna a janela a aba ativa da folha dela. False se ela nao esta em aba.
  bool select(SU::UIHandle w) {
    SW::UIDockNode* leaf = find(root_, w);
    return leaf && leaf->tabs.SelectContent(w);
  }

  // Folhas com o conteudo visivel e o retangulo da folha: entrada do DockDragController.
  [[nodiscard]] std::vector<SW::DockLeafRect> leaves() const {
    std::vector<SW::DockLeafRect> out;
    collect(root_, out);
    return out;
  }

  // Monta o layout inicial: a primeira janela ocupa a raiz; as demais entram por divisao de uma folha existente.
  // Falso se a janela ja esta no dock ou o alvo nao existe.
  bool place(SU::UIHandle w) {
    if (contains(w) || !w.IsValid()) return false;
    if (empty()) {
      root_.content = w;
      return true;
    }
    return false;
  }

  // Divide a folha de `target` no lado `slot` e coloca `w` no novo filho. Usado pelo layout padrao.
  bool split(SU::UIHandle target, SW::DockSlot slot, float ratio, SU::UIHandle w) {
    if (contains(w)) return false;
    SW::UIDockNode* leaf = find(root_, target);
    if (!leaf) return false;
    return leaf->Split(slot, ratio, w);
  }

  // Aplica a acao de um arraste solto. Dock e Tab encaixam; Float tira a janela do dock.
  bool apply(SU::UIHandle w, const SW::DockDropResult& drop) {
    if (drop.action == SW::DockAction::Float) return remove(w);
    if (drop.action == SW::DockAction::None) return false;
    const bool ok = SW::ApplyDropResult(root_, w, drop, kSideRatio);
    if (ok) root_.Layout(area_);
    return ok;
  }

  // Tira a janela do dock e colapsa folhas vazias. False se ela nao estava no dock.
  bool remove(SU::UIHandle w) {
    const bool ok = SW::RemoveFromDock(root_, w);
    if (ok) root_.Layout(area_);
    return ok;
  }

  // Resultado do arraste solto no ponto `pointer`, usando as folhas atuais.
  [[nodiscard]] SW::DockDropResult release(SW::DockDragController& controller, SU::Vec2 pointer) const {
    const std::vector<SW::DockLeafRect> items = leaves();
    return controller.Release(pointer, items);
  }

  // Divisor de uma folha dividida: arrasta a posicao `offsetPx` a partir do inicio do no (pai do divisor).
  bool dragDivider(SU::UIHandle w, float offsetPx, float minSidePx) {
    SW::UIDockNode* leaf = find(root_, w);
    if (!leaf) return false;
    const bool ok = SW::DragSplitDivider(*leaf, offsetPx, minSidePx);
    if (ok) root_.Layout(area_);
    return ok;
  }

 private:
  // Procura a folha que tem a janela como conteudo unico ou como aba.
  static const SW::UIDockNode* find(const SW::UIDockNode& node, SU::UIHandle w) {
    if (!node.isSplit) {
      if (node.content == w || node.tabs.IndexOf(w)) return &node;
      return nullptr;
    }
    if (const SW::UIDockNode* a = find(*node.firstChild, w)) return a;
    return find(*node.secondChild, w);
  }
  static SW::UIDockNode* find(SW::UIDockNode& node, SU::UIHandle w) {
    return const_cast<SW::UIDockNode*>(find(static_cast<const SW::UIDockNode&>(node), w));
  }

  static void collect(const SW::UIDockNode& node, std::vector<SW::DockLeafRect>& out) {
    if (node.isSplit) {
      if (node.firstChild) collect(*node.firstChild, out);
      if (node.secondChild) collect(*node.secondChild, out);
      return;
    }
    const SU::UIHandle visible = node.VisibleContent();
    if (!visible.IsValid()) return;
    SW::DockLeafRect leaf;
    leaf.leaf = visible;
    leaf.bounds = node.bounds;
    out.push_back(leaf);
  }

  SW::UIDockNode root_;
  SU::Rect area_{};
};

}  // namespace editor::dock_layout
