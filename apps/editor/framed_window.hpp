#pragma once

// Moldura SpriteUI de uma janela do editor que nao tem desenho proprio de cabecalho (Inspector e Settings).
// E a mesma moldura de Scene e Game: titulo, "?", minimizar, X e grip. Quem usa chama prepare() para saber
// a area do corpo, desenha o corpo por conta propria e chama draw() para a moldura. O acoplamento ao dock
// (setDock/floatAt/frameRect/gripRect) e o do host (dock_host.hpp).

#include "window_palette.hpp"
#include "spriteui_host.hpp"
#include "view_painter.hpp"
#include "view_shapes.hpp"
#include "window_frame.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace editor::framed {

namespace SU = ToolEngine::SpriteUi;
namespace Wd = ToolEngine::SpriteUi::Widgets;
namespace SR = ToolEngine::SpriteUi::Rendering;

inline constexpr float kFont = 12.f;
inline constexpr float kTitleFont = 12.f;

class Chrome {
 public:
  Chrome(const sui::TextAtlasSet& text, sui::SpriteUiSdlRenderer& sdl) : painter_{text}, sdl_{sdl} {
  }
  Chrome(const Chrome&) = delete;
  Chrome& operator=(const Chrome&) = delete;

  // Corpo desenhado pelo painel: a moldura nao pinta corpo nem borda (a borda, que e a caixa inteira, apareceria
  // por baixo do painel).
  // O painel desenha o proprio fundo e o contorno dentro da area do corpo.
  void setBodyFill(bool on) {
    bodyFill_ = on;
    applyPalette();
  }

  // Acoplamento: o host manda a area da folha (ou nullopt, flutuante) e le a moldura para o grip.
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

  // Monta a moldura na janela (winW x winH) e devolve o corpo, abaixo do cabecalho. Minimizada, o corpo some.
  sui::Box prepare(float winW, float winH, float width, float height) {
    applyPalette();   // a paleta em uso (Configuracoes > Appearance) vale no quadro atual
    frame_.refreshStyle();
    frame_.placeIn(winW, winH, dock_, width, height);
    visible_ = true;
    if (frame_.minimized()) return {};
    const SU::Rect c = frame_.content();
    return {c.position.x, c.position.y, c.size.x, c.size.y};
  }

  // Janela escondida neste quadro: some a moldura e o acoplamento de tela e soltado no proximo prepare.
  void hide() {
    visible_ = false;
    frame_.reset();
  }

  [[nodiscard]] bool minimized() const { return frame_.minimized(); }
  [[nodiscard]] bool visible() const { return visible_; }

  // Eventos do SDL: so os botoes da moldura (X, "?", minimizar). Arraste e redimensionamento sao do host do dock,
  // que e o unico dono do ponteiro. True quando um botao consumiu o clique.
  bool handleEvent(const SDL_Event& e) {
    if (!visible_) return false;
    if (e.type != SDL_EVENT_MOUSE_BUTTON_DOWN || e.button.button != SDL_BUTTON_LEFT) return false;
    const window_frame::Event ev = frame_.buttonAt({e.button.x, e.button.y});
    if (ev == window_frame::Event::Close) closeRequested_ = true;
    if (ev == window_frame::Event::Help) helpRequested_ = true;
    return ev != window_frame::Event::None;
  }

  // Pedidos da moldura: o X fecha, o "?" abre a ajuda. Cada pedido vale uma vez.
  bool consumeClose() {
    const bool v = closeRequested_;
    closeRequested_ = false;
    return v;
  }
  bool consumeHelp() {
    const bool v = helpRequested_;
    helpRequested_ = false;
    return v;
  }

  // Desenha a moldura (fundo, borda, cabecalho, X, minimizar, "?", titulo e grip). O corpo fica por conta de quem chama.
  bool draw(SDL_Renderer* renderer, const std::string& title) {
    if (!visible_ || !frame_.placed()) return true;
    const SU::Rect frame = frame_.rect();
    view::ShapeList chrome;
    buildHeader(chrome, title);
    view::PaintList list;
    list.owner = sui::handleFor(0);
    Wd::WindowFrameState st;
    st.active = true;
    st.dragging = frame_.dragging();
    st.docked = frame_.docked();
    auto items = Wd::BuildWindowFrameItems(list.owner, frame, frame_.metrics(), st, colors_, -2.f, list.paint);
    list.paint += items.size() + 1;
    list.items.insert(list.items.end(), items.begin(), items.end());
    painter_.append(chrome, 0u, list);
    const std::vector<SR::ClipRectGPU> clips{{frame.position.x, frame.position.y, frame.position.x + frame.size.x, frame.position.y + frame.size.y}};
    return view::drawPaintList(renderer, sdl_, extractor_, packet_, frameId_, clips, list);
  }

 private:
  // Cores da moldura = paleta em uso; sem corpo (o painel desenha por dentro), corpo e borda ficam transparentes.
  void applyPalette() {
    colors_ = editor::window_palette::Standard();
    const palette::Palette& p = palette::active();
    auto c = [](const palette::Rgb& x) { return view::rgbaF(x.r, x.g, x.b, 1.f); };
    titleText_ = c(p.text);
    bubble_ = c(p.accent);
    titleDim_ = c(p.textDim);
    if (!bodyFill_) {
      const auto clear = sui::Srgb(0.f, 0.f, 0.f, 0.f);
      colors_.body = clear;
      colors_.border = clear;
    }
  }
  void buildHeader(view::ShapeList& out, const std::string& title) {
    const SU::Rect head = frame_.header();
    const SU::Rect grip = frame_.gripRect();
    const SU::Rect help = frame_.helpRect();
    const view::Pixel hc{help.position.x + help.size.x * 0.5f, help.position.y + help.size.y * 0.5f};
    out.circleFilled(hc, help.size.x * 0.5f, bubble_);
    out.text({hc.x, hc.y - editor::ui_style::uiFont(kFont) * 0.62f}, "?", editor::ui_style::uiFont(kFont), titleText_, true);   // texto ancorado no topo
    const float tx = frame_.titleTextX();
    const float ty = head.position.y + (head.size.y - editor::ui_style::uiFont(kTitleFont)) * 0.5f;
    out.text({tx, ty}, title, editor::ui_style::uiFont(kTitleFont), titleText_);
    out.text({tx + 0.7f, ty}, title, editor::ui_style::uiFont(kTitleFont), titleText_);   // duas passadas: negrito
    const float cx = grip.position.x + grip.size.x * 0.5f, cy = grip.position.y + grip.size.y * 0.5f;
    for (int c = 0; c < 2; ++c) {
      for (int r = 0; r < 3; ++r) {
        out.circleFilled({cx + (static_cast<float>(c) - 0.5f) * 4.f, cy + (static_cast<float>(r) - 1.f) * 4.f}, 1.2f,
                         titleDim_);
      }
    }
  }

  window_frame::Frame frame_;
  Wd::WindowFrameColors colors_{};
  bool bodyFill_ = true;
  view::Color titleText_ = view::Rgba(233, 234, 238), bubble_ = view::Rgba(64, 102, 178), titleDim_ = view::Rgba(120, 135, 160);
  std::optional<SU::Rect> dock_;
  bool visible_ = false;
  bool closeRequested_ = false, helpRequested_ = false;
  view::ShapePainter painter_;
  sui::SpriteUiSdlRenderer& sdl_;
  SR::RenderExtractor extractor_;
  SR::FramePacket packet_;
  std::uint64_t frameId_ = 0;
};

}  // namespace editor::framed
