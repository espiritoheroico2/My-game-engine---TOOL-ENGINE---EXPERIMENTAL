#pragma once

// Barra de menus principal feita com SpriteUI. Substitui o BeginMainMenuBar do ImGui (ver drawMenu em main.cpp).
// Os menus, os atalhos e as marcas vem do mainmenu_model.hpp. O elemento MenuBar da SpriteUI faz a geometria
// dos menus, o mouse, o Alt e as setas. Este arquivo so monta os textos, os botoes Play/Pausa e envia o quadro.
// Nada aqui conhece o Editor: o que o usuario pediu sai em `out` (ver applyMenuRequest em main.cpp).
//
// Como funciona:
//   setState:    guarda o estado do Editor. Os menus so sao refeitos quando o estado muda.
//   handleEvent: clique, movimento e teclas. Clique fora dos menus fecha a barra e segue para a interface.
//   draw:        arruma o elemento, monta os textos (cabecalhos, botoes, linhas abertas) e envia o quadro.
//
// Limites da biblioteca usados aqui: o MenuBar nao tem item marcado, entao a marca vai no rotulo ("[x] ").
// Os botoes Play e Pausa nao sao cabecalhos: sao desenhados aqui, com as mesmas posicoes do ImGui.

#include "mainmenu_model.hpp"
#include "sui_panel.hpp"

#include "SpriteUi/Elements/ElementTree.hpp"
#include "SpriteUi/Elements/MenuBar.hpp"
#include "SpriteUi/Widgets/Menu.hpp"

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace editor::mainmenu {

namespace SU = ToolEngine::SpriteUi;
namespace SE = ToolEngine::SpriteUi::Elements;
namespace SW = ToolEngine::SpriteUi::Widgets;
namespace SR = ToolEngine::SpriteUi::Rendering;
namespace ST = ToolEngine::SpriteUi::Text;

class MainMenuPanel {
 public:
  MainMenuPanel(const ST::TruetypeAtlas& atlas, sui::SpriteUiSdlRenderer& sdl) : canvas_{atlas}, sdl_{sdl} {
    style_ = makeStyle(theme_);
    barId_ = tree_.Create<SE::MenuBar>(ToolEngine::SpriteUi::UIHandle{});
    bar().popupWidth = 260.f;
    bar().headerWidth = 84.f;
    bar().rowHeight = 24.f;
  }
  MainMenuPanel(const MainMenuPanel&) = delete;
  MainMenuPanel& operator=(const MainMenuPanel&) = delete;

  // Estado e idioma para o proximo quadro (chamar antes de draw).
  void setState(const MenuState& state, Translate tr) {
    state_ = state;
    tr_ = tr;
  }

  // Mouse e teclado da barra. true quando a barra (faixa ou menu aberto) ficou com o evento.
  bool handleEvent(const SDL_Event& e) {
    switch (e.type) {
      case SDL_EVENT_MOUSE_BUTTON_DOWN: return onPress(e.button.button, e.button.x, e.button.y);
      case SDL_EVENT_MOUSE_BUTTON_UP: return onRelease(e.button.button, e.button.x, e.button.y);
      case SDL_EVENT_MOUSE_MOTION: return onMove(e.motion.x, e.motion.y);
      case SDL_EVENT_KEY_DOWN: return onKey(e.key.key, e.key.repeat);
      default: return false;
    }
  }

  // Desenha a faixa de `barHeight` px no topo e os menus abertos por cima. Os pedidos do usuario saem em `out`.
  bool draw(SDL_Renderer* renderer, SDL_Window* window, float barHeight, std::vector<Request>& out) {
    out.insert(out.end(), queue_.begin(), queue_.end());
    queue_.clear();
    if (!renderer || !window) return false;
    int ww = 0, wh = 0;
    if (!SDL_GetWindowSize(window, &ww, &wh)) return false;
    if (ww <= 0 || wh <= 0) return true;   // janela minimizada

    barHeight_ = barHeight;
    windowRect_ = ToolEngine::SpriteUi::Rect{{0.f, 0.f}, {static_cast<float>(ww), static_cast<float>(wh)}};
    syncMenus();
    stretchBar();
    tree_.Arrange(barId_, ToolEngine::SpriteUi::Rect{{0.f, 0.f}, {windowRect_.size.x, barHeight_}});
    bar().Layout(windowRect_);
    buildCanvas(windowRect_.size.x);

    std::vector<SR::UIRenderItem> items = SE::BuildMenuBarItems(bar(), style_, 0u, 0.f, 1u);
    std::uint64_t paint = items.empty() ? 1u : items.back().paintOrder + 1u;
    canvas_.appendItems(items, 0.f, 0.f, 0.f, 0.f, windowRect_.size.y, bar().handle, 0u, paint);
    const std::vector<SR::ClipRectGPU> clips{{0.f, 0.f, windowRect_.size.x, windowRect_.size.y}};
    packet_ = extractor_.Build(++frameId_, items, nullptr, clips);
    if (!SR::IsFramePacketValid(packet_)) return true;
    return sdl_.draw(renderer, packet_);
  }

 private:
  struct Button {
    sui::Box box;
    Command command = Command::None;
  };

  static constexpr float kFontSize = 13.f;
  static constexpr float kHeaderPad = 10.f;
  static constexpr float kRowPad = 12.f;
  static constexpr float kRowGap = 24.f;
  static constexpr float kButtonPad = 10.f;
  static constexpr float kButtonGap = 8.f;
  static constexpr float kButtonInset = 2.f;
  static constexpr float kButtonsShift = 30.f;   // Play fica um pouco a esquerda do meio, como no ImGui

  static SE::MenuBarStyle makeStyle(const sui::Theme& theme) {
    SE::MenuBarStyle style;
    // Faixa e popups opacos: a janela ImGui que fica embaixo nao deve aparecer atras do texto.
    style.bar = sui::Srgb(0.055f, 0.075f, 0.11f);
    style.header = style.bar;
    style.headerOpen = sui::Srgb(0.16f, 0.2f, 0.28f);
    style.popup = sui::Srgb(0.07f, 0.09f, 0.12f);
    style.popupBorder = theme.divider;
    style.highlight = sui::Srgb(0.12f, 0.3f, 0.6f);
    style.separator = theme.divider;
    return style;
  }

  SE::MenuBar& bar() { return *tree_.GetAs<SE::MenuBar>(barId_); }

  // A faixa ocupa a largura toda. Por padrao o MenuBar tem 100 px, e so os primeiros cabecalhos respondiam ao mouse.
  void stretchBar() {
    SE::Element& element = bar();
    element.transform.anchorMin = SU::Vec2{0.f, 0.f};
    element.transform.anchorMax = SU::Vec2{1.f, 0.f};
    element.transform.offsetMin = SU::Vec2{0.f, 0.f};
    element.transform.offsetMax = SU::Vec2{0.f, barHeight_};
  }

  // So refaz os menus (SetMenus fecha o menu aberto) quando o estado ou o idioma mudou.
  void syncMenus() {
    if (haveBuilt_ && state_ == built_) return;
    bar().control.SetMenus(toBarMenus(buildMenus(state_, tr_)));
    built_ = state_;
    haveBuilt_ = true;
  }

  static std::vector<SW::MenuBarEntry> toBarMenus(const std::vector<MenuSpec>& specs) {
    std::vector<SW::MenuBarEntry> out;
    for (const MenuSpec& spec : specs) {
      SW::MenuBarEntry entry;
      entry.label = spec.title;
      for (const ItemSpec& item : spec.items) {
        SW::MenuItem menuItem;
        menuItem.label = item.label;
        menuItem.shortcut = item.shortcut;
        menuItem.kind = item.separator ? SW::MenuItemKind::Separator : SW::MenuItemKind::Action;
        menuItem.disabled = item.disabled;
        entry.items.push_back(std::move(menuItem));
      }
      out.push_back(std::move(entry));
    }
    return out;
  }

  // Textos e botoes em coordenadas da janela. Os textos sao medidos aqui, a cada quadro.
  void buildCanvas(float windowW) {
    canvas_.clear();
    const float lineH = canvas_.lineHeight(kFontSize);
    const std::size_t menuCount = bar().control.Menus().size();
    for (std::size_t i = 0; i < menuCount; ++i) addHeader(i, lineH);
    addButtons(windowW, lineH);
    if (!bar().control.IsOpen()) return;
    for (std::size_t depth = 0; depth < bar().control.Dropdown().Depth(); ++depth) addRows(depth, lineH);
  }

  void addHeader(std::size_t index, float lineH) {
    const auto header = bar().HeaderRect(index);
    const float y = header.position.y + (header.size.y - lineH) * 0.5f;
    canvas_.addText(header.position.x + kHeaderPad, y, bar().control.Menus()[index].label, kFontSize, theme_.text,
                    header.size.x - 2.f * kHeaderPad);
  }

  void addButtons(float windowW, float lineH) {
    buttons_.clear();
    float x = windowW * 0.5f - kButtonsShift;
    for (const BarButton& button : barButtons(state_, tr_)) {
      const float textW = canvas_.layout(button.label, kFontSize).size.x;
      const float w = textW + 2.f * kButtonPad;
      const sui::Box box{x, kButtonInset, w, barHeight_ - 2.f * kButtonInset};
      canvas_.addSolid(box.x, box.y, box.w, box.h, theme_.divider, 0.2f);
      canvas_.addText(box.x + kButtonPad, box.y + (box.h - lineH) * 0.5f, button.label, kFontSize, theme_.text, w);
      buttons_.push_back(Button{box, button.command});
      x += w + kButtonGap;
    }
  }

  // Linhas de um nivel aberto: rotulo a esquerda, atalho a direita (cinza). Separadores nao tem texto.
  void addRows(std::size_t depth, float lineH) {
    const std::vector<SW::MenuItem>& items = bar().control.Dropdown().Items(depth);
    for (std::size_t i = 0; i < items.size(); ++i) {
      const auto row = bar().RowRect(depth, i);
      if (items[i].kind == SW::MenuItemKind::Separator || row.size.y <= 0.f) continue;
      const float y = row.position.y + (row.size.y - lineH) * 0.5f;
      const float shortcutW = items[i].shortcut.empty() ? 0.f : canvas_.layout(items[i].shortcut, kFontSize).size.x + kRowGap;
      const SU::Vec4 color = items[i].disabled ? theme_.textDim : theme_.text;
      canvas_.addText(row.position.x + kRowPad, y, items[i].label, kFontSize, color, row.size.x - 2.f * kRowPad - shortcutW);
      if (shortcutW > 0.f) {
        canvas_.addText(row.position.x + row.size.x - kRowPad, y, items[i].shortcut, kFontSize, theme_.textDim, shortcutW, true);
      }
    }
  }

  // Botao (Play, Pausa...) embaixo do ponteiro, ou nullptr.
  [[nodiscard]] const Button* buttonAt(float x, float y) const {
    for (const Button& button : buttons_) {
      if (button.box.contains(x, y)) return &button;
    }
    return nullptr;
  }

  // Ponto dentro da faixa ou de um popup aberto (o elemento MenuBar so "ve" esses dois lugares).
  [[nodiscard]] bool insideBar(float x, float y) {
    if (y >= 0.f && y < barHeight_) return true;
    if (!bar().control.IsOpen()) return false;
    for (std::size_t depth = 0; depth < bar().control.Dropdown().Depth(); ++depth) {
      const auto popup = bar().PopupRect(depth);
      if (x >= popup.position.x && x < popup.position.x + popup.size.x && y >= popup.position.y &&
          y < popup.position.y + popup.size.y) {
        return true;
      }
    }
    return false;
  }

  void sendPointer(SE::PointerPhase phase, float x, float y) {
    const SE::PointerEvent event{phase, {x, y}, 0u, insideBar(x, y)};
    const SE::MenuBarEvents events = bar().OnPointer(event, windowRect_);
    if (events.activated) queueActivation();
    if (events.closed) barActive_ = false;
  }

  // Item escolhido: o caminho tem um indice por nivel; so ha um nivel (a barra nao tem submenus).
  void queueActivation() {
    const std::vector<std::size_t>& path = bar().control.ActivationPath();
    if (path.size() != 1) return;
    const Request request = requestAt(bar().control.ActivatedMenu(), path[0]);
    if (request.command != Command::None) queue_.push_back(request);
  }

  bool onPress(std::uint8_t button, float x, float y) {
    if (button != SDL_BUTTON_LEFT) return false;
    if (const Button* hit = buttonAt(x, y)) {
      queue_.push_back(Request{hit->command, 0});
      return true;
    }
    if (!insideBar(x, y)) {
      // Clique fora: fecha os menus, mas o clique segue para a interface (o ImGui pode usa-lo).
      if (bar().control.IsOpen()) bar().control.Close();
      barActive_ = false;
      return false;
    }
    pressed_ = true;
    sendPointer(SE::PointerPhase::Down, x, y);
    return true;
  }

  bool onRelease(std::uint8_t button, float x, float y) {
    if (button != SDL_BUTTON_LEFT || !pressed_) return false;
    pressed_ = false;
    sendPointer(SE::PointerPhase::Up, x, y);
    return true;
  }

  bool onMove(float x, float y) {
    if (!bar().control.IsOpen()) return false;
    sendPointer(SE::PointerPhase::Move, x, y);
    return bar().control.IsOpen();
  }

  // Alt liga e desliga a barra (como no Windows). Teclas de menu so sao consumidas com a barra ativa ou aberta.
  bool onKey(SDL_Keycode key, bool repeat) {
    if (key == SDLK_LALT || key == SDLK_RALT) {
      if (!repeat) {
        const bool wasActive = barActive_ || bar().control.IsOpen();
        const SE::MenuBarEvents events = bar().PressAlt();
        barActive_ = !wasActive && !events.closed;
      }
      return false;
    }
    const std::optional<SW::MenuKey> menuKey = menuKeyOf(key);
    if (!menuKey || !(barActive_ || bar().control.IsOpen())) return false;
    const SE::MenuBarEvents events = bar().KeyPressed(*menuKey, windowRect_);
    if (events.activated) queueActivation();
    if (events.closed) barActive_ = false;
    return true;
  }

  static std::optional<SW::MenuKey> menuKeyOf(SDL_Keycode key) {
    switch (key) {
      case SDLK_UP: return SW::MenuKey::Up;
      case SDLK_DOWN: return SW::MenuKey::Down;
      case SDLK_LEFT: return SW::MenuKey::Left;
      case SDLK_RIGHT: return SW::MenuKey::Right;
      case SDLK_HOME: return SW::MenuKey::Home;
      case SDLK_END: return SW::MenuKey::End;
      case SDLK_RETURN:
      case SDLK_KP_ENTER: return SW::MenuKey::Enter;
      case SDLK_ESCAPE: return SW::MenuKey::Escape;
      default: return std::nullopt;
    }
  }

  sui::Canvas canvas_;
  sui::SpriteUiSdlRenderer& sdl_;
  sui::Theme theme_;
  SE::MenuBarStyle style_;
  SE::ElementTree tree_;
  ToolEngine::SpriteUi::UIHandle barId_{};

  MenuState state_;
  MenuState built_;
  bool haveBuilt_ = false;
  Translate tr_ = [](const char* s) -> const char* { return s; };

  ToolEngine::SpriteUi::Rect windowRect_{};
  float barHeight_ = 0.f;
  bool pressed_ = false;     // o Down comecou dentro da barra e espera o Up
  bool barActive_ = false;   // Alt ativou a barra, sem menu aberto ainda

  std::vector<Request> queue_;   // pedidos dos eventos, entregues no draw
  std::vector<Button> buttons_;

  SR::RenderExtractor extractor_;
  SR::FramePacket packet_;
  std::uint64_t frameId_ = 0;
};

}  // namespace editor::mainmenu
