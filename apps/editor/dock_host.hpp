#pragma once

// Host do docking das janelas SpriteUI (main.cpp). O modelo e as regras estao em dock_layout.hpp. Aqui ficam:
//  - o registro das janelas acopláveis (o host fala com cada uma por funcoes);
//  - o layout padrao, montado quando o dock esta vazio;
//  - o arraste do grip: o DockDragController decide Dock, Tab ou Float ao soltar;
//  - a faixa de abas e as guias de encaixe, desenhadas por cima de tudo (overlay).
// Quando o grip e pressionado, o host consome o clique: a janela nao inicia o proprio arraste. Assim a janela
// flutuante so muda de lugar ao soltar (floatAt), e a acoplada continua no dock ate o encaixe.
//
// Ordem por quadro (main.cpp): beginFrame, desenho das janelas com visible(), overlay() por ultimo.
// Eventos do ponteiro: onEvent antes das janelas; quando devolve true, a janela nao recebe o evento.

#include "dock_layout.hpp"
#include "spriteui_host.hpp"
#include "view_painter.hpp"
#include "view_shapes.hpp"

#include "SpriteUi/Widgets/DockInteraction.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace editor::dock_host {

namespace SU = ToolEngine::SpriteUi;
namespace SW = ToolEngine::SpriteUi::Widgets;
namespace SR = ToolEngine::SpriteUi::Rendering;
namespace view = editor::view;

// Converte o ponto da SpriteUI no ponto dos desenhos de view_shapes (mesmo significado, tipos distintos).
inline view::Pixel px(SU::Vec2 v) { return view::Pixel{v.x, v.y}; }

// Janelas SpriteUI acopláveis. Os ids sao estaveis: o modelo de dock os guarda.
enum class Kind : std::uint32_t { Hierarchy, Scene, Game, Scenes, Assets, Console, Profiler };
inline SU::UIHandle idOf(Kind k) { return SU::UIHandle{0xE0000010u + static_cast<std::uint32_t>(k), 1u}; }

inline constexpr float kStripFont = 13.f;
inline constexpr float kStripPad = 8.f;

// O que o host precisa de cada janela. As funcoes sao preenchidas pelo main.cpp com os paineis ja criados.
struct Window {
  SU::UIHandle id{};
  std::function<std::string()> title;                         // nome na faixa de abas (traduzido ao desenhar)
  std::function<bool()> open;                                 // a flag do Editor (ed.showX)
  std::function<std::optional<SU::Rect>()> frame;             // moldura no ultimo quadro (nullopt: nao desenhada)
  std::function<SU::Rect()> grip;                             // grip no ultimo quadro
  std::function<void(std::optional<SU::Rect>)> setDock;       // area da folha, ou nullopt (flutuante)
  std::function<void(SU::Vec2)> floatAt;                      // sai do dock no ponto dado
};

class Host {
 public:
  Host(const sui::TextAtlasSet& text, sui::SpriteUiSdlRenderer& sdl) : painter_{text}, sdl_{sdl} {}
  Host(const Host&) = delete;
  Host& operator=(const Host&) = delete;

  void add(Window w) { windows_.push_back(std::move(w)); }

  // Inicio do quadro: area do dock (janela sem menu e sem barra de status) e sincronizacao das janelas.
  // Janela fechada sai do dock. Janela aberta fora do dock flutua. Janela aberta no dock recebe a area da folha
  // (so a aba ativa e desenhada; as outras ficam no dock).
  void beginFrame(SU::Rect area, int winW, int winH) {
    area_ = area;
    winW_ = static_cast<float>(winW);
    winH_ = static_cast<float>(winH);
    model_.setArea(area);
    if (model_.empty()) buildDefault();
    for (Window& w : windows_) {
      if (!w.open()) {
        if (model_.contains(w.id)) model_.remove(w.id);
        continue;
      }
      if (!model_.contains(w.id)) {
        w.setDock(std::nullopt);
        continue;
      }
      if (model_.active(w.id)) w.setDock(model_.contentRect(w.id));
    }
  }

  // A janela deve ser desenhada neste quadro: aberta e (flutuante ou a aba ativa da folha).
  [[nodiscard]] bool visible(SU::UIHandle id, bool open) const {
    if (!open) return false;
    return !model_.contains(id) || model_.active(id);
  }

  // Eventos do ponteiro, antes das janelas. True quando o host consumiu o clique (faixa de abas ou grip).
  bool onEvent(const SDL_Event& e) {
    switch (e.type) {
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e.button.button == SDL_BUTTON_LEFT) return pressed({e.button.x, e.button.y});
        return false;
      case SDL_EVENT_MOUSE_MOTION:
        pointer_ = {e.motion.x, e.motion.y};
        if (drag_.IsPressed()) drag_.Move(pointer_);
        return false;
      case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e.button.button == SDL_BUTTON_LEFT) return released({e.button.x, e.button.y});
        return false;
      default: return false;
    }
  }

  // Overlay do dock: faixas de abas, guia de encaixe durante o arraste e contorno da janela arrastada.
  // Desenhado depois de todas as janelas. False so em falha do SDL.
  bool overlay(SDL_Renderer* renderer) {
    view::ShapeList shapes;
    for (const SW::DockLeafRect& leaf : model_.leaves()) {
      const std::vector<SU::UIHandle> tabs = model_.tabsOf(leaf.leaf);
      if (tabs.size() < 2) continue;
      const float tabW = leaf.bounds.size.x / static_cast<float>(tabs.size());
      for (std::size_t i = 0; i < tabs.size(); ++i) {
        const float x = leaf.bounds.position.x + tabW * static_cast<float>(i);
        const float y = leaf.bounds.position.y;
        const bool on = tabs[i] == leaf.leaf;
        shapes.rectFilled({x, y}, {x + tabW, y + dock_layout::kTabStripHeight},
                          on ? view::Rgba(51, 92, 133) : view::Rgba(33, 41, 54));
        shapes.text({x + tabW * 0.5f, y + (dock_layout::kTabStripHeight - kStripFont) * 0.5f}, titleOf(tabs[i]),
                    kStripFont, view::Rgba(233, 234, 238), true, std::max(0.f, tabW - kStripPad));
      }
    }
    if (drag_.IsDragging()) {
      for (const SW::DockLeafRect& leaf : model_.leaves()) {
        if (!inside(leaf.bounds, pointer_)) continue;
        const SW::DockSlot slot = SW::ComputeDropSlot(leaf.bounds, pointer_, cfg_.edgeFraction);
        const SU::Rect hl = guideRect(leaf.bounds, slot);
        if (hl.size.x > 0.f && hl.size.y > 0.f) {
          shapes.rectFilled(px(hl.position), px({hl.position.x + hl.size.x, hl.position.y + hl.size.y}),
                            view::Rgba(74, 130, 200, 110));
        }
        break;
      }
      // Contorno da janela que se move: segue o ponteiro, com o mesmo ponto de pegada do grip.
      const SU::Vec2 a{pointer_.x - grabOffset_.x, pointer_.y - grabOffset_.y};
      shapes.rectFilled(px(a), px({a.x + grabSize_.x, a.y + grabSize_.y}), view::Rgba(74, 130, 200, 40));
    }
    if (shapes.shapes().empty()) return true;
    const std::vector<SR::ClipRectGPU> clips{{0.f, 0.f, winW_, winH_}};
    view::PaintList list;
    list.owner = sui::handleFor(0);
    painter_.append(shapes, 0u, list);
    return view::drawPaintList(renderer, sdl_, extractor_, packet_, frameId_, clips, list);
  }

  [[nodiscard]] const dock_layout::Model& model() const { return model_; }

 private:
  Window* find(SU::UIHandle id) {
    for (Window& w : windows_) {
      if (w.id == id) return &w;
    }
    return nullptr;
  }

  std::string titleOf(SU::UIHandle id) const {
    for (const Window& w : windows_) {
      if (w.id == id) return w.title ? w.title() : std::string{};
    }
    return "";
  }

  // Janela desenhada neste quadro (esta no dock como aba ativa, ou flutua). Grip de aba inativa nao e clicavel.
  [[nodiscard]] bool visibleNow(const Window& w) const { return !model_.contains(w.id) || model_.active(w.id); }

  static bool inside(SU::Rect r, SU::Vec2 p) {
    return p.x >= r.position.x && p.x < r.position.x + r.size.x && p.y >= r.position.y && p.y < r.position.y + r.size.y;
  }

  // Retangulo de uma zona de encaixe dentro da folha (borda = um quarto da folha; centro = miolo).
  static SU::Rect guideRect(SU::Rect leaf, SW::DockSlot slot) {
    const SU::Vec2 p = leaf.position;
    const SU::Vec2 s = leaf.size;
    switch (slot) {
      case SW::DockSlot::Left: return {p, {s.x * 0.25f, s.y}};
      case SW::DockSlot::Right: return {{p.x + s.x * 0.75f, p.y}, {s.x * 0.25f, s.y}};
      case SW::DockSlot::Top: return {p, {s.x, s.y * 0.25f}};
      case SW::DockSlot::Bottom: return {{p.x, p.y + s.y * 0.75f}, {s.x, s.y * 0.25f}};
      case SW::DockSlot::Center: return {{p.x + s.x * 0.25f, p.y + s.y * 0.25f}, {s.x * 0.5f, s.y * 0.5f}};
      default: return {};
    }
  }

  // Clique: faixa de abas troca a aba ativa; grip inicia o gesto de arraste. Devolve true se consumiu o clique.
  bool pressed(SU::Vec2 p) {
    pointer_ = p;
    for (const SW::DockLeafRect& leaf : model_.leaves()) {
      const std::vector<SU::UIHandle> tabs = model_.tabsOf(leaf.leaf);
      if (tabs.size() < 2) continue;
      const SU::Rect strip{leaf.bounds.position, {leaf.bounds.size.x, dock_layout::kTabStripHeight}};
      if (!inside(strip, p)) continue;
      const float tabW = strip.size.x / static_cast<float>(tabs.size());
      const auto index = static_cast<std::size_t>(
          std::clamp((p.x - strip.position.x) / tabW, 0.f, static_cast<float>(tabs.size() - 1)));
      model_.select(tabs[index]);
      return true;
    }
    for (Window& w : windows_) {
      if (!w.open() || !visibleNow(w)) continue;
      const std::optional<SU::Rect> f = w.frame();
      if (!f || !inside(w.grip(), p)) continue;
      drag_.BeginPress(w.id, p, model_.contains(w.id));
      activeWin_ = w.id;
      grabOffset_ = {p.x - f->position.x, p.y - f->position.y};
      grabSize_ = f->size;
      return true;
    }
    return false;
  }

  // Solta: o controlador escolhe Dock, Tab ou Float. Float tira a janela do dock e a deixa no ponto de soltura.
  // Devolve true quando havia um arraste (o clique de soltura nao vai para as janelas).
  bool released(SU::Vec2 p) {
    pointer_ = p;
    if (!drag_.IsPressed()) return false;
    const bool wasDragging = drag_.IsDragging();
    const SW::DockDropResult r = model_.release(drag_, p);   // encerra o gesto
    if (!wasDragging) return false;
    Window* w = find(activeWin_);
    if (!w) return true;
    const SU::Vec2 at{p.x - grabOffset_.x, p.y - grabOffset_.y};
    if (r.action == SW::DockAction::Float) {
      if (model_.contains(w->id)) model_.remove(w->id);
      w->floatAt(at);
    } else if (r.action == SW::DockAction::Dock || r.action == SW::DockAction::Tab) {
      if (r.targetLeaf == w->id) return true;               // soltou sobre si mesma: nada muda
      if (model_.contains(w->id)) model_.remove(w->id);     // sai da folha antiga antes de entrar na nova
      if (!model_.apply(w->id, r)) w->floatAt(at);          // alvo sumiu: a janela fica solta
    }
    return true;
  }

  // Layout padrao: Scene no centro; Hierarquia e Scenes a esquerda; Assets e Game a direita; Console e Profiler
  // embaixo. So entram as janelas abertas. Sem Scene aberta, o dock fica vazio e as janelas flutuam.
  void buildDefault() {
    auto has = [&](Kind k) {
      for (const Window& w : windows_) {
        if (w.id == idOf(k) && w.open()) return true;
      }
      return false;
    };
    if (!has(Kind::Scene)) return;
    model_.place(idOf(Kind::Scene));
    model_.setArea(area_);
    if (has(Kind::Hierarchy)) model_.split(idOf(Kind::Scene), SW::DockSlot::Left, dock_layout::kSideRatio, idOf(Kind::Hierarchy));
    if (has(Kind::Scenes) && has(Kind::Hierarchy)) model_.split(idOf(Kind::Hierarchy), SW::DockSlot::Bottom, 0.45f, idOf(Kind::Scenes));
    if (has(Kind::Assets)) model_.split(idOf(Kind::Scene), SW::DockSlot::Right, dock_layout::kSideRatio, idOf(Kind::Assets));
    if (has(Kind::Game)) model_.split(idOf(Kind::Scene), SW::DockSlot::Right, 0.5f, idOf(Kind::Game));
    if (has(Kind::Console)) model_.split(idOf(Kind::Scene), SW::DockSlot::Bottom, dock_layout::kBottomRatio, idOf(Kind::Console));
    if (has(Kind::Profiler) && has(Kind::Console)) model_.split(idOf(Kind::Console), SW::DockSlot::Right, 0.5f, idOf(Kind::Profiler));
    model_.setArea(area_);
  }

  dock_layout::Model model_;
  SW::DockInteractionConfig cfg_{};
  SW::DockDragController drag_{};
  SU::UIHandle activeWin_{};
  SU::Vec2 grabOffset_{};
  SU::Vec2 grabSize_{};
  SU::Vec2 pointer_{};
  SU::Rect area_{};
  float winW_ = 0.f, winH_ = 0.f;
  std::vector<Window> windows_;
  view::ShapePainter painter_;
  sui::SpriteUiSdlRenderer& sdl_;
  SR::RenderExtractor extractor_;
  SR::FramePacket packet_;
  std::uint64_t frameId_ = 0;
};

}  // namespace editor::dock_host
