#pragma once

// Janela Assets ligada ao editor: recebe os eventos do SDL (ponteiro, digitacao, Enter, Backspace, roda) e desenha a
// view SpriteUI. Regras e estado: assets_model.hpp. Desenho e cliques: assets_spriteui.hpp. Quem lista a pasta e
// aplica as acoes no disco e o host (main.cpp, syncAssetsPanel e applyAssetsActions).
#include "assets_spriteui.hpp"
#include "spriteui_host.hpp"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace editor::assetsui {

class Panel {
 public:
  using View = editor::assetsui::View;

  Panel(const sui::TextAtlasSet& text, sui::SpriteUiSdlRenderer& sdl) : view_{state_, text}, sdl_{sdl} {}

  // Estado que o host preenche (pasta, itens) e le depois (selecao, pedidos).
  [[nodiscard]] AM::State& model() { return state_; }
  [[nodiscard]] View& view() { return view_; }
  // Retangulo da janela no ultimo quadro (para o roteador de paineis).
  [[nodiscard]] ToolEngine::SpriteUi::Rect rect() const { return view_.frameRect(); }

  // Eventos do SDL. Devolve true quando a janela usou o evento (o ImGui por baixo nao deve receber).
  bool handleEvent(const SDL_Event& e, bool shown) {
    if (!shown) return false;
    switch (e.type) {
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e.button.button != SDL_BUTTON_LEFT) return false;
        x_ = e.button.x;
        y_ = e.button.y;
        if (!inside(x_, y_)) return false;
        pressed_ = true;
        down_ = true;
        owned_ = true;
        return true;
      case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e.button.button != SDL_BUTTON_LEFT) return false;
        x_ = e.button.x;
        y_ = e.button.y;
        if (!owned_) return false;
        released_ = true;
        down_ = false;
        owned_ = false;
        return true;
      case SDL_EVENT_MOUSE_MOTION:
        x_ = e.motion.x;
        y_ = e.motion.y;
        return owned_;
      case SDL_EVENT_MOUSE_WHEEL:
        if (!inside(x_, y_)) return false;
        wheelRows_ += static_cast<int>(std::lround(e.wheel.y * 3.f));
        return true;
      case SDL_EVENT_TEXT_INPUT:
        if (!typing() || !e.text.text) return false;
        texts_.emplace_back(e.text.text);
        return true;
      case SDL_EVENT_KEY_DOWN:
        if (!typing()) return false;
        if (e.key.key == SDLK_BACKSPACE) {
          ++backspaces_;
          return true;
        }
        if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) {
          ++enters_;
          return true;
        }
        return false;
      default:
        return false;
    }
  }

  // Aplica a digitacao e a roda do quadro, monta a view e desenha. `actions` recebe o que foi pedido neste quadro.
  // Devolve false se o SDL falhou ao desenhar.
  bool draw(SDL_Renderer* renderer, float windowW, float windowH, std::vector<View::Action>& actions) {
    for (const std::string& t : texts_) view_.TypeText(t);
    for (int i = 0; i < backspaces_; ++i) view_.Backspace();
    for (int i = 0; i < enters_; ++i) view_.EnterKey();
    if (wheelRows_ != 0) view_.Scroll(wheelRows_);
    texts_.clear();
    backspaces_ = 0;
    enters_ = 0;
    wheelRows_ = 0;

    FrameInput in;
    in.pointerX = x_;
    in.pointerY = y_;
    in.pointerDown = down_;
    in.pointerPressed = pressed_;
    in.pointerReleased = released_;
    in.timeSeconds = static_cast<double>(SDL_GetTicks()) / 1000.0;
    in.frameId = ++frame_;
    pressed_ = false;
    released_ = false;

    const auto packet = view_.Frame(windowW, windowH, in, actions);
    if (!ToolEngine::SpriteUi::Rendering::IsFramePacketValid(packet)) return true;
    return sdl_.draw(renderer, packet);
  }

 private:
  // A digitacao vai para o editor de texto, o filtro ou o campo de nome; so nesses casos a janela consome o teclado.
  bool typing() const { return state_.textOpen || state_.filterFocus || state_.nameFocus; }

  bool inside(float x, float y) const {
    const auto r = view_.frameRect();
    return x >= r.position.x && y >= r.position.y && x < r.position.x + r.size.x && y < r.position.y + r.size.y;
  }

  AM::State state_;
  View view_;
  sui::SpriteUiSdlRenderer& sdl_;
  float x_ = 0.f, y_ = 0.f;
  bool pressed_ = false, released_ = false, down_ = false, owned_ = false;
  int backspaces_ = 0;
  int enters_ = 0;
  int wheelRows_ = 0;
  std::vector<std::string> texts_;
  std::uint64_t frame_ = 0;
};

}  // namespace editor::assetsui
