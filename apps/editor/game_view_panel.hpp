#pragma once

// Janela Jogo em SpriteUI (tambem o modo --player, sem barra). A moldura e do ImGui, como na Scene. Cada camera
// principal vira uma celula: a imagem sai pelo elemento Image da biblioteca (Conter no letterbox, Preencher ou
// Esticar), e por cima vao as particulas, a grade e a UI do jogo. Os retangulos das celulas vem do
// game_view_model.hpp; o Editor monta as formas de cada celula e passa em Values. O painel desenha, trata os cliques
// dos botoes da UI (so no Play) e a barra de Play. Nao grava nada: os pedidos voltam em Requests.
//
// Exemplo: a UI "Vida" do jogo e um retangulo vermelho no canto de cima. Durante o Play, um clique nele devolve a
// acao "vida.cura" em Requests, e o Editor faz o mesmo que fazia antes. Com a janela em letterbox (16:9 numa area
// 4:3), a imagem fica no meio e as faixas pretas sao da janela, nao da imagem.
//
// Como funciona:
//   draw:        desenha o fundo, cada celula (imagem, sobreposicao) e a barra (se nao e o modo --player).
//   handleEvent: guarda o mouse e os cliques para o proximo draw (sem consumir nada).

#include "game_view_model.hpp"
#include "i18n.hpp"
#include "scene_view_model.hpp"
#include "spriteui_host.hpp"
#include "sui_panel.hpp"
#include "view_painter.hpp"
#include "view_shapes.hpp"

#include "SpriteUi/Elements/Image.hpp"
#include "SpriteUi/Rendering/DrawList.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace editor::game_view_panel {

namespace gm = editor::game_view_model;
namespace vm = editor::scene_view_model;
namespace view = editor::view;
namespace SU = ToolEngine::SpriteUi;
namespace ST = ToolEngine::SpriteUi::Text;
namespace SR = ToolEngine::SpriteUi::Rendering;
namespace Elm = ToolEngine::SpriteUi::Elements;

inline constexpr float kToolbarH = 30.f;
inline constexpr float kPad = sui::kPanelPadding;
inline constexpr float kFont = 13.f;
inline constexpr float kBtnH = 22.f;
inline constexpr std::uint32_t kFirstCellSlot = 1;   // slot 0 e da Scene (SpriteUiSdlRenderer::setView)
inline constexpr view::Color kPanelFill = view::Rgba(14, 19, 28, 250);
inline constexpr view::Color kButtonFill = view::Rgba(33, 41, 54);
inline constexpr view::Color kButtonOn = view::Rgba(51, 92, 133);
inline constexpr view::Color kText = view::Rgba(233, 234, 238);
inline constexpr view::Color kTextDim = view::Rgba(143, 144, 153);

// Botao da UI do jogo: retangulo na janela e a acao que ele dispara.
struct UiHit {
  vm::Pixel a{}, b{};
  std::string action;
};

// Uma camera principal: retangulo da celula, textura, modo de ajuste e as formas e botoes dela.
struct Cell {
  gm::Rect box{};
  SDL_Texture* texture = nullptr;
  float texW = 1.f, texH = 1.f;
  Elm::ImageFit fit = Elm::ImageFit::Stretch;
  view::ShapeList overlay;
  std::vector<UiHit> hits;
};

struct Values {
  bool shown = false;
  bool player = false;      // --player: janela cheia, sem barra
  bool playing = false, paused = false;
  std::string cameraLabel;  // texto pronto da barra ("Principal cameras: 2 (vertical split)")
  std::string message;      // sem camera principal: aviso no centro
  std::vector<Cell> cells;
};

struct Requests {
  bool playToggle = false, pauseToggle = false, stepOnce = false;
  std::vector<std::string> uiActions;   // cliques nos botoes da UI do jogo (so durante o Play)
};

class GamePanel {
public:
  GamePanel(const ST::TruetypeAtlas& atlas, sui::SpriteUiSdlRenderer& sdl) : painter_{atlas}, sdl_{sdl} {}
  GamePanel(const GamePanel&) = delete;
  GamePanel& operator=(const GamePanel&) = delete;

  // Guarda o evento para o proximo draw. Sempre false: o evento segue para o ImGui.
  bool handleEvent(const SDL_Event& e) {
    switch (e.type) {
      case SDL_EVENT_MOUSE_MOTION: queue_.push_back({In::Kind::Move, 0, e.motion.x, e.motion.y}); break;
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e.button.button == SDL_BUTTON_LEFT) queue_.push_back({In::Kind::Down, 0, e.button.x, e.button.y});
        break;
      case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e.button.button == SDL_BUTTON_LEFT) queue_.push_back({In::Kind::Up, 0, e.button.x, e.button.y});
        break;
      default: break;
    }
    return false;
  }

  // Desenha a janela na area `rect` (coordenadas da janela). Os cliques de UI saem em `out`.
  bool draw(SDL_Renderer* renderer, sui::Box rect, const Values& v, Requests& out) {
    rect_ = rect;
    visible_ = v.shown;
    if (!visible_) {
      queue_.clear();
      pressed_.reset();
      return true;
    }
    for (const In& ev : queue_) process(ev, v, out);
    queue_.clear();

    const sui::Box bar{rect.x, rect.y, rect.w, v.player ? 0.f : kToolbarH};
    const std::vector<SR::ClipRectGPU> clips = clipsFor(rect, v);
    view::PaintList list;
    list.owner = sui::handleFor(0);
    list.items.push_back(sui::Canvas::solidItem(rect.x, rect.y, rect.w, rect.h, view::premultiplied(kPanelFill), 0.f, list.owner, 0u, list.paint++));
    for (std::size_t i = 0; i < v.cells.size(); ++i) {
      const Cell& c = v.cells[i];
      const auto clip = static_cast<std::uint32_t>(i + 1);
      sdl_.setView(static_cast<std::uint32_t>(kFirstCellSlot + i), c.texture);
      appendCellImage(c, i, clip, list);
      painter_.append(c.overlay, clip, list);
    }
    view::ShapeList chrome;
    if (!v.player) buildToolbar(v, bar, chrome);
    if (v.cells.empty() && !v.message.empty()) {
      chrome.text({rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f - 8.f}, v.message, kFont, kTextDim, true, rect.w - 24.f);
    }
    painter_.append(chrome, 0u, list);
    return view::drawPaintList(renderer, sdl_, extractor_, packet_, frameId_, clips, list);
  }

  // Indice do botao de UI sob o mouse na celula `cell`, durante o Play (ou -1). Para pintar o estado do botao.
  [[nodiscard]] int hoverHit(std::size_t cell) const { return cell == hoverCell_ ? hoverIndex_ : -1; }
  [[nodiscard]] int pressedHit(std::size_t cell) const { return pressed_ && pressed_->first == cell ? static_cast<int>(pressed_->second) : -1; }

private:
  struct In {
    enum class Kind : std::uint8_t { Move, Down, Up };
    Kind kind = Kind::Move;
    int button = 0;
    float x = 0.f, y = 0.f;
  };

  void process(const In& ev, const Values& v, Requests& out) {
    const vm::Pixel p{ev.x, ev.y};
    mouse_ = p;
    if (ev.kind == In::Kind::Move) {
      updateHover(v, p);
      return;
    }
    if (ev.kind == In::Kind::Down) {
      if (buttonAt(p, out)) return;
      if (!v.playing) return;
      if (const auto hit = hitAt(v, p)) pressed_ = hit;
      return;
    }
    // Up: clique completo = soltar dentro do mesmo botao que foi apertado.
    if (pressed_) {
      const auto hit = hitAt(v, p);
      if (hit && *hit == *pressed_) out.uiActions.push_back(v.cells[pressed_->first].hits[pressed_->second].action);
      pressed_.reset();
    }
  }

  // Botoes da barra. Devolve true se o clique foi num deles.
  bool buttonAt(vm::Pixel p, Requests& out) {
    for (const Button& b : buttons_) {
      if (!b.box.contains(p.x, p.y)) continue;
      switch (b.id) {
        case Btn::PlayStop: out.playToggle = true; break;
        case Btn::Pause: out.pauseToggle = true; break;
        case Btn::Step: out.stepOnce = true; break;
      }
      return true;
    }
    return false;
  }

  // Botao da UI do jogo sob o ponto: (celula, indice). Na ordem de desenho: o ultimo (por cima) vence.
  [[nodiscard]] std::optional<std::pair<std::size_t, std::size_t>> hitAt(const Values& v, vm::Pixel p) const {
    for (std::size_t c = v.cells.size(); c-- > 0;) {
      const auto& hits = v.cells[c].hits;
      for (std::size_t i = hits.size(); i-- > 0;) {
        const UiHit& h = hits[i];
        if (p.x >= h.a.x && p.x < h.b.x && p.y >= h.a.y && p.y < h.b.y) return std::make_pair(c, i);
      }
    }
    return std::nullopt;
  }

  void updateHover(const Values& v, vm::Pixel p) {
    hoverCell_ = static_cast<std::size_t>(-1);
    hoverIndex_ = -1;
    if (!v.playing) return;
    if (const auto hit = hitAt(v, p)) {
      hoverCell_ = hit->first;
      hoverIndex_ = static_cast<int>(hit->second);
    }
  }

  // Clip 0 = janela; clip i+1 = celula i (cada imagem e cada overlay fica dentro da propria celula).
  static std::vector<SR::ClipRectGPU> clipsFor(const sui::Box& rect, const Values& v) {
    std::vector<SR::ClipRectGPU> clips{{rect.x, rect.y, rect.x + rect.w, rect.y + rect.h}};
    for (const Cell& c : v.cells) clips.push_back({c.box.x, c.box.y, c.box.x + c.box.w, c.box.y + c.box.h});
    return clips;
  }

  // Imagem da celula: Conter (letterbox), Preencher ou Esticar, pelo elemento Image da biblioteca.
  void appendCellImage(const Cell& c, std::size_t index, std::uint32_t clip, view::PaintList& list) const {
    if (c.box.w < 1.f || c.box.h < 1.f) return;
    Elm::ImageSource src;
    src.textureSlice = sui::kViewSliceBase + kFirstCellSlot + static_cast<std::uint32_t>(index);
    src.textureSize = {c.texW, c.texH};
    src.loaded = c.texture != nullptr;
    src.fit = c.fit;
    const SU::Rect content{{c.box.x, c.box.y}, {c.box.w, c.box.h}};
    const std::vector<SR::UIRenderItem> built =
        Elm::BuildImageSourceItems(list.owner, content, src, {1.f, 1.f, 1.f, 1.f}, Elm::ImageStyle{}, clip, 0.f, list.paint);
    list.paint += built.size() + 1;
    list.items.insert(list.items.end(), built.begin(), built.end());
  }

  // Barra: Play/Stop, Pause/Resume, Step (com o jogo pausado) e o texto das cameras.
  enum class Btn : std::uint8_t { PlayStop, Pause, Step };
  struct Button {
    sui::Box box;
    Btn id;
  };

  void buildToolbar(const Values& v, const sui::Box& bar, view::ShapeList& out) {
    buttons_.clear();
    float x = bar.x + kPad;
    const float y = bar.y + (bar.h - kBtnH) * 0.5f;
    addButton(out, x, y, v.playing ? T("Stop") : T("Play"), Btn::PlayStop, v.playing);
    if (v.playing) addButton(out, x, y, v.paused ? T("Resume") : T("Pause"), Btn::Pause, v.paused);
    if (v.playing && v.paused) addButton(out, x, y, T("Step"), Btn::Step, false);
    x += 10.f;
    out.text({x, bar.y + (bar.h - kFont) * 0.5f}, v.cameraLabel, kFont, kTextDim);
  }

  void addButton(view::ShapeList& out, float& cursor, float y, const std::string& label, Btn id, bool active) {
    const float w = painter_.measure(label, kFont) + 18.f;
    const sui::Box b{cursor, y, w, kBtnH};
    out.rectFilled({b.x, b.y}, {b.x + b.w, b.y + b.h}, active ? kButtonOn : kButtonFill, 4.f);
    out.text({b.x + b.w * 0.5f, b.y + (b.h - kFont) * 0.5f}, label, kFont, kText, true);
    buttons_.push_back({b, id});
    cursor += w + 4.f;
  }

  sui::Box rect_{};
  bool visible_ = false;
  std::vector<In> queue_;
  vm::Pixel mouse_{};
  std::optional<std::pair<std::size_t, std::size_t>> pressed_;
  std::size_t hoverCell_ = static_cast<std::size_t>(-1);
  int hoverIndex_ = -1;
  std::vector<Button> buttons_;

  view::ShapePainter painter_;
  sui::SpriteUiSdlRenderer& sdl_;
  SR::RenderExtractor extractor_;
  SR::FramePacket packet_;
  std::uint64_t frameId_ = 0;
};

}  // namespace editor::game_view_panel
