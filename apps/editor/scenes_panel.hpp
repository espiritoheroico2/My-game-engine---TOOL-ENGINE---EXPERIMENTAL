#pragma once

// Janela Scenes ligada ao editor: recebe os eventos do SDL (ponteiro, digitacao, apagar) e desenha a view SpriteUI.
// Regras: scenes_model.hpp. Desenho e cliques: scenes_spriteui.hpp. Quem aplica as acoes ao Editor e o host
// (main.cpp, applyScenesActions), que tambem copia a lista e as cenas para o modelo antes de cada quadro.
#include "scenes_spriteui.hpp"
#include "spriteui_host.hpp"

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>
#include <vector>

namespace editor::scenesui {

class Panel {
 public:
  using View = editor::scenesui::View;

  Panel(const sui::TextAtlasSet& text, sui::SpriteUiSdlRenderer& sdl)
      : view_{model_, text}, sdl_{sdl} {}

  // Estado que o host preenche (cenas, assets, candidato a Assign) e le depois (selecao, modo do campo).
  [[nodiscard]] scenes_model::Model& model() { return model_; }
  [[nodiscard]] View& view() { return view_; }
  // Retangulo da janela no ultimo quadro (para o roteador de paineis).
  [[nodiscard]] ToolEngine::SpriteUi::Rect rect() const { return view_.frameRect(); }

  // Comeca a criar uma cena (atalho do menu "Nova cena").
  void beginNew() { scenes_model::BeginName(model_, scenes_model::NameMode::New); }

  // Eventos do SDL. Devolve true quando a janela usou o evento.
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
        doubleClick_ = e.button.clicks >= 2;
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
        if (inside(x_, y_)) dirty_ = true;   // passar o mouse sobre botoes e linhas muda o realce
        return owned_;
      case SDL_EVENT_MOUSE_WHEEL:
        if (!inside(e.wheel.mouse_x, e.wheel.mouse_y)) return false;
        view_.wheelAt(e.wheel.mouse_x, e.wheel.mouse_y, -e.wheel.y * 40.f);
        dirty_ = true;
        return true;
      case SDL_EVENT_TEXT_INPUT:
        if (!model_.nameFocus || !e.text.text) return false;
        texts_.emplace_back(e.text.text);
        dirty_ = true;
        return true;
      case SDL_EVENT_KEY_DOWN:
        // Enter confirma e Esc cancela criar/renomear/apagar; Backspace apaga no campo de nome em foco.
        if ((e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) && model_.nameFocus) {
          view_.PressEnter();
          dirty_ = true;
          return true;
        }
        if (e.key.key == SDLK_ESCAPE && model_.mode != scenes_model::NameMode::None) {
          view_.PressEscape();
          dirty_ = true;
          return true;
        }
        if (e.key.key != SDLK_BACKSPACE || !model_.nameFocus) return false;
        ++backspaces_;
        dirty_ = true;
        return true;
      default:
        return false;
    }
  }

  // Aplica a digitacao do quadro, monta a view e desenha. `actions` recebe o que foi clicado neste quadro.
  // Devolve false se o SDL falhou ao desenhar.
  bool draw(SDL_Renderer* renderer, float windowW, float windowH, std::vector<View::Action>& actions) {
    // O SDL so envia SDL_EVENT_TEXT_INPUT com a entrada de texto ligada. Liga quando o campo de nome ganha foco
    // (criar ou renomear) e desliga quando perde; sem isso, nada que se digita entra no nome.
    if (SDL_Window* win = renderer ? SDL_GetRenderWindow(renderer) : nullptr; win && model_.nameFocus != textInputOn_) {
      if (model_.nameFocus) SDL_StartTextInput(win);
      else SDL_StopTextInput(win);
      textInputOn_ = model_.nameFocus;
    }
    for (const std::string& t : texts_) view_.TypeText(t);
    for (int i = 0; i < backspaces_; ++i) view_.Backspace();
    texts_.clear();
    backspaces_ = 0;

    // Pressionar e soltar no mesmo quadro (clique rapido): o Down vale neste quadro e a soltura no seguinte, senao o
    // clique do SpriteUI se perde. O quadro extra e pedido ao host (needsFrame).
    const bool deferRelease = pressed_ && released_;
    FrameInput in;
    in.pointerX = x_;
    in.pointerY = y_;
    in.pointerDown = deferRelease ? true : down_;
    in.pointerPressed = pressed_;
    in.pointerReleased = released_ && !deferRelease;
    in.doubleClick = pressed_ && doubleClick_;
    in.frameId = ++frame_;
    // O clique muda o modelo depois do desenho: um quadro a mais mostra o resultado sem esperar outro evento.
    needsFrame_ = pressed_ || released_ || dirty_;
    dirty_ = false;
    pressed_ = false;
    if (!deferRelease) released_ = false;

    const auto packet = view_.Frame(windowW, windowH, in, actions);
    if (!ToolEngine::SpriteUi::Rendering::IsFramePacketValid(packet)) return true;
    return sdl_.draw(renderer, packet);
  }

 public:
  // true quando o quadro tratou um clique: o host pede mais um quadro (o ritmo por eventos nao redesenha sozinho).
  [[nodiscard]] bool needsFrame() const { return needsFrame_ || released_; }

 private:
  bool inside(float x, float y) const {
    const auto r = view_.frameRect();
    return x >= r.position.x && y >= r.position.y && x < r.position.x + r.size.x && y < r.position.y + r.size.y;
  }

  scenes_model::Model model_;
  View view_;
  sui::SpriteUiSdlRenderer& sdl_;
  float x_ = 0.f, y_ = 0.f;
  bool pressed_ = false, released_ = false, down_ = false, owned_ = false;
  bool doubleClick_ = false, needsFrame_ = false, dirty_ = false;   // dirty_: texto, tecla, roda ou ponteiro mudou algo
  int backspaces_ = 0;
  bool textInputOn_ = false;   // estado atual de SDL_StartTextInput/SDL_StopTextInput da janela
  std::vector<std::string> texts_;
  std::uint64_t frame_ = 0;
};

}  // namespace editor::scenesui
