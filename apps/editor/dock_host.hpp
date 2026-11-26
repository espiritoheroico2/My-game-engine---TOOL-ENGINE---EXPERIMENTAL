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
#include "resize_hover.hpp"
#include "spriteui_host.hpp"
#include "view_painter.hpp"
#include "view_shapes.hpp"
#include "window_frame.hpp"

#include "SpriteUi/Widgets/DockInteraction.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iterator>
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
enum class Kind : std::uint32_t { Hierarchy, Scene, Game, Scenes, Assets, Console, Profiler, Inspector, Settings, Coding, Narrative };
inline SU::UIHandle idOf(Kind k) { return SU::UIHandle{0xE0000010u + static_cast<std::uint32_t>(k), 1u}; }

inline constexpr float kStripFont = 12.f;
inline constexpr float kStripPad = 8.f;
// Abas na barra da moldura (mesma altura do cabecalho): depois do grip e do "?" ate antes de minimizar e X.
inline constexpr float kHeaderH = 30.f;
inline constexpr float kHelpReserve = 30.f;   // grip + "?" (quando nao ha grip medido)
inline constexpr float kCloseReserve = 8.f;   // margem a direita da barra agrupada
inline constexpr float kTabX = 18.f;          // botao X de cada aba (fecha a janela da aba)
inline constexpr float kTabMaxWidth = 120.f;
inline constexpr float kTabPadX = 6.f;       // folga horizontal interna de cada aba (grip e X ficam afastados da borda)
inline constexpr float kTabGripW = 14.f;     // grip de cada aba (seis pontos), antes do nome
inline constexpr float kTabEdge = 4.f;       // folga da barra de abas a esquerda (ela ocupa o cabecalho inteiro)
inline constexpr float kEdgeHighlightW = 4.f;   // espessura do destaque da borda sob o mouse (ou redimensionada)

// O que o host precisa de cada janela. As funcoes sao preenchidas pelo main.cpp com os paineis ja criados.
struct Window {
  SU::UIHandle id{};
  std::function<std::string()> title;                         // nome na faixa de abas (traduzido ao desenhar)
  std::function<bool()> open;                                 // a flag do Editor (ed.showX)
  std::function<void()> close;                                // fecha a janela (flag do Editor = false)
  std::function<std::optional<SU::Rect>()> frame;             // moldura no ultimo quadro (nullopt: nao desenhada)
  std::function<SU::Rect()> grip;                             // grip no ultimo quadro
  std::function<void(std::optional<SU::Rect>)> setDock;       // area da folha, ou nullopt (flutuante)
  std::function<void(SU::Vec2)> floatAt;                      // sai do dock no ponto dado
  std::function<window_frame::Frame*()> frameState;           // moldura: redimensiona pelas bordas (nullptr: nao)
  bool wasOpen = false;                                       // estava aberta no quadro anterior (host)
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
    applyPendingGestures();
    for (Window& w : windows_) {
      if (!w.open()) {
        if (model_.contains(w.id)) model_.remove(w.id);
        w.wasOpen = false;
        continue;
      }
      // Janela que acabou de abrir entra no grupo de abas (nao flutua por cima das outras). Flutuar e escolha do
      // usuario, feita arrastando; uma janela que ja estava aberta e fora do dock continua onde esta.
      if (!w.wasOpen && !model_.contains(w.id)) model_.attach(w.id, lastActive_);
      w.wasOpen = true;
      if (!model_.contains(w.id)) {
        w.setDock(std::nullopt);
        continue;
      }
      if (model_.active(w.id)) w.setDock(model_.contentRect(w.id));
    }
  }

  // A janela deve ser desenhada neste quadro: aberta e (flutuante ou a aba ativa da folha).
  // Janela solta (fora do dock). A ordem de desenho usa isso: acopladas primeiro, soltas por ultimo.
  [[nodiscard]] bool floating(SU::UIHandle id) const { return !model_.contains(id); }

  [[nodiscard]] bool visible(SU::UIHandle id, bool open) const {
    if (!open) return false;
    return !model_.contains(id) || model_.active(id);
  }

  // Eventos do ponteiro, antes das janelas. True quando o host consumiu o clique (faixa de abas ou grip).
  // Dono unico do ponteiro para as janelas: arraste, redimensionamento, divisorias, cursor e destaque da borda.
  // Movimento so guarda a posicao; o redimensionamento e aplicado uma vez por quadro (applyPendingGestures).
  bool onEvent(const SDL_Event& e) {
    switch (e.type) {
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e.button.button == SDL_BUTTON_LEFT) return pressed({e.button.x, e.button.y});
        return false;
      case SDL_EVENT_MOUSE_MOTION:
        pointer_ = {e.motion.x, e.motion.y};
        // Sem botao apertado, um gesto em andamento ficou sem soltura (ex.: soltou fora da janela): encerra agora.
        if (gestureActive() && e.motion.state == 0) {
          cancelGestures(pointer_);
          return false;
        }
        if (drag_.IsPressed()) drag_.Move(pointer_);
        if (resizing_) pendingResize_ = true;
        if (divider_) pendingDivider_ = true;
        if (!gestureActive()) {
          updateHover();
          return false;
        }
        return true;   // gesto do host em andamento: o movimento nao chega aos paineis
      case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e.button.button == SDL_BUTTON_LEFT) return released({e.button.x, e.button.y});
        return false;
      case SDL_EVENT_WINDOW_FOCUS_LOST:
        cancelGestures(pointer_);
        return false;
      default: return false;
    }
  }

  // Abas da folha dentro da barra da moldura da janela ativa: faixa entre o "?" e os botoes da direita.
  // Nao cria barra nova. Nullopt se a folha tem uma so janela.
  struct TabCells {
    float x0, x1, tabW;
  };
  [[nodiscard]] std::optional<TabCells> tabCells(const SW::DockLeafRect& leaf) const {
    const std::size_t n = model_.tabsOf(leaf.leaf).size();
    if (n < 2) return std::nullopt;
    const float right = leaf.bounds.position.x + leaf.bounds.size.x - kCloseReserve;
    const float left = leaf.bounds.position.x + kTabEdge;
    const float tabW = std::min(kTabMaxWidth, std::max(0.f, right - left) / static_cast<float>(n));
    return TabCells{left, left + tabW * static_cast<float>(n), tabW};
  }

  // Overlay do dock: abas na barra da moldura, guia de encaixe durante o arraste e contorno da janela arrastada.
  // Desenhado depois de todas as janelas. False so em falha do SDL.
  bool overlay(SDL_Renderer* renderer) {
    view::ShapeList shapes;
    for (const SW::DockLeafRect& leaf : model_.leaves()) {
      const std::optional<TabCells> c = tabCells(leaf);
      if (!c) continue;
      const std::vector<SU::UIHandle> tabs = model_.tabsOf(leaf.leaf);
      const float y = leaf.bounds.position.y;
      // Limpa o nome da janela ativa na barra e pinta as abas no lugar dele.
      shapes.rectFilled({leaf.bounds.position.x, y}, {leaf.bounds.position.x + leaf.bounds.size.x - kCloseReserve, y + kHeaderH},
                        view::Rgba(31, 41, 59));
      // O grip do cabecalho da janela ativa deixa de valer aqui: o desenho dele e apagado e cada aba tem o seu.
      for (const Window& w : windows_) {
        if (!(w.id == leaf.leaf) || !w.grip) continue;
        const SU::Rect g = w.grip();
        if (g.size.x > 0.f) shapes.rectFilled(px(g.position), px({g.position.x + g.size.x, g.position.y + g.size.y}), view::Rgba(31, 41, 59));
      }
      for (std::size_t i = 0; i < tabs.size(); ++i) {
        const float x = c->x0 + c->tabW * static_cast<float>(i);
        const bool on = tabs[i] == leaf.leaf;
        shapes.rectFilled({x, y}, {x + c->tabW - 1.f, y + kHeaderH},
                          on ? view::Rgba(52, 68, 96) : view::Rgba(24, 31, 46));
        if (on) shapes.rectFilled({x, y + kHeaderH - 2.f}, {x + c->tabW - 1.f, y + kHeaderH}, view::Rgba(74, 130, 200));
        // Grip da aba: seis pontos (duas colunas, tres linhas), com folga de kTabPadX da borda da aba.
        const float gx = x + kTabPadX, gy = y + (kHeaderH - 12.f) * 0.5f;
        for (int col = 0; col < 2; ++col) {
          for (int row = 0; row < 3; ++row) {
            const float dx = gx + static_cast<float>(col) * 4.f, dy = gy + static_cast<float>(row) * 4.f;
            shapes.rectFilled({dx, dy}, {dx + 2.f, dy + 2.f}, view::Rgba(120, 132, 150));
          }
        }
        // Nome alinhado a esquerda, depois do grip, e X centrado na sua faixa a direita; os dois no centro vertical da aba.
        const float midY = y + kHeaderH * 0.5f;
        const float nameX = x + kTabPadX + kTabGripW;
        const float nameW = std::max(0.f, c->tabW - (nameX - x) - kTabX - kTabPadX);
        shapes.textMiddle({nameX, midY}, titleOf(tabs[i]), kStripFont, view::Rgba(233, 234, 238), false, nameW);
        shapes.textMiddle({x + c->tabW - kTabPadX - kTabX * 0.5f, midY}, "X", kStripFont, view::Rgba(170, 178, 194), true, kTabX);
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
    addResizeHighlight(shapes);
    if (shapes.shapes().empty()) return true;
    const std::vector<SR::ClipRectGPU> clips{{0.f, 0.f, winW_, winH_}};
    view::PaintList list;
    list.owner = sui::handleFor(0);
    painter_.append(shapes, 0u, list);
    return view::drawPaintList(renderer, sdl_, extractor_, packet_, frameId_, clips, list);
  }

  [[nodiscard]] const dock_layout::Model& model() const { return model_; }

  // Volta ao layout padrao (menu Janela > Restaurar layout). Vale no proximo quadro.
  void resetDefault() {
    model_.clear();
    buildDefault();
  }

  // Salva a organizacao atual (abas, divisoes e tamanhos). False se o arquivo nao pode ser escrito.
  [[nodiscard]] bool save(const std::string& path) const {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << model_.serialize();
    return static_cast<bool>(f);
  }

  // Carrega uma organizacao salva. False se o arquivo nao existe ou nao e valido (o dock fica como estava).
  bool load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return model_.deserialize(text, [this](SU::UIHandle h) { return isRegistered(h); });
  }

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

  // Z-order: a camada do dock fica embaixo; janelas flutuantes ficam por cima dela, e entre elas a tocada por ultimo
  // fica no topo. A busca de grip e de borda segue essa ordem, para pegar a janela que o usuario ve.
  [[nodiscard]] std::vector<Window*> hitOrder() {
    std::vector<Window*> order;
    order.reserve(windows_.size());
    for (Window& w : windows_) order.push_back(&w);
    auto rank = [this](const Window* w) -> std::size_t {
      const auto it = std::find(raised_.begin(), raised_.end(), w->id);
      return it == raised_.end() ? 0 : static_cast<std::size_t>(it - raised_.begin()) + 1;
    };
    std::stable_sort(order.begin(), order.end(), [&](const Window* a, const Window* b) {
      const bool floatA = !model_.contains(a->id), floatB = !model_.contains(b->id);
      if (floatA != floatB) return floatA;   // flutuante fica por cima do dock
      return rank(a) > rank(b);              // entre iguais, a tocada por ultimo primeiro
    });
    return order;
  }

  // Marca a janela como a tocada por ultimo (topo do seu grupo de z-order).
  void raise(SU::UIHandle id) {
    raised_.erase(std::remove(raised_.begin(), raised_.end(), id), raised_.end());
    raised_.push_back(id);
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
    // Cada aba tem o seu grip: arrastar por ele tira so aquela aba do grupo.
    for (const SW::DockLeafRect& leaf : model_.leaves()) {
      const std::vector<SU::UIHandle> tabs = model_.tabsOf(leaf.leaf);
      const std::optional<TabCells> c = tabCells(leaf);
      if (!c) continue;
      for (std::size_t i = 0; i < tabs.size(); ++i) {
        const float x = c->x0 + c->tabW * static_cast<float>(i);
        if (!inside(tabGripRect(leaf, x), p)) continue;
        drag_.BeginPress(tabs[i], p, true);
        activeWin_ = tabs[i];
        lastActive_ = tabs[i];
        raise(tabs[i]);
        grabOffset_ = {p.x - leaf.bounds.position.x, p.y - leaf.bounds.position.y};
        grabSize_ = leaf.bounds.size;
        return true;
      }
    }
    for (const SW::DockLeafRect& leaf : model_.leaves()) {
      const std::optional<TabCells> c = tabCells(leaf);
      if (!c) continue;
      const SU::Rect strip{{c->x0, leaf.bounds.position.y}, {c->x1 - c->x0, kHeaderH}};
      if (!inside(strip, p)) continue;
      const std::vector<SU::UIHandle> tabs = model_.tabsOf(leaf.leaf);
      const auto index = static_cast<std::size_t>(
          std::clamp((p.x - c->x0) / c->tabW, 0.f, static_cast<float>(tabs.size() - 1)));
      const float localX = p.x - (c->x0 + c->tabW * static_cast<float>(index));
      if (localX >= c->tabW - kTabX) {   // X da aba: fecha a janela dela
        for (Window& w : windows_) {
          if (w.id == tabs[index] && w.close) { w.close(); return true; }
        }
      }
      model_.select(tabs[index]);
      lastActive_ = tabs[index];
      raise(tabs[index]);
      return true;
    }
    // Bordas de janela flutuante (vem antes de tudo que esta embaixo dela), de cima para baixo.
    for (Window* wp : hitOrder()) {
      Window& w = *wp;
      if (!w.open() || !visibleNow(w) || model_.contains(w.id)) continue;
      window_frame::Frame* state = w.frameState ? w.frameState() : nullptr;
      if (state && state->beginResize(p)) {
        resizeMask_ = state->edgeAt(p);
        resizing_ = true;
        resizeWin_ = w.id;
        activeWin_ = w.id;
        lastActive_ = w.id;
        raise(w.id);
        return true;
      }
    }
    // Divisoria entre folhas do dock.
    if (SW::UIDockNode* d = model_.dividerAt(p)) {
      divider_ = d;
      return true;
    }
    for (Window* wp : hitOrder()) {
      Window& w = *wp;
      if (!w.open() || !visibleNow(w)) continue;
      const std::optional<SU::Rect> f = w.frame();
      if (!f || inGroup(w.id) || !inside(w.grip(), p)) continue;   // grip de aba agrupada fica oculto
      drag_.BeginPress(w.id, p, model_.contains(w.id));
      activeWin_ = w.id;
      lastActive_ = w.id;
      raise(w.id);
      grabOffset_ = {p.x - f->position.x, p.y - f->position.y};
      grabSize_ = f->size;
      return true;
    }
    return false;
  }

  // Area do grip de uma aba (a faixa inteira da aba responde ao grip, para ser facil de acertar).
  [[nodiscard]] SU::Rect tabGripRect(const SW::DockLeafRect& leaf, float x) const {
    return {{x, leaf.bounds.position.y}, {kTabGripW, kHeaderH}};
  }

  // A janela esta numa folha com duas ou mais abas: o grip do cabecalho dela nao vale (o da aba vale).
  [[nodiscard]] bool inGroup(SU::UIHandle id) const {
    if (!model_.contains(id)) return false;
    for (const SW::DockLeafRect& leaf : model_.leaves()) {
      const std::vector<SU::UIHandle> tabs = model_.tabsOf(leaf.leaf);
      if (tabs.size() >= 2 && std::find(tabs.begin(), tabs.end(), id) != tabs.end()) return true;
    }
    return false;
  }

  // Solta: o controlador escolhe Dock, Tab ou Float. Float tira a janela do dock e a deixa no ponto de soltura.
  // Devolve true quando havia um arraste (o clique de soltura nao vai para as janelas).
  bool released(SU::Vec2 p) {
    pointer_ = p;
    if (resizing_) {
      if (Window* w = find(resizeWin_)) {
        if (window_frame::Frame* f = w->frameState ? w->frameState() : nullptr) f->pointerUp();
      }
      resizing_ = false;
      pendingResize_ = false;   // o movimento ja acumulado nao pode ser aplicado depois da soltura
      return true;
    }
    if (divider_) {
      divider_ = nullptr;
      pendingDivider_ = false;
      return true;
    }
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

  bool gestureActive() const { return drag_.IsPressed() || resizing_ || divider_ != nullptr; }

  // Aplica no quadro o movimento acumulado: a moldura redimensionada e a divisoria movida.
  void applyPendingGestures() {
    if (pendingResize_) {
      pendingResize_ = false;
      if (Window* w = find(resizeWin_)) {
        if (window_frame::Frame* f = w->frameState ? w->frameState() : nullptr) f->pointerMove(pointer_);
      }
    }
    if (pendingDivider_) {
      pendingDivider_ = false;
      if (divider_) model_.moveDivider(divider_, pointer_);
    }
  }

  // Encerra qualquer gesto de redimensionamento. Solta todas as molduras, para nenhuma continuar seguindo o mouse.
  void endGestures() {
    for (Window& w : windows_) {
      if (window_frame::Frame* f = w.frameState ? w.frameState() : nullptr) f->pointerUp();
    }
    resizing_ = false;
    pendingResize_ = false;
    pendingDivider_ = false;
    divider_ = nullptr;
  }

  // Gesto interrompido (soltura perdida ou janela sem foco): termina o arraste na posicao atual e solta os gestos.
  void cancelGestures(SU::Vec2 p) {
    pointer_ = p;
    if (drag_.IsPressed()) (void)released(p);
    endGestures();
  }

  // Cursor e destaque sob o mouse, sem gesto: divisoria do dock ou borda de janela flutuante.
  void updateHover() {
    hoverWin_ = {};
    hoverMask_ = 0;
    hoverDivider_ = nullptr;
    resize_hover::CursorShape shape = resize_hover::CursorShape::Arrow;
    if (SW::UIDockNode* d = model_.dividerAt(pointer_)) {
      hoverDivider_ = d;
      shape = resize_hover::DividerCursor(d->splitDirection == SU::LayoutDirection::Row);
    } else {
      for (Window& w : windows_) {
        if (!w.open() || !visibleNow(w) || model_.contains(w.id)) continue;
        window_frame::Frame* f = w.frameState ? w.frameState() : nullptr;
        const int m = f ? f->edgeAt(pointer_) : 0;
        if (m == 0) continue;
        hoverWin_ = w.id;
        hoverMask_ = m;
        shape = resize_hover::EdgeCursor(m);
        break;
      }
    }
    setCursor(shape);
  }

  // Troca o cursor do SDL so quando a forma muda (cache dos cursores de sistema).
  void setCursor(resize_hover::CursorShape shape) {
    if (shape == cursor_) return;
    cursor_ = shape;
    static SDL_Cursor* cache[5] = {};
    const auto i = static_cast<std::size_t>(shape);
    static constexpr SDL_SystemCursor kSys[5] = {SDL_SYSTEM_CURSOR_DEFAULT, SDL_SYSTEM_CURSOR_EW_RESIZE, SDL_SYSTEM_CURSOR_NS_RESIZE,
                                                 SDL_SYSTEM_CURSOR_NWSE_RESIZE, SDL_SYSTEM_CURSOR_NESW_RESIZE};
    if (!cache[i]) cache[i] = SDL_CreateSystemCursor(kSys[i]);
    if (cache[i]) SDL_SetCursor(cache[i]);
  }

  // Destaque: a borda sob o mouse (ou a que esta sendo redimensionada) e a divisoria sob o mouse (ou a movida)
  // ficam com uma faixa mais grossa e brilhante.
  void addResizeHighlight(view::ShapeList& shapes) {
    const view::Color bright = view::Rgba(120, 170, 235, 235);
    if (resizing_) {
      if (Window* w = find(resizeWin_)) {
        if (const std::optional<SU::Rect> f = w->frame()) {
          const resize_hover::Highlight h = resize_hover::EdgeHighlight(*f, resizeMask_, kEdgeHighlightW);
          for (std::size_t i = 0; i < h.count; ++i) shapes.rectFilled(px(h.rects[i].position), px({h.rects[i].position.x + h.rects[i].size.x, h.rects[i].position.y + h.rects[i].size.y}), bright);
        }
      }
    } else if (hoverMask_ != 0 && !gestureActive()) {
      if (Window* w = find(hoverWin_)) {
        if (const std::optional<SU::Rect> f = w->frame()) {
          const resize_hover::Highlight h = resize_hover::EdgeHighlight(*f, hoverMask_, kEdgeHighlightW);
          for (std::size_t i = 0; i < h.count; ++i) shapes.rectFilled(px(h.rects[i].position), px({h.rects[i].position.x + h.rects[i].size.x, h.rects[i].position.y + h.rects[i].size.y}), bright);
        }
      }
    }
    SW::UIDockNode* d = divider_ ? divider_ : (gestureActive() ? nullptr : hoverDivider_);
    if (d && d->isSplit) {
      const bool row = d->splitDirection == SU::LayoutDirection::Row;
      const float r = std::clamp(d->splitRatio, 0.05f, 0.95f);
      const float at = row ? d->bounds.position.x + d->bounds.size.x * r : d->bounds.position.y + d->bounds.size.y * r;
      const SU::Rect hl = resize_hover::DividerHighlight(d->bounds, row, at, kEdgeHighlightW);
      shapes.rectFilled(px(hl.position), px({hl.position.x + hl.size.x, hl.position.y + hl.size.y}), bright);
    }
  }

  // Layout padrao: Hierarquia (em cima) e Scenes (embaixo) a esquerda; Scene e Game em abas no centro, em cima;
  // Console, Profiler e Assets em abas embaixo do centro; Inspector (em cima) e Settings (embaixo) a direita.
  // So entram as janelas registradas e abertas. Sem Scene aberta, o dock fica vazio e as janelas flutuam.
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
    if (has(Kind::Inspector)) {
      model_.split(idOf(Kind::Scene), SW::DockSlot::Right, dock_layout::kSideRatio, idOf(Kind::Inspector));
      if (has(Kind::Settings)) model_.split(idOf(Kind::Inspector), SW::DockSlot::Bottom, 0.5f, idOf(Kind::Settings));
    }
    if (has(Kind::Hierarchy)) {
      model_.split(idOf(Kind::Scene), SW::DockSlot::Left, dock_layout::kSideRatio, idOf(Kind::Hierarchy));
      if (has(Kind::Scenes)) model_.split(idOf(Kind::Hierarchy), SW::DockSlot::Bottom, 0.5f, idOf(Kind::Scenes));
    }
    if (has(Kind::Console)) model_.split(idOf(Kind::Scene), SW::DockSlot::Bottom, dock_layout::kBottomRatio, idOf(Kind::Console));
    tabInto(Kind::Scene, Kind::Game, has);
    tabInto(Kind::Scene, Kind::Coding, has);
    tabInto(Kind::Console, Kind::Profiler, has);
    tabInto(Kind::Console, Kind::Assets, has);
    // A ultima aba adicionada fica ativa; o padrao abre Scene e Console (a primeira de cada grupo).
    if (has(Kind::Scene)) model_.select(idOf(Kind::Scene));
    if (has(Kind::Console)) model_.select(idOf(Kind::Console));
    model_.setArea(area_);
  }

  // Poe a janela `w` como aba da folha de `target` (o mesmo que soltar no centro da folha).
  template <class Has>
  void tabInto(Kind target, Kind w, Has& has) {
    if (!has(w) || !has(target)) return;
    SW::DockDropResult drop;
    drop.action = SW::DockAction::Tab;
    drop.targetLeaf = idOf(target);
    model_.apply(idOf(w), drop);
  }

  bool isRegistered(SU::UIHandle id) const {
    for (const Window& w : windows_) {
      if (w.id == id) return true;
    }
    return false;
  }

  dock_layout::Model model_;
  SW::DockInteractionConfig cfg_{};
  SW::DockDragController drag_{};
  SU::UIHandle activeWin_{};
  SU::UIHandle lastActive_{};   // ultimo grupo tocado: janela nova entra nele
  std::vector<SU::UIHandle> raised_;   // ordem de ativacao (o fim e a janela tocada por ultimo)
  SU::UIHandle resizeWin_{};    // janela flutuante sendo redimensionada pela borda
  int resizeMask_ = 0;          // bordas que o redimensionamento em curso move (mascara de Frame::edgeAt)
  bool resizing_ = false;
  bool pendingResize_ = false;  // o ponteiro andou: a moldura e atualizada uma vez no proximo quadro
  bool pendingDivider_ = false;
  SW::UIDockNode* divider_ = nullptr;   // divisoria entre folhas sendo arrastada
  SU::UIHandle hoverWin_{};     // janela cuja borda esta sob o mouse (sem gesto)
  int hoverMask_ = 0;
  SW::UIDockNode* hoverDivider_ = nullptr;   // divisoria sob o mouse (sem gesto)
  resize_hover::CursorShape cursor_ = resize_hover::CursorShape::Arrow;
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
