#pragma once

// Janela Editor info ligada ao editor: recebe os eventos do SDL (ponteiro, digitacao e teclas de edicao) e desenha a
// view SpriteUI. Dados e regras: editor_info_model.hpp. Desenho e cliques: editor_info_spriteui.hpp. Quem grava o projeto
// e o host (main.cpp, applyEditorInfoActions), que copia o rascunho para o ProjectSettings e chama save().
#include "editor_info_spriteui.hpp"
#include "spriteui_host.hpp"

#include <SDL3/SDL.h>

#include <cstdint>
#include <vector>

namespace editor::infoui {

class Panel {
 public:
  using View = editor::infoui::View;

  Panel(const sui::TextAtlasSet& text, sui::SpriteUiSdlRenderer& sdl) : model_{ProjectSettings{}}, view_{model_, text}, sdl_{sdl} {}

  // Ao abrir a janela: o rascunho volta a copiar o que esta salvo no projeto.
  void open(const ProjectSettings& saved) { model_ = Model(saved); }

  [[nodiscard]] Model& model() { return model_; }
  [[nodiscard]] View& view() { return view_; }
  [[nodiscard]] ToolEngine::SpriteUi::Rect rect() const { return view_.frameRect(); }

  // Eventos do SDL. Devolve true quando a janela usou o evento.
  bool handleEvent(const SDL_Event& e, bool shown) {
    if (!shown) return false;
    switch (e.type) {
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e.button.button != SDL_BUTTON_LEFT) return false;
        x_ = e.button.x;
        y_ = e.button.y;
        if (!inside(x_, y_)) {
          model_.Blur();   // clique fora: o campo em edicao deixa de receber a digitacao
          return false;
        }
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
      case SDL_EVENT_TEXT_INPUT:
        if (!model_.focused() || !e.text.text) return false;
        model_.InsertText(e.text.text);
        return true;
      case SDL_EVENT_KEY_DOWN:
        return keyDown(e.key.key, e.key.mod);
      default:
        return false;
    }
  }

  // Calcula e desenha um quadro. `actions` recebe o que foi clicado neste quadro (Save, Close).
  bool draw(SDL_Renderer* renderer, float windowW, float windowH, std::vector<View::Action>& actions) {
    // Texto so chega com a entrada de texto ligada: liga quando um campo ganha foco e desliga quando perde.
    if (SDL_Window* win = renderer ? sui::UiWindowOf(renderer) : nullptr; win && model_.focused() != textInputOn_) {
      if (model_.focused()) SDL_StartTextInput(win);
      else SDL_StopTextInput(win);
      textInputOn_ = model_.focused();
    }
    FrameInput in;
    in.pointerX = x_;
    in.pointerY = y_;
    in.pointerDown = down_;
    in.pointerPressed = pressed_;
    in.pointerReleased = released_;
    in.frameId = ++frame_;
    pressed_ = false;
    released_ = false;

    const auto packet = view_.Frame(windowW, windowH, in, actions);
    if (!ToolEngine::SpriteUi::Rendering::IsFramePacketValid(packet)) return true;
    return sdl_.draw(renderer, packet);
  }

 private:
  bool inside(float x, float y) const {
    const auto r = view_.frameRect();
    return x >= r.position.x && y >= r.position.y && x < r.position.x + r.size.x && y < r.position.y + r.size.y;
  }

  // Teclas de edicao do campo focado. Teclas com Ctrl viram comandos (A, C, X, V, Z); as outras, movimento e apagar.
  bool keyDown(SDL_Keycode key, SDL_Keymod mod) {
    if (!model_.focused()) return false;
    const bool ctrl = (mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
    const bool shift = (mod & SDL_KMOD_SHIFT) != 0;
    if (ctrl) {
      switch (key) {
        case SDLK_A: model_.SelectAll(); return true;
        case SDLK_C: model_.Copy(); setClipboard(); return true;
        case SDLK_X: model_.Cut(); setClipboard(); return true;
        case SDLK_V:
          if (char* text = SDL_GetClipboardText()) {
            model_.Paste(text);
            SDL_free(text);
          }
          return true;
        case SDLK_Z: model_.Undo(); return true;
        default: return false;
      }
    }
    switch (key) {
      case SDLK_LEFT: model_.Press(Key::Left, shift); return true;
      case SDLK_RIGHT: model_.Press(Key::Right, shift); return true;
      case SDLK_HOME: model_.Press(Key::Home, shift); return true;
      case SDLK_END: model_.Press(Key::End, shift); return true;
      case SDLK_BACKSPACE: model_.Press(Key::Backspace, shift); return true;
      case SDLK_DELETE: model_.Press(Key::Delete, shift); return true;
      case SDLK_TAB: model_.Press(Key::Tab, shift); return true;
      case SDLK_ESCAPE: model_.Press(Key::Escape, shift); return true;
      default: return false;
    }
  }

  void setClipboard() {
    SDL_SetClipboardText(model_.clipboardOut().c_str());
  }

  Model model_;
  View view_;
  sui::SpriteUiSdlRenderer& sdl_;
  float x_ = 0.f, y_ = 0.f;
  bool pressed_ = false, released_ = false, down_ = false, owned_ = false;
  bool textInputOn_ = false;
  std::uint64_t frame_ = 0;
};

}  // namespace editor::infoui
