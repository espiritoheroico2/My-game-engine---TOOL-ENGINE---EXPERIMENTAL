#pragma once

// Janela Scene em SpriteUI. A moldura (titulo, foco, docking e o "?") e a SpriteUI (window_frame.hpp), como nas outras
// janelas. A area da cena tambem e SpriteUI: a imagem sai pelo elemento Image da biblioteca (modo Esticar); por cima, a grade, os objetos
// cortados, o gizmo, a barra de ferramentas, a barra flutuante e o menu de contexto do objeto. Regras e contas ficam
// em scene_view_model.hpp; o que o painel precisa do Editor chega em Values (um quadro) e o que ele pede volta em
// Requests. O painel nunca grava na cena.
//
// Exemplo: o gizmo de mover segue a Caixa azul. Com a Caixa selecionada e a ponta do eixo X arrastada 40 px, o
// painel calcula o novo X com scene_view_model::dragEdit e devolve a posicao em Requests; o Editor grava e
// desfaz como antes.
//
// Os eventos de mouse e teclado entram em uma fila (handleEvent) e sao tratados em draw, com os dados do quadro
// atual. handleEvent devolve true so para o que e da moldura (X, "?", minimizar, grip); o resto segue para a fila.
//
// Como funciona:
//   handleEvent: guarda o evento na fila (mouse, Esc para fechar o menu).
//   draw:        trata a fila (botoes, gizmo, menu), monta as formas e desenha: imagem, sobreposicoes, barras e menu.

#include "window_palette.hpp"
#include "i18n.hpp"
#include "scene_transform_menu.hpp"
#include "canvas_edit.hpp"
#include "canvas_division.hpp"
#include "canvas_regions.hpp"
#include "scene_view_model.hpp"
#include "spriteui_host.hpp"
#include "sui_panel.hpp"
#include "view_painter.hpp"
#include "view_shapes.hpp"
#include "window_frame.hpp"

#include "SpriteUi/Elements/Image.hpp"
#include "SpriteUi/Rendering/DrawList.hpp"
#include "SpriteUi/Widgets/Menu.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace editor::scene_view_panel {

namespace vm = editor::scene_view_model;
namespace stm = editor::scene_transform_menu;
namespace view = editor::view;
namespace SU = ToolEngine::SpriteUi;
namespace ST = ToolEngine::SpriteUi::Text;
namespace SR = ToolEngine::SpriteUi::Rendering;
namespace Wd = ToolEngine::SpriteUi::Widgets;
namespace Elm = ToolEngine::SpriteUi::Elements;

inline constexpr float kToolbarH = 30.f;
inline constexpr float kPad = sui::kPanelPadding;
inline constexpr float kFont = 12.f;
inline constexpr float kTitleFont = 12.f;   // titulo da janela (negrito por duas passadas)
inline constexpr float kBtnH = 22.f;
inline constexpr float kFloatH = 28.f;
inline constexpr float kFloatMargin = 10.f;
inline constexpr float kSliderW = 110.f;
inline constexpr float kRowH = 22.f;
inline constexpr float kSepH = 7.f;
inline constexpr float kMenuW = 210.f;
inline constexpr float kClickSlop = 4.f;   // botao direito sem arrastar (px): abre o menu de contexto
inline constexpr std::uint32_t kSceneSlot = 0;   // slot de textura da cena (SpriteUiSdlRenderer::setView)

// Cores em RGBA 0..255.
inline constexpr view::Color kPanelFill = view::Rgba(14, 19, 28, 250);
inline constexpr view::Color kPanelEdge = view::Rgba(51, 61, 77);
inline constexpr view::Color kButtonFill = view::Rgba(33, 41, 54);
inline constexpr view::Color kButtonOn = view::Rgba(51, 92, 133);
inline constexpr view::Color kText = view::Rgba(233, 234, 238);
inline constexpr view::Color kTextDim = view::Rgba(143, 144, 153);
inline constexpr view::Color kHighlight = view::Rgba(255, 235, 90);

// O que o Editor sabe de um objeto sob o mouse. O painel so guarda o id e devolve nos pedidos.
struct Probe {
  bool found = false;
  std::uint64_t id = 0;
  std::string name;
  bool locked = false;
  bool hidden = false;
};

// Acoes do menu de contexto da cena (o alvo e o id do objeto do clique).
enum class CtxAct : std::uint8_t { Inspector, Focus, Duplicate, Delete, SnapToContact, ToggleHidden, ToggleLocked };

// Objeto selecionado neste quadro. present = ha Transform e o objeto esta vivo.
struct Selection {
  bool present = false;
  bool locked = false;
  bool hasParent = false;
  std::array<float, 3> pos{}, rot{}, scale{1.f, 1.f, 1.f};
  tool::Vec3 worldPos{};
  tool::Mat4 world{};       // matriz de mundo do objeto (eixos do espaco Local)
  tool::Mat4 parentInv{};   // inversa da matriz de mundo do pai (so com hasParent)
};

// Dados do Editor para um quadro. Ordem de pintura por cima da imagem: particulas (below), grade (grid), objetos
// cortados, icones e fisica (above), e por ultimo o gizmo, que o painel desenha sozinho.
struct Values {
  bool shown = false;
  tool::Camera cam{};
  SDL_Texture* texture = nullptr;   // imagem renderizada da cena (nullptr: marcador de erro)
  float texW = 1.f, texH = 1.f;
  bool playing = false, paused = false, freeFly = false, ctrlDown = false;
  bool showGrid = false, showGizmos = false, showCulled = false, showPostFx = false;
  int gizmoMode = 0;
  bool localSpace = false;   // Q: gizmo e arrasto seguem os eixos do objeto (false = eixos do mundo)
  bool snapOn = false;
  float snapStep = 0.25f;
  Selection sel;
  // Canvas da UI de jogo, desenhados sobre a cena (o Editor passa todos; a janela desenha os visiveis).
  struct CanvasBox {
    std::uint64_t id = 0;
    tool::canvas::Layout layout;
    tool::canvas::Screen ref;
    bool selected = false;
    bool visible = true;    // "Mostrar na Scene" ligado e objeto nao escondido pelo olho
    bool locked = false;    // cadeado: o canvas nao se move, nao redimensiona e nao edita pontos nem linhas
    std::vector<canvas_division::Point> divisions;   // pontos de divisao (cruzes)
    std::vector<canvas_division::Line> divisionLines;   // linhas entre pontos
    canvas_regions::Regions regions;                    // numero de cada regiao do canvas
  };
  std::vector<CanvasBox> canvases;
  bool selCanvas = false;    // a selecao e um Canvas: mostra o botao Edit canvas
  bool canvasEdit = false;   // modo Edit canvas: clicar e arrastar move ou redimensiona o canvas
  bool divisionAdd = false;  // modo Adicionar ponto: o clique dentro do canvas selecionado coloca uma cruz
  int selDivision = -1;      // ponto de divisao selecionado no canvas selecionado (-1: nenhum)
  bool divisionLine = false; // modo Linha: dois cliques em pontos ligam uma linha
  bool dottedLine = false;   // estilo das linhas novas: pontilhada
  int selLine = -1;          // linha selecionada (-1: nenhuma)
  int lineFrom = -1;         // primeiro ponto escolhido no modo Linha (-1: nenhum)
  view::ShapeList below;
  view::ShapeList grid;   // vazio quando a grade esta desligada
  view::ShapeList above;
  std::function<Probe(vm::Pixel)> probe;
};

// Pedidos ao Editor. Cada campo so vem preenchido quando algo mudou neste quadro.
struct Requests {
  bool playToggle = false, pauseToggle = false, stepOnce = false, snapToContact = false;
  std::optional<bool> grid, gizmos, culled, postFx, snapOn, localSpace;
  std::optional<int> gizmoMode;
  std::optional<float> snapStep;
  std::optional<std::uint64_t> select;
  std::optional<std::array<float, 3>> pos, rot, scale;   // arrasto do gizmo: Transform novo do quadro
  std::optional<std::pair<CtxAct, std::uint64_t>> ctx;   // item escolhido no menu de contexto
  // Modo Edit canvas: liga/desliga, e o arrasto do canvas neste quadro (delta em pixels desde o quadro anterior).
  struct CanvasDrag {
    std::uint64_t id = 0;
    float dx = 0.f, dy = 0.f;
    canvas_edit::Grip grip = canvas_edit::Grip::None;
    canvas_edit::Ppu ppu{};
  };
  std::optional<bool> canvasEdit;
  std::optional<CanvasDrag> canvasDrag;
  // Pontos de divisao do canvas: colocar (Add), arrastar (Move) ou apagar (Remove). Fracao do canvas, 0..1.
  enum class DivisionOp : std::uint8_t { Add, Move, Remove };
  struct DivisionEdit {
    std::uint64_t id = 0;
    DivisionOp op = DivisionOp::Add;
    std::size_t index = 0;
    float x = 0.f, y = 0.f;
  };
  std::optional<DivisionEdit> division;
  // Linhas entre pontos: escolher um ponto no modo Linha (Pick), apagar a linha selecionada (Remove).
  enum class LineOp : std::uint8_t { Pick, Remove };
  struct LineEdit {
    std::uint64_t id = 0;
    LineOp op = LineOp::Pick;
    std::size_t index = 0;   // Pick: indice do ponto; Remove: indice da linha
  };
  std::optional<LineEdit> line;
  std::optional<bool> divisionLine, dottedLine;
  std::optional<int> selLine, lineFrom;
  std::optional<bool> divisionAdd;
  std::optional<int> selDivision;
  bool close = false, help = false;                       // X e "?" da moldura (o host fecha ou abre a Help)
};

// Retangulo da imagem (sem a barra) em coordenadas da janela.
inline sui::Box imageArea(sui::Box rect) { return {rect.x, rect.y + kToolbarH, rect.w, std::max(0.f, rect.h - kToolbarH)}; }

class ScenePanel {
public:
  ScenePanel(const sui::TextAtlasSet& text, sui::SpriteUiSdlRenderer& sdl) : painter_{text}, sdl_{sdl} {
    // Mesmas cores da moldura do Console (window_frame.hpp, janelas SpriteUI).
  }
  ScenePanel(const ScenePanel&) = delete;
  ScenePanel& operator=(const ScenePanel&) = delete;

  // Eventos do SDL. A moldura (X, "?", minimizar, grip) e tratada aqui; o resto vai para a fila da cena.
  // Devolve true quando o evento foi da moldura.
  bool handleEvent(const SDL_Event& e) {
    switch (e.type) {
      case SDL_EVENT_MOUSE_MOTION:
        queue_.push_back({In::Kind::Move, 0, e.motion.x, e.motion.y});
        break;
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e.button.button == SDL_BUTTON_LEFT && frameDown(e.button.x, e.button.y)) return true;
        queue_.push_back({In::Kind::Down, e.button.button, e.button.x, e.button.y});
        break;
      case SDL_EVENT_MOUSE_BUTTON_UP:
        queue_.push_back({In::Kind::Up, e.button.button, e.button.x, e.button.y});
        break;
      case SDL_EVENT_KEY_DOWN:
        if (!e.key.repeat && e.key.key == SDLK_ESCAPE) queue_.push_back({In::Kind::Escape, 0, 0.f, 0.f});
        break;
      default: break;
    }
    return false;
  }

  // Monta a moldura na janela inteira (winW x winH) e devolve o corpo (abaixo do cabecalho): a area da cena.
  // Chame antes de montar os Values, porque a imagem depende desta area. Minimizada, o corpo some (area vazia).
  void setDock(std::optional<SU::Rect> dock) { dock_ = dock; }
  [[nodiscard]] bool docked() const { return frame_.docked(); }
  [[nodiscard]] SU::Rect frameRect() const { return frame_.rect(); }
  [[nodiscard]] SU::Rect gripRect() const { return frame_.gripRect(); }
  // Moldura de posicao e tamanho: o host do dock a usa para redimensionar pelas bordas.
  window_frame::Frame* frameState() { return &frame_; }
  void floatAt(SU::Vec2 at) {
    dock_.reset();
    frame_.undock(at);
  }

  sui::Box prepare(float winW, float winH) {
    frame_.placeIn(winW, winH, dock_, std::max(320.f, winW * 0.6f), std::max(240.f, winH * 0.6f));
    const SU::Rect c = frame_.content();
    rect_ = frame_.minimized() ? sui::Box{} : sui::Box{c.position.x, c.position.y, c.size.x, c.size.y};
    return rect_;
  }

  // Desenha a moldura e o corpo. Os pedidos saem em `out`; X e "?" sao pedidos de janela (close, help).
  bool draw(SDL_Renderer* renderer, const Values& v, Requests& out) {
    visible_ = v.shown;
    out.close = closeRequested_;
    out.help = helpRequested_;
    closeRequested_ = helpRequested_ = false;
    if (!visible_) {
      queue_.clear();
      ctx_.Close();
      dragging_ = false;
      canvasDragging_ = false;
      canvasUp_ = false;
      frame_.reset();
      return true;
    }
    if (!frame_.placed()) return true;   // prepare() ainda nao rodou neste quadro
    const bool minimized = frame_.minimized();
    const sui::Box img = imageArea(rect_);
    if (!minimized) {
      for (const In& ev : queue_) process(ev, v, img, out);
    }
    queue_.clear();
    if (dragging_) updateDrag(v, out);
    if (canvasDragging_ && !minimized) {
      // Delta desde o quadro anterior; o soltar fecha o arrasto depois de mandar o ultimo pedaco.
      const float dx = mouse_.x - canvasLast_.x, dy = mouse_.y - canvasLast_.y;
      if (dx != 0.f || dy != 0.f) out.canvasDrag = Requests::CanvasDrag{canvasId_, dx, dy, canvasGrip_, canvasPpu_};
      canvasLast_ = mouse_;
      if (canvasUp_) {
        canvasDragging_ = false;
        canvasUp_ = false;
      }
    }

    if (divisionDragging_ && !minimized && divisionBox_.w > 0.f && divisionBox_.h > 0.f) {
      const float fx = canvas_division::clamp01((mouse_.x - divisionBox_.x) / divisionBox_.w);
      const float fy = canvas_division::clamp01((mouse_.y - divisionBox_.y) / divisionBox_.h);
      out.division = Requests::DivisionEdit{divisionId_, Requests::DivisionOp::Move, divisionIndex_, fx, fy};
    }

    view::ShapeList overlay;
    if (!minimized && img.w >= 1.f && img.h >= 1.f) {
      const vm::Projection proj = vm::Projection::make(v.cam, areaOf(img));
      overlay.append(v.below);
      overlay.append(v.grid);
      overlay.append(v.above);
      buildCanvases(v, img, overlay);
      if (v.showGizmos && !v.playing && v.sel.present) addGizmo(v, proj, overlay);
    }
    view::ShapeList chrome;
    if (!minimized) {
      buildToolbar(v, chrome);
      buildFloatingBar(v, img, chrome);
      buildPopup(img, chrome);
    }
    if (dragging_) chrome.text({mouse_.x + 14.f, mouse_.y + 12.f}, readout_, kFont, kHighlight);
    buildHeader(chrome);

    // Moldura por tras (zOrder -2), depois o corpo, a imagem, as sobreposicoes e a barra.
    const SU::Rect frame = frame_.rect();
    view::PaintList list;
    list.owner = sui::handleFor(0);
    Wd::WindowFrameState st;
    st.active = true;
    st.dragging = frame_.dragging();
    st.docked = frame_.docked();
    frameColors_ = editor::window_palette::Standard();
    auto frameItems = Wd::BuildWindowFrameItems(list.owner, frame, frame_.metrics(), st, frameColors_, -2.f, list.paint);
    list.paint += frameItems.size() + 1;
    list.items.insert(list.items.end(), frameItems.begin(), frameItems.end());
    if (!minimized) {
      list.items.push_back(sui::Canvas::solidItem(rect_.x, rect_.y, rect_.w, rect_.h, view::premultiplied(kPanelFill), 0.f, list.owner, 0u, list.paint++));
      appendImage(v, img, list);
      painter_.append(overlay, 1u, list);
    }
    painter_.append(chrome, 0u, list);

    const std::vector<SR::ClipRectGPU> clips{{frame.position.x, frame.position.y, frame.position.x + frame.size.x, frame.position.y + frame.size.y},
                                             {img.x, img.y, img.x + img.w, img.y + img.h}};
    sdl_.setView(kSceneSlot, v.texture);
    return view::drawPaintList(renderer, sdl_, extractor_, packet_, frameId_, clips, list);
  }

  // Ponteiro sobre a imagem da cena no ultimo quadro.
  [[nodiscard]] bool hovered() const { return visible_ && imageArea(rect_).contains(mouse_.x, mouse_.y); }
  [[nodiscard]] bool dragging() const { return dragging_; }

private:
  struct In {
    enum class Kind : std::uint8_t { Move, Down, Up, Escape };
    Kind kind = Kind::Move;
    int button = 0;
    float x = 0.f, y = 0.f;
  };

  // Botoes da barra e da barra flutuante. O id diz o que o clique faz.
  enum class Btn : std::uint8_t { PlayStop, Pause, Step, Grid, Gizmos, Culled, PostFx, Mode0, Mode1, Mode2, Magnet, Space, SnapContact, EditCanvas, AddPoint, RemovePoint, AddLine, DottedLine, RemoveLine };
  struct Button {
    sui::Box box;
    Btn id;
    bool enabled;
  };
  struct CtxSlot {
    CtxAct act;
    bool valid;
  };

  // ---------- Entrada (roda em draw, com os dados do quadro) ----------
  void process(const In& ev, const Values& v, const sui::Box& img, Requests& out) {
    const vm::Pixel p{ev.x, ev.y};
    switch (ev.kind) {
      case In::Kind::Move:
        mouse_ = p;
        if (floatDragging_) {
          floatOffset_.x = floatDragOrigin_.x + p.x - floatDragStart_.x - (img.x + kFloatMargin);
          floatOffset_.y = floatDragOrigin_.y + p.y - floatDragStart_.y - (img.y + img.h - kFloatMargin - kFloatH);
        }
        if (sliderDrag_) setSnapFromX(p.x, out);
        break;
      case In::Kind::Escape:
        ctx_.Close();
        break;
      case In::Kind::Down:
        if (ev.button == SDL_BUTTON_LEFT) onLeftDown(p, v, img, out);
        else if (ev.button == SDL_BUTTON_RIGHT) {
          rightDown_ = true;
          rightStart_ = p;
        }
        break;
      case In::Kind::Up:
        if (ev.button == SDL_BUTTON_LEFT) {
          dragging_ = false;
          sliderDrag_ = false;
          floatDragging_ = false;
          if (canvasDragging_) canvasUp_ = true;
          divisionDragging_ = false;
        }
        if (ev.button == SDL_BUTTON_RIGHT && rightDown_) {
          rightDown_ = false;
          const float dx = p.x - rightStart_.x, dy = p.y - rightStart_.y;
          if (dx * dx + dy * dy < kClickSlop * kClickSlop && img.contains(p.x, p.y) && !v.freeFly) openMenu(p, v, out);
        }
        break;
    }
    mouse_ = p;
  }

  // Clique na moldura: X e "?" viram pedidos do proximo draw; o grip inicia o arraste. True: o clique foi da moldura.
  bool frameDown(float x, float y) {
    const window_frame::Event ev = frame_.buttonAt({x, y});
    if (ev == window_frame::Event::Close) closeRequested_ = true;
    if (ev == window_frame::Event::Help) helpRequested_ = true;
    return ev != window_frame::Event::None;
  }

  // Titulo em negrito (duas passadas), circulo do "?" e grip (2 colunas x 3 pontos). Os botoes minimizar e X sao
  // desenhados pela moldura (BuildWindowFrameItems).
  void buildHeader(view::ShapeList& out) {
    const SU::Rect head = frame_.header();
    const SU::Rect grip = frame_.gripRect();
    const SU::Rect help = frame_.helpRect();
    const vm::Pixel hc{help.position.x + help.size.x * 0.5f, help.position.y + help.size.y * 0.5f};
    out.circleFilled(hc, help.size.x * 0.5f, view::Rgba(64, 102, 178));
    out.text({hc.x, hc.y - kFont * 0.62f}, "?", kFont, kText, true);   // texto e ancorado no topo: sobe para centralizar
    const float tx = frame_.titleTextX();
    const float ty = head.position.y + (head.size.y - kTitleFont) * 0.5f;
    out.text({tx, ty}, editor::T("Scene"), kTitleFont, kText);
    out.text({tx + 0.7f, ty}, editor::T("Scene"), kTitleFont, kText);
    const float cx = grip.position.x + grip.size.x * 0.5f, cy = grip.position.y + grip.size.y * 0.5f;
    for (int c = 0; c < 2; ++c) {
      for (int r = 0; r < 3; ++r) {
        const vm::Pixel a{cx - 3.f + 4.f * static_cast<float>(c), cy - 4.f + 4.f * static_cast<float>(r)};
        out.rectFilled(a, {a.x + 2.f, a.y + 2.f}, kTextDim);
      }
    }
  }

  void onLeftDown(vm::Pixel p, const Values& v, const sui::Box& img, Requests& out) {
    if (ctx_.IsOpen()) {   // menu aberto: clique numa linha escolhe; fora dele, fecha e segue o clique
      const int row = rowAt(p);
      if (row >= 0) {
        if (ctx_.Click(0, static_cast<std::size_t>(row)) == Wd::MenuEvent::Activated) {
          const std::size_t idx = ctx_.ActivationPath().front();
          if (idx < ctxActs_.size() && ctxActs_[idx].valid) out.ctx = std::make_pair(ctxActs_[idx].act, ctxTarget_);
        }
        ctx_.Close();
        return;
      }
      ctx_.Close();
    }
    if (const std::optional<Btn> b = buttonAt(p)) {
      press(*b, v, out);
      return;
    }
    if (floatDragHandle_.contains(p.x, p.y)) {
      floatDragging_ = true;
      floatDragStart_ = p;
      floatDragOrigin_ = {floatRect_.x, floatRect_.y};
      return;
    }
    if (sliderBox_.contains(p.x, p.y)) {
      sliderDrag_ = true;
      setSnapFromX(p.x, out);
      return;
    }
    if (floatRect_.contains(p.x, p.y)) return;
    if (img.contains(p.x, p.y)) {
      if (v.canvasEdit && startCanvas(p, v, img, out)) return;
      if (v.freeFly) return;
      if (startGizmo(p, v)) return;
      const Probe hit = v.probe ? v.probe(p) : Probe{};
      if (hit.found) out.select = hit.id;
      return;
    }
  }

  // Clique num botao da barra ou da barra flutuante.
  // Modo Edit canvas: clique sobre um canvas seleciona-o; sobre o canvas ja selecionado, o corpo ou o canto comeca
  // o arrasto. True: o clique foi do canvas (nao vai para o objeto da cena).
  bool startCanvas(vm::Pixel p, const Values& v, const sui::Box& img, Requests& out) {
    for (const Values::CanvasBox& c : v.canvases) {
      if (!c.visible || !c.selected || c.locked) continue;   // travado: sem pontos nem linhas
      const canvas_edit::Box b = canvas_edit::boxFor(c.layout, img.w, img.h, c.ref);
      const canvas_edit::Box win{img.x + b.x, img.y + b.y, b.w, b.h};
      // Pontos de divisao do canvas selecionado: pegar um arrasta; no modo Adicionar, clicar dentro coloca um.
      const canvas_division::Rect rect{win.x, win.y, win.w, win.h};
      const int hit = canvas_division::hitAt(c.divisions, rect, p.x, p.y);
      if (hit >= 0 && v.divisionLine) {
        out.line = Requests::LineEdit{c.id, Requests::LineOp::Pick, static_cast<std::size_t>(hit)};
        return true;
      }
      if (hit >= 0) {
        out.selDivision = hit;
        out.selLine = -1;
        divisionDragging_ = true;
        divisionId_ = c.id;
        divisionIndex_ = static_cast<std::size_t>(hit);
        divisionBox_ = win;
        return true;
      }
      if (!v.divisionLine) {
        const int li = canvas_division::lineAt(c.divisions, c.divisionLines, rect, p.x, p.y);
        if (li >= 0) {
          out.selLine = li;
          out.selDivision = -1;
          return true;
        }
      }
      if (v.divisionAdd && win.contains(p.x, p.y) && win.w > 0.f && win.h > 0.f) {
        const float fx = canvas_division::clamp01((p.x - win.x) / win.w);
        const float fy = canvas_division::clamp01((p.y - win.y) / win.h);
        out.division = Requests::DivisionEdit{c.id, Requests::DivisionOp::Add, c.divisions.size(), fx, fy};
        out.selDivision = static_cast<int>(c.divisions.size());
        return true;
      }
    }
    for (const Values::CanvasBox& c : v.canvases) {
      if (!c.visible) continue;
      const canvas_edit::Box b = canvas_edit::boxFor(c.layout, img.w, img.h, c.ref);
      const canvas_edit::Box win{img.x + b.x, img.y + b.y, b.w, b.h};
      const canvas_edit::Grip g = canvas_edit::gripAt(win, p.x, p.y);
      if (g == canvas_edit::Grip::None) continue;
      if (!c.selected) {
        out.select = c.id;
        return true;
      }
      if (c.locked) return true;   // travado: seleciona, mas nao move nem redimensiona
      canvasDragging_ = true;
      canvasUp_ = false;
      canvasId_ = c.id;
      canvasGrip_ = g;
      canvasPpu_ = canvas_edit::pixelsPerUnit(win, c.layout);
      canvasLast_ = p;
      return true;
    }
    return false;
  }

  // Caixa de cada canvas visivel sobre a cena: contorno (o selecionado em destaque) e, no modo Edit canvas, a alca
  // do canto. Fica por cima da cena e abaixo do gizmo.
  void buildCanvases(const Values& v, const sui::Box& img, view::ShapeList& out) const {
    for (const Values::CanvasBox& c : v.canvases) {
      if (!c.visible) continue;
      const canvas_edit::Box b = canvas_edit::boxFor(c.layout, img.w, img.h, c.ref);
      const vm::Pixel a{img.x + b.x, img.y + b.y}, z{a.x + b.w, a.y + b.h};
      const view::Color edge = c.selected ? kHighlight : kTextDim;
      out.line(a, {z.x, a.y}, 1.f, edge);
      out.line({z.x, a.y}, z, 1.f, edge);
      out.line(z, {a.x, z.y}, 1.f, edge);
      out.line({a.x, z.y}, a, 1.f, edge);
      out.text({a.x + 4.f, a.y + 4.f}, T("Canvas"), kFont, edge);
      // Linhas entre pontos (pontilhadas ou solidas); a selecionada fica em destaque.
      for (std::size_t i = 0; i < c.divisionLines.size(); ++i) {
        const canvas_division::Line& l = c.divisionLines[i];
        if (l.a >= c.divisions.size() || l.b >= c.divisions.size()) continue;
        float ax = 0.f, ay = 0.f, bx = 0.f, by = 0.f;
        canvas_division::toPixels(c.divisions[l.a], {a.x, a.y, b.w, b.h}, ax, ay);
        canvas_division::toPixels(c.divisions[l.b], {a.x, a.y, b.w, b.h}, bx, by);
        const view::Color col = c.selected && static_cast<int>(i) == v.selLine ? kHighlight : kTextDim;
        if (!l.dotted) {
          out.line({ax, ay}, {bx, by}, 1.f, col);
        } else {
          const float dx = bx - ax, dy = by - ay, len = std::sqrt(dx * dx + dy * dy);
          const int steps = static_cast<int>(len / 8.f);
          for (int k = 0; k < steps; ++k) {
            const float t0 = static_cast<float>(k) / static_cast<float>(steps), t1 = (static_cast<float>(k) + 0.5f) / static_cast<float>(steps);
            out.line({ax + dx * t0, ay + dy * t0}, {ax + dx * t1, ay + dy * t1}, 1.f, col);
          }
        }
      }
      // Numero de cada regiao, no centro dela.
      for (int id = 1; id < c.regions.next; ++id) {
        float rx = 0.f, ry = 0.f;
        if (!canvas_regions::centerOf(c.regions, id, rx, ry)) continue;
        out.text({a.x + rx * b.w - 4.f, a.y + ry * b.h - 6.f}, std::to_string(id), kFont, edge);
      }
      // Pontos de divisao: cruz e nome (viewpoint). O ponto selecionado fica em destaque.
      for (std::size_t i = 0; i < c.divisions.size(); ++i) {
        float px = 0.f, py = 0.f;
        canvas_division::toPixels(c.divisions[i], {a.x, a.y, b.w, b.h}, px, py);
        const bool sel = c.selected && static_cast<int>(i) == v.selDivision;
        const view::Color cross = sel ? kHighlight : edge;
        out.line({px - 6.f, py}, {px + 6.f, py}, 1.f, cross);
        out.line({px, py - 6.f}, {px, py + 6.f}, 1.f, cross);
        out.text({px + 8.f, py - 8.f}, canvas_division::displayName(c.divisions[i], i), kFont, cross);
      }
      if (c.selected && v.canvasEdit) {
        // Alcas do modo Edit canvas: a ancora (canto superior esquerdo, move) e o canto inferior direito (redimensiona).
        out.rectFilled({a.x - 4.f, a.y - 4.f}, {a.x + 4.f, a.y + 4.f}, kHighlight, 1.f);
        out.rectFilled({z.x - 4.f, z.y - 4.f}, {z.x + 4.f, z.y + 4.f}, kHighlight, 1.f);
      }
    }
  }

  void press(Btn b, const Values& v, Requests& out) {
    switch (b) {
      case Btn::PlayStop: out.playToggle = true; break;
      case Btn::Pause: out.pauseToggle = true; break;
      case Btn::Step: out.stepOnce = true; break;
      case Btn::Grid: out.grid = !v.showGrid; break;
      case Btn::Gizmos: out.gizmos = !v.showGizmos; break;
      case Btn::Culled: out.culled = !v.showCulled; break;
      case Btn::PostFx: out.postFx = !v.showPostFx; break;
      case Btn::Mode0: out.gizmoMode = 0; break;
      case Btn::Mode1: out.gizmoMode = 1; break;
      case Btn::Mode2: out.gizmoMode = 2; break;
      case Btn::Magnet: out.snapOn = !v.snapOn; break;
      case Btn::Space: out.localSpace = !v.localSpace; break;
      case Btn::SnapContact: out.snapToContact = true; break;
      case Btn::EditCanvas: out.canvasEdit = !v.canvasEdit; break;
      case Btn::AddPoint: out.divisionAdd = !v.divisionAdd; break;
      case Btn::AddLine: out.divisionLine = !v.divisionLine; out.lineFrom = -1; break;
      case Btn::DottedLine: out.dottedLine = !v.dottedLine; break;
      case Btn::RemoveLine:
        for (const Values::CanvasBox& c : v.canvases) {
          if (!c.selected || v.selLine < 0) continue;
          out.line = Requests::LineEdit{c.id, Requests::LineOp::Remove, static_cast<std::size_t>(v.selLine)};
          out.selLine = -1;
        }
        break;
      case Btn::RemovePoint:
        for (const Values::CanvasBox& c : v.canvases) {
          if (!c.selected || v.selDivision < 0) continue;
          out.division = Requests::DivisionEdit{c.id, Requests::DivisionOp::Remove, static_cast<std::size_t>(v.selDivision), 0.f, 0.f};
          out.selDivision = -1;
        }
        break;
    }
  }

  // Comeca o arrasto se o mouse esta sobre uma alca do gizmo do objeto selecionado.
  bool startGizmo(vm::Pixel p, const Values& v) {
    if (!v.showGizmos || v.playing || !v.sel.present || v.sel.locked) return false;
    const vm::Projection proj = vm::Projection::make(v.cam, areaOf(imageArea(rect_)));
    const vm::Gizmo g = gizmoOf(v);
    vm::Pixel center{};
    if (!proj.point(v.sel.worldPos, center)) return false;
    const vm::Mode mode = modeOf(v);
    vm::Gizmo at = g;
    at.center = center;
    const vm::Handle hot = vm::hitTest(proj, at, mode, p);
    if (hot == vm::Handle::None) return false;
    start_ = {};
    start_.mode = mode;
    start_.handle = hot;
    start_.pos = v.sel.pos;
    start_.rot = v.sel.rot;
    start_.scale = v.sel.scale;
    start_.mouse0 = p;
    start_.startWorld = v.sel.worldPos;
    start_.angle0 = std::atan2(p.y - center.y, p.x - center.x);
    start_.gizmo = at;
    start_.eye = v.cam.eye;
    start_.worldAxes = !v.localSpace;
    start_.hasParent = v.sel.hasParent;
    start_.parentInv = v.sel.parentInv;
    dragging_ = true;
    return true;
  }

  // Quadro de arrasto: o Transform novo sai em Requests, so quando o resultado muda.
  void updateDrag(const Values& v, Requests& out) {
    vm::DragFrame f;
    f.mouse = mouse_;
    f.snap = v.ctrlDown || v.snapOn;
    f.snapStep = v.snapStep;
    f.proj = vm::Projection::make(v.cam, areaOf(imageArea(rect_)));
    const vm::DragResult r = vm::dragEdit(start_, f);
    if (!r.changed) return;
    out.pos = r.pos;
    out.rot = r.rot;
    out.scale = r.scale;
    readout_ = r.readout;
  }

  // Menu de contexto do objeto sob o mouse. Sem objeto, nao abre nada.
  void openMenu(vm::Pixel p, const Values& v, Requests& out) {
    const Probe hit = v.probe ? v.probe(p) : Probe{};
    if (!hit.found) return;
    out.select = hit.id;
    ctxTarget_ = hit.id;
    std::vector<Wd::MenuItem> items;
    ctxActs_.clear();
    auto add = [&](std::string label, std::string shortcut, bool disabled, bool hasAct, CtxAct act) {
      Wd::MenuItem it;
      it.label = std::move(label);
      it.shortcut = std::move(shortcut);
      it.disabled = disabled;
      items.push_back(std::move(it));
      ctxActs_.push_back({act, hasAct});
    };
    auto sep = [&]() {
      Wd::MenuItem it;
      it.kind = Wd::MenuItemKind::Separator;
      items.push_back(it);
      ctxActs_.push_back({CtxAct::Inspector, false});
    };
    add(hit.name.empty() ? T("(unnamed)") : hit.name, "", true, false, CtxAct::Inspector);
    sep();
    add(T("Edit in Inspector"), "", false, true, CtxAct::Inspector);
    add(T("Focus"), "", false, true, CtxAct::Focus);
    add(T("Duplicate"), "", false, true, CtxAct::Duplicate);
    add(T("Delete"), "Del", hit.locked, true, CtxAct::Delete);
    sep();
    add(T("Snap to contact"), "", hit.locked, true, CtxAct::SnapToContact);
    sep();
    add(hit.hidden ? T("Show") : T("Hide"), "", false, true, CtxAct::ToggleHidden);
    add(hit.locked ? T("Unlock") : T("Lock"), "", false, true, CtxAct::ToggleLocked);
    ctxPos_ = p;
    ctx_.Open(items);
  }

  // ---------- Geometria ----------
  [[nodiscard]] static vm::Area areaOf(const sui::Box& img) { return {img.x, img.y, img.w, img.h}; }
  [[nodiscard]] vm::Gizmo gizmoOf(const Values& v) const {
    vm::Gizmo g;
    g.origin = v.sel.worldPos;
    g.len = vm::axisLength(g.origin, v.cam.eye);
    g.axes = v.localSpace ? vm::localAxes(v.sel.world) : vm::worldAxes();
    return g;
  }
  [[nodiscard]] static vm::Mode modeOf(const Values& v) { return static_cast<vm::Mode>(std::clamp(v.gizmoMode, 0, 2)); }

  [[nodiscard]] std::optional<Btn> buttonAt(vm::Pixel p) const {
    for (const Button& b : buttons_) {
      if (b.enabled && b.box.contains(p.x, p.y)) return b.id;
    }
    return std::nullopt;
  }

  // Linha do menu sob o ponto, ou -1. Cada item e uma linha; separadores tem altura propria e nao sao clicaveis.
  [[nodiscard]] int rowAt(vm::Pixel p) const {
    for (std::size_t i = 0; i < menuRows_.size(); ++i) {
      if (menuRows_[i].second && menuRows_[i].first.contains(p.x, p.y)) return static_cast<int>(i);
    }
    return -1;
  }

  void setSnapFromX(float x, Requests& out) {
    const float t = std::clamp((x - sliderBox_.x) / std::max(1.f, sliderBox_.w), 0.f, 1.f);
    out.snapStep = stm::snapStepFromSlider(t);
  }

  // ---------- Montagem das formas ----------
  // Gizmo do objeto selecionado, so do modo ativo: eixos (linhas), anel (linhas) ou pontas com quadrado (escala).
  // Mover: cada ponta tem duas linhas em V (seta). Escala: quadrado na ponta. Centro: quadrado branco.
  void addGizmo(const Values& v, const vm::Projection& proj, view::ShapeList& out) const {
    vm::Gizmo g = gizmoOf(v);
    if (!proj.point(g.origin, g.center)) return;
    const vm::Mode mode = modeOf(v);
    const vm::Handle shown = dragging_ ? start_.handle : hotHandle(proj, g, mode);
    for (const vm::Stroke& s : vm::strokes(proj, g, mode, shown)) out.line(s.a, s.b, s.highlighted ? 3.5f : 2.5f, handleColor(s.handle, s.highlighted));
    if (mode != vm::Mode::Rotate) {
      for (int i = 0; i < 3; ++i) {
        vm::Pixel tip{};
        if (!proj.point(g.origin + g.axes[static_cast<std::size_t>(i)] * g.len, tip)) continue;
        const vm::Handle h = static_cast<vm::Handle>(i);
        const view::Color col = handleColor(h, shown == h);
        if (mode == vm::Mode::Move) addArrowHead(out, g.center, tip, col);
        else out.rectFilled({tip.x - 5.f, tip.y - 5.f}, {tip.x + 5.f, tip.y + 5.f}, col);
      }
    }
    const view::Color c = mode == vm::Mode::Scale && shown == vm::Handle::Center ? kHighlight : view::Rgba(255, 255, 255, 220);
    out.rectFilled({g.center.x - 3.f, g.center.y - 3.f}, {g.center.x + 3.f, g.center.y + 3.f}, c);
  }

  // Ponta de seta: duas linhas saindo da ponta, para tras e para os lados (V).
  static void addArrowHead(view::ShapeList& out, vm::Pixel from, vm::Pixel tip, view::Color col) {
    const float dx = tip.x - from.x, dy = tip.y - from.y, l = std::max(1.f, std::hypot(dx, dy));
    const float ux = dx / l, uy = dy / l;
    const vm::Pixel back{tip.x - ux * 10.f, tip.y - uy * 10.f};
    out.line(tip, {back.x - uy * 5.f, back.y + ux * 5.f}, 2.5f, col);
    out.line(tip, {back.x + uy * 5.f, back.y - ux * 5.f}, 2.5f, col);
  }

  // Alca sob o mouse (so com o mouse na imagem, fora de arrasto e fora do modo de jogo: ver addGizmo).
  [[nodiscard]] vm::Handle hotHandle(const vm::Projection& proj, const vm::Gizmo& g, vm::Mode mode) const {
    if (!hovered()) return vm::Handle::None;
    return vm::hitTest(proj, g, mode, mouse_);
  }

  static view::Color handleColor(vm::Handle h, bool highlighted) {
    if (highlighted) return kHighlight;
    switch (h) {
      case vm::Handle::X: return view::Rgba(235, 70, 70);
      case vm::Handle::Y: return view::Rgba(90, 220, 90);
      case vm::Handle::Z: return view::Rgba(80, 120, 245);
      default: return view::Rgba(230, 230, 230);
    }
  }

  // Imagem da cena pelo elemento Image da biblioteca, modo Esticar (a textura cobre a area da imagem).
  void appendImage(const Values& v, const sui::Box& img, view::PaintList& list) const {
    Elm::ImageSource src;
    src.textureSlice = sui::kViewSliceBase + kSceneSlot;
    src.textureSize = {v.texW, v.texH};
    src.loaded = v.texture != nullptr;
    src.fit = Elm::ImageFit::Stretch;
    const SU::Rect content{{img.x, img.y}, {img.w, img.h}};
    const std::vector<SR::UIRenderItem> built =
        Elm::BuildImageSourceItems(list.owner, content, src, {1.f, 1.f, 1.f, 1.f}, Elm::ImageStyle{}, 1u, 0.f, list.paint);
    list.paint += built.size() + 1;
    list.items.insert(list.items.end(), built.begin(), built.end());
  }

  // ---------- Barra de ferramentas (topo) e barra flutuante (embaixo, sobre a imagem) ----------
  // Botao: retangulo com texto centrado; ativo (ou ligado) = cor de destaque. Largura pelo texto + margem.
  void addButton(view::ShapeList& out, float& cursor, float y, float h, const std::string& label, Btn id, bool active, bool enabled, float pad) {
    const float w = painter_.measure(label, kFont) + pad * 2.f;
    const sui::Box b{cursor, y, w, h};
    out.rectFilled({b.x, b.y}, {b.x + b.w, b.y + b.h}, active ? kButtonOn : kButtonFill, 4.f);
    out.text({b.x + b.w * 0.5f, b.y + (b.h - kFont) * 0.5f}, label, kFont, kText, true);
    buttons_.push_back({b, id, enabled});
    cursor += w + 4.f;
  }

  void buildToolbar(const Values& v, view::ShapeList& out) {
    buttons_.clear();
    float x = rect_.x + kPad;
    const float y = rect_.y + (kToolbarH - kBtnH) * 0.5f;
    addButton(out, x, y, kBtnH, v.playing ? T("Stop") : T("Play"), Btn::PlayStop, v.playing, true, 9.f);
    if (v.playing) {
      addButton(out, x, y, kBtnH, v.paused ? T("Resume") : T("Pause"), Btn::Pause, v.paused, true, 9.f);
      if (v.paused) addButton(out, x, y, kBtnH, T("Step"), Btn::Step, false, true, 9.f);
    }
    x += 6.f;
    addButton(out, x, y, kBtnH, T("Grid"), Btn::Grid, v.showGrid, true, 9.f);
    addButton(out, x, y, kBtnH, T("Gizmos"), Btn::Gizmos, v.showGizmos, true, 9.f);
    addButton(out, x, y, kBtnH, T("Culled"), Btn::Culled, v.showCulled, true, 9.f);
    addButton(out, x, y, kBtnH, T("Post-fx"), Btn::PostFx, v.showPostFx, true, 9.f);
    const std::string hint = v.freeFly ? T("FREE NAVIGATION: WASD move, Q/E down/up, Shift fast, wheel speed, Alt/Esc exit")
                                       : T("right: orbit | middle: pan | wheel: zoom | Alt: free navigation | double-click in Hierarchy: focus");
    out.text({x + 8.f, rect_.y + (kToolbarH - kFont) * 0.5f}, hint, kFont, kTextDim, false, std::max(0.f, rect_.x + rect_.w - kPad - x - 16.f));
  }

  void buildFloatingBar(const Values& v, const sui::Box& img, view::ShapeList& out) {
    if (img.w < 120.f || img.h < 60.f) return;
    const float width = floatingBarWidth(v);
    const float x0 = std::clamp(img.x + kFloatMargin + floatOffset_.x, img.x + 4.f,
                                std::max(img.x + 4.f, img.x + img.w - width - 4.f));
    const float y = std::clamp(img.y + img.h - kFloatMargin - kFloatH + floatOffset_.y, img.y + 4.f,
                               std::max(img.y + 4.f, img.y + img.h - kFloatH - 4.f));
    view::ShapeList bar;
    float x = x0 + 26.f;
    const float by = y + (kFloatH - kBtnH) * 0.5f;
    const int mode = std::clamp(v.gizmoMode, 0, 2);
    addButton(bar, x, by, kBtnH, T("Move (W)"), Btn::Mode0, mode == 0, true, 8.f);
    addButton(bar, x, by, kBtnH, T("Rotate (E)"), Btn::Mode1, mode == 1, true, 8.f);
    addButton(bar, x, by, kBtnH, T("Scale (R)"), Btn::Mode2, mode == 2, true, 8.f);
    x += 4.f;
    addButton(bar, x, by, kBtnH, T("Magnet"), Btn::Magnet, v.snapOn, true, 8.f);
    x += 4.f;
    addButton(bar, x, by, kBtnH, v.localSpace ? T("Local (Q)") : T("World (Q)"), Btn::Space, v.localSpace, true, 8.f);
    if (v.selCanvas) addButton(bar, x, by, kBtnH, T("Edit canvas"), Btn::EditCanvas, v.canvasEdit, true, 8.f);
    if (v.selCanvas && v.canvasEdit) addButton(bar, x, by, kBtnH, T("Add point"), Btn::AddPoint, v.divisionAdd, true, 8.f);
    if (v.selCanvas && v.canvasEdit && v.selDivision >= 0) addButton(bar, x, by, kBtnH, T("Remove point"), Btn::RemovePoint, false, true, 8.f);
    if (v.selCanvas && v.canvasEdit) addButton(bar, x, by, kBtnH, T("Line"), Btn::AddLine, v.divisionLine, true, 8.f);
    if (v.selCanvas && v.canvasEdit && v.divisionLine) addButton(bar, x, by, kBtnH, T("Dotted"), Btn::DottedLine, v.dottedLine, true, 8.f);
    if (v.selCanvas && v.canvasEdit && v.selLine >= 0) addButton(bar, x, by, kBtnH, T("Remove line"), Btn::RemoveLine, false, true, 8.f);
    x += 4.f;
    sliderBox_ = {x, y + (kFloatH - 8.f) * 0.5f, kSliderW, 8.f};
    const float t = stm::sliderFromSnapStep(v.snapStep);
    bar.rectFilled({sliderBox_.x, sliderBox_.y}, {sliderBox_.x + sliderBox_.w, sliderBox_.y + sliderBox_.h}, view::Rgba(20, 24, 32), 4.f);
    bar.rectFilled({sliderBox_.x, sliderBox_.y}, {sliderBox_.x + sliderBox_.w * t, sliderBox_.y + sliderBox_.h}, view::Rgba(90, 150, 220), 4.f);
    char val[32];
    std::snprintf(val, sizeof val, "%.2f m", static_cast<double>(v.snapStep));
    const float valX = sliderBox_.x + sliderBox_.w + 6.f;
    bar.text({valX, y + (kFloatH - kFont) * 0.5f}, val, kFont, kText);
    x = valX + painter_.measure(val, kFont) + 10.f;
    addButton(bar, x, by, kBtnH, T("Snap to contact"), Btn::SnapContact, false, v.sel.present && !v.sel.locked, 8.f);
    const float right = x - 4.f + 8.f;
    floatRect_ = {x0, y, right - x0, kFloatH};
    floatDragHandle_ = {x0 + 2.f, y + 3.f, 18.f, kFloatH - 6.f};
    out.panel({x0, y}, {right, y + kFloatH}, 6.f, view::Rgba(14, 19, 28, 230), kPanelEdge);
    for (int i = 0; i < 3; ++i) {
      const float gx = floatDragHandle_.x + 5.f + static_cast<float>(i) * 4.f;
      bar.line({gx, y + 9.f}, {gx, y + 19.f}, 1.5f, kTextDim);
    }
    out.append(bar);
  }

  float floatingBarWidth(const Values& v) const {
    float cursor = 26.f;
    const auto addWidth = [&](const std::string& label, float pad) {
      cursor += painter_.measure(label, kFont) + 2.f * pad + 4.f;
    };
    addWidth(T("Move (W)"), 8.f);
    addWidth(T("Rotate (E)"), 8.f);
    addWidth(T("Scale (R)"), 8.f);
    cursor += 4.f;
    addWidth(T("Magnet"), 8.f);
    cursor += 4.f;
    addWidth(v.localSpace ? T("Local (Q)") : T("World (Q)"), 8.f);
    if (v.selCanvas) addWidth(T("Edit canvas"), 8.f);
    if (v.selCanvas && v.canvasEdit) addWidth(T("Add point"), 8.f);
    if (v.selCanvas && v.canvasEdit && v.selDivision >= 0) addWidth(T("Remove point"), 8.f);
    if (v.selCanvas && v.canvasEdit) addWidth(T("Line"), 8.f);
    if (v.selCanvas && v.canvasEdit && v.divisionLine) addWidth(T("Dotted"), 8.f);
    if (v.selCanvas && v.canvasEdit && v.selLine >= 0) addWidth(T("Remove line"), 8.f);
    cursor += 4.f + kSliderW + 6.f;
    char val[32];
    std::snprintf(val, sizeof val, "%.2f m", static_cast<double>(v.snapStep));
    cursor += painter_.measure(val, kFont) + 10.f;
    addWidth(T("Snap to contact"), 8.f);
    return cursor + 4.f;
  }

  // Menu de contexto (modelo Wd::MenuControl): fundo de painel, linhas e destaque.
  void buildPopup(const sui::Box& img, view::ShapeList& out) {
    menuRows_.clear();
    if (!ctx_.IsOpen()) return;
    const auto& items = ctx_.Items(0);
    float h = 0.f;
    for (const auto& it : items) h += it.kind == Wd::MenuItemKind::Separator ? kSepH : kRowH;
    const float x = std::max(img.x + 4.f, std::min(ctxPos_.x, img.x + img.w - kMenuW - 4.f));
    const float y = std::max(img.y + 4.f, std::min(ctxPos_.y, img.y + img.h - h - 4.f));
    out.panel({x, y}, {x + kMenuW, y + h}, 4.f, view::Rgba(12, 16, 24, 250), kPanelEdge);
    const std::size_t hi = ctx_.Highlighted(0);
    float row = y;
    for (std::size_t i = 0; i < items.size(); ++i) {
      const Wd::MenuItem& it = items[i];
      if (it.kind == Wd::MenuItemKind::Separator) {
        out.line({x + 6.f, row + kSepH * 0.5f}, {x + kMenuW - 6.f, row + kSepH * 0.5f}, 1.f, kPanelEdge);
        menuRows_.push_back({{x, row, kMenuW, kSepH}, false});
        row += kSepH;
        continue;
      }
      const bool enabled = !it.disabled;
      if (i == hi && enabled) out.rectFilled({x + 2.f, row + 1.f}, {x + kMenuW - 2.f, row + kRowH - 1.f}, kButtonOn, 3.f);
      out.text({x + 10.f, row + (kRowH - kFont) * 0.5f}, it.label, kFont, enabled ? kText : view::Rgba(120, 122, 130));
      if (!it.shortcut.empty()) {
        out.text({x + kMenuW - 10.f - painter_.measure(it.shortcut, kFont), row + (kRowH - kFont) * 0.5f}, it.shortcut, kFont, kTextDim);
      }
      menuRows_.push_back({{x, row, kMenuW, kRowH}, enabled});
      row += kRowH;
    }
  }

  // ---------- Estado ----------
  sui::Box rect_{};
  bool visible_ = false;
  vm::Pixel mouse_{};
  std::vector<In> queue_;

  bool dragging_ = false;
  bool canvasDragging_ = false;   // arrasto de canvas em curso (Edit canvas)
  bool divisionDragging_ = false; // arrasto de um ponto de divisao em curso
  std::uint64_t divisionId_ = 0;
  std::size_t divisionIndex_ = 0;
  canvas_edit::Box divisionBox_{};   // caixa do canvas (em pixels de janela) no inicio do arrasto
  bool canvasUp_ = false;         // soltou no meio do quadro: o arrasto acaba depois de mandar o ultimo delta
  std::uint64_t canvasId_ = 0;
  canvas_edit::Grip canvasGrip_ = canvas_edit::Grip::None;
  canvas_edit::Ppu canvasPpu_{};
  vm::Pixel canvasLast_{};
  vm::DragStart start_{};
  std::string readout_;
  bool sliderDrag_ = false;
  bool floatDragging_ = false;
  vm::Pixel floatOffset_{};
  vm::Pixel floatDragStart_{};
  vm::Pixel floatDragOrigin_{};
  bool rightDown_ = false;
  vm::Pixel rightStart_{};

  Wd::MenuControl ctx_;
  std::vector<CtxSlot> ctxActs_;
  std::vector<std::pair<sui::Box, bool>> menuRows_;
  std::uint64_t ctxTarget_ = 0;
  vm::Pixel ctxPos_{};

  std::vector<Button> buttons_;
  sui::Box sliderBox_{};
  sui::Box floatRect_{};   // fundo da barra flutuante (o clique nele nao vai para a cena)
  sui::Box floatDragHandle_{};

  view::ShapePainter painter_;
  sui::SpriteUiSdlRenderer& sdl_;
  window_frame::Frame frame_;
  std::optional<SU::Rect> dock_;
  Wd::WindowFrameColors frameColors_{};
  bool closeRequested_ = false;
  bool helpRequested_ = false;
  SR::RenderExtractor extractor_;
  SR::FramePacket packet_;
  std::uint64_t frameId_ = 0;
};

}  // namespace editor::scene_view_panel
