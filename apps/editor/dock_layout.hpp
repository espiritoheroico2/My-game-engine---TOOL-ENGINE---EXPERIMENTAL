#pragma once

// Docking das janelas do editor (SpriteUI). Modelo puro: uma arvore de UIDockNode (biblioteca SpriteUI) com as
// folhas, as abas e as regras de encaixe. Sem SDL e sem desenho: o host (main.cpp) desenha as molduras, a faixa de
// abas e as guias, e encaminha o arraste do grip para o DockDragController. Testado sem janela (Editor.DockLayout).
//
// Regras:
//  - Uma janela esta no dock (folha ou aba) ou fora dele (flutuante). Nunca nos dois.
//  - Folha com mais de uma janela mostra so a aba ativa; as abas ficam na propria barra da moldura (sem faixa nova).
//  - Solta na borda de uma folha divide a folha; solta no centro junta como aba; solta fora vira flutuante.
//  - Remover a ultima janela de um lado colapsa o pai (sem folhas vazias).
#include "SpriteUi/Core/Handle.hpp"
#include "SpriteUi/Core/Math.hpp"
#include "SpriteUi/Widgets/DockInteraction.hpp"
#include "SpriteUi/Widgets/DockOps.hpp"
#include "SpriteUi/Widgets/Docking.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace editor::dock_layout {

namespace SU = ToolEngine::SpriteUi;
namespace SW = ToolEngine::SpriteUi::Widgets;

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

  // Retangulo da folha onde a janela esta.
  [[nodiscard]] std::optional<SU::Rect> leafRect(SU::UIHandle w) const {
    const SW::UIDockNode* leaf = find(root_, w);
    if (!leaf) return std::nullopt;
    return leaf->bounds;
  }

  // Retangulo do conteudo da janela: a folha inteira (as abas ficam na barra da moldura).
  [[nodiscard]] std::optional<SU::Rect> contentRect(SU::UIHandle w) const {
    const SW::UIDockNode* leaf = find(root_, w);
    if (!leaf) return std::nullopt;
    return leaf->bounds;
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

  // Esvazia o dock (nenhuma janela). A area nao muda.
  void clear() { root_ = SW::UIDockNode(); }

  // Texto do layout (arvore de divisoes e folhas, com as abas na ordem). Dock vazio: "E".
  [[nodiscard]] std::string serialize() const {
    if (empty()) return "E";
    std::ostringstream out;
    write(root_, out);
    return out.str();
  }

  // Le um layout salvo. So aceita texto valido em que cada janela aparece uma vez e `known` a reconhece.
  // Falso: o dock fica como estava.
  bool deserialize(const std::string& text, const std::function<bool(SU::UIHandle)>& known) {
    if (text == "E") {
      clear();
      return true;
    }
    std::istringstream in(text);
    SW::UIDockNode fresh;
    std::vector<SU::UIHandle> seen;
    if (!read(in, fresh, known, seen)) return false;
    std::string extra;
    if (in >> extra) return false;
    root_ = std::move(fresh);
    root_.Layout(area_);
    return true;
  }

 private:
  static std::string handleText(SU::UIHandle h) {
    return std::to_string(h.index) + ":" + std::to_string(h.generation);
  }

  static SU::UIHandle parseHandle(const std::string& t) {
    const auto colon = t.find(':');
    if (colon == std::string::npos) return {};
    std::uint32_t index = 0, generation = 0;
    const char* end = t.data() + t.size();
    const auto a = std::from_chars(t.data(), t.data() + colon, index);
    const auto b = std::from_chars(t.data() + colon + 1, end, generation);
    if (a.ec != std::errc{} || b.ec != std::errc{} || a.ptr != t.data() + colon || b.ptr != end) return {};
    return SU::UIHandle{index, generation};
  }

  // Formato: "S H|V <razao> <primeiro> <segundo>" ou "L <n> <aba ativa> <janela>...".
  static void write(const SW::UIDockNode& n, std::ostringstream& out) {
    if (n.isSplit) {
      out << "S " << (n.splitDirection == SU::LayoutDirection::Row ? 'H' : 'V') << ' ' << n.splitRatio << ' ';
      write(*n.firstChild, out);
      out << ' ';
      write(*n.secondChild, out);
      return;
    }
    const auto& tabs = n.tabs.GetTabs();
    if (tabs.size() < 2) {
      out << "L 1 0 " << handleText(n.VisibleContent());
      return;
    }
    out << "L " << tabs.size() << ' ' << n.tabs.GetActiveTabIndex();
    for (const SW::UITab& t : tabs) out << ' ' << handleText(t.contentHandle);
  }

  static bool read(std::istringstream& in, SW::UIDockNode& n, const std::function<bool(SU::UIHandle)>& known,
                   std::vector<SU::UIHandle>& seen) {
    std::string tag;
    if (!(in >> tag)) return false;
    if (tag == "S") {
      std::string dir;
      float ratio = 0.f;
      if (!(in >> dir >> ratio) || (dir != "H" && dir != "V") || !std::isfinite(ratio)) return false;
      n.isSplit = true;
      n.splitDirection = dir == "H" ? SU::LayoutDirection::Row : SU::LayoutDirection::Column;
      n.splitRatio = ratio;
      n.firstChild = std::make_unique<SW::UIDockNode>();
      n.secondChild = std::make_unique<SW::UIDockNode>();
      return read(in, *n.firstChild, known, seen) && read(in, *n.secondChild, known, seen);
    }
    if (tag != "L") return false;
    std::size_t count = 0, sel = 0;
    if (!(in >> count >> sel) || count == 0) return false;
    std::vector<SU::UIHandle> ids;
    for (std::size_t i = 0; i < count; ++i) {
      std::string token;
      if (!(in >> token)) return false;
      const SU::UIHandle h = parseHandle(token);
      if (!h.IsValid() || !known(h) || std::find(seen.begin(), seen.end(), h) != seen.end()) return false;
      seen.push_back(h);
      ids.push_back(h);
    }
    if (count == 1) {
      n.content = ids[0];
      return true;
    }
    for (const SU::UIHandle& h : ids) {
      if (!n.tabs.AddTab("", h)) return false;
    }
    return n.tabs.SelectTab(sel);
  }

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
