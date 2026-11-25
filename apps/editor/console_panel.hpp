#pragma once

// Painel do Console feito com SpriteUI.
//
// O que aparece, filtra e conta vem do modelo (console_model.hpp). Este arquivo faz o layout, a medida de texto,
// as caixas de clique e o desenho. Nada aqui conhece o Editor: o que precisa ser feito fora (limpar, rodar scripts,
// rodar comando, selecionar objeto) volta em Requests.
//
// Como funciona:
//   rebuildList: quebra e mede as linhas so quando o log, a largura ou o filtro mudam (ver Source::serial).
//   buildChrome: cabecalho, botoes, filtros e linha de comando sao posicionados a cada quadro (poucos textos).
//   handleEvent: clique, roda, teclado (so com um campo focado) e X. Clique em botao fica pendente e so e
//                aplicado no draw, onde o log esta disponivel.
//   draw:        aplica o clique pendente, monta os itens e envia o quadro pelo SpriteUiSdlRenderer.
//
// Eventos que caem no painel ficam com ele (handleEvent devolve true). Teclado so e consumido com um campo
// focado; o campo liga o texto do SDL (SDL_StartTextInput) e desliga ao perder o foco.
#include "window_frame.hpp"
#include "window_help_icon.hpp"
#include "text_atlas_set.hpp"
#include "console_model.hpp"
#include "i18n.hpp"
#include "spriteui_host.hpp"

#include "SpriteUi/Layout/LayoutTree.hpp"
#include "SpriteUi/Rendering/DrawList.hpp"
#include "SpriteUi/Rendering/SpriteInstanceGPU.hpp"
#include "SpriteUi/Text/TextLayout.hpp"
#include "SpriteUi/Text/TruetypeAtlas.hpp"
#include "SpriteUi/Widgets/WindowFrame.hpp"
#include "SpriteUi/Widgets/WindowFramePaint.hpp"

#include <optional>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace editor::console_panel {

namespace SU = ToolEngine::SpriteUi;
namespace SR = ToolEngine::SpriteUi::Rendering;
namespace ST = ToolEngine::SpriteUi::Text;
namespace cm = editor::console_model;
namespace SW = ToolEngine::SpriteUi::Widgets;
namespace wf = editor::window_frame;

struct Theme {
  static constexpr float kPad = sui::kPanelPadding;
  static constexpr float kRowH = 30.f;            // barra de botoes, barra de filtros e linha de comando
  static constexpr float kBtnH = 24.f;
  static constexpr float kBtnPad = 12.f;          // espaco de cada lado do rotulo dentro do botao
  static constexpr float kBtnGap = 6.f;
  static constexpr float kFieldH = 24.f;
  static constexpr float kFieldInset = 8.f;
  static constexpr float kListGap = 4.f;
  static constexpr float kListInset = 6.f;        // recuo do texto dentro da lista
  static constexpr float kRowGap = 3.f;           // espaco entre duas linhas do log
  static constexpr float kScrollLane = 12.f;      // reserva a direita da lista para a barra de rolagem
  static constexpr float kScrollW = 4.f;
  static constexpr float kFont = 12.f;
  static constexpr float kSmallFont = 11.f;
  static constexpr float kWheelStep = 48.f;
};


inline SU::Vec4 Srgb(float r, float g, float b, float a = 1.f) {
  return SR::SrgbStraightToLinearPremultiplied({r, g, b, a});
}

struct Colors {
  SU::Vec4 panel = Srgb(0.055f, 0.075f, 0.11f, 0.97f);
  SU::Vec4 text = Srgb(0.91f, 0.918f, 0.933f);
  SU::Vec4 textDim = Srgb(0.56f, 0.565f, 0.60f);
  SU::Vec4 title = Srgb(0.45f, 0.72f, 0.96f);
  SU::Vec4 btn = Srgb(0.13f, 0.16f, 0.21f);
  SU::Vec4 btnHover = Srgb(0.19f, 0.23f, 0.30f);
  SU::Vec4 btnPressed = Srgb(0.09f, 0.11f, 0.15f);
  SU::Vec4 btnOn = Srgb(0.12f, 0.31f, 0.52f);       // Auto-scroll ligado
  SU::Vec4 field = Srgb(0.08f, 0.10f, 0.14f);
  SU::Vec4 fieldFocus = Srgb(0.10f, 0.13f, 0.19f);
  SU::Vec4 selection = Srgb(0.15f, 0.22f, 0.37f, 0.75f);
  SU::Vec4 caret = Srgb(0.91f, 0.918f, 0.933f);
  SU::Vec4 rowHover = Srgb(0.12f, 0.15f, 0.20f);
  SU::Vec4 infoText = Srgb(0.91f, 0.918f, 0.933f);
  SU::Vec4 warnText = Srgb(1.f, 0.85f, 0.3f);
  SU::Vec4 errorText = Srgb(1.f, 0.4f, 0.4f);
  SU::Vec4 scroll = Srgb(0.45f, 0.48f, 0.55f, 0.75f);
};

enum class HAlign : std::uint8_t { Left, Center, Right };
enum class Focus : std::uint8_t { None, Filter, Command };

// Alvo de clique: botoes, chips de nivel, campos e linhas da lista.
enum class Target : std::uint8_t { None, Clear, Check, Copy, Follow, Level, Filter, Command, Run, Row };

struct Hit {
  Target target = Target::None;
  int index = -1;   // Level: grupo; Row: linha da lista
  bool operator==(const Hit& o) const { return target == o.target && index == o.index; }
};

// O que o painel mostra. `serial` muda a cada alteracao do log (o Editor incrementa ed.consoleSerial).
struct Source {
  const cm::LogEntries& entries;
  std::uint64_t serial;
  std::size_t debugErrors;
  std::size_t debugWarnings;
};

// O que o painel pede ao Editor. Preenchido em draw(); o chamador aplica.
struct Requests {
  bool close = false;          // X da moldura (o Editor fecha a janela)
  std::string help;            // "?" da moldura: topico da Help a abrir (vazio = nada)
  bool clear = false;          // limpar o log
  bool checkScripts = false;   // verificar os scripts da cena
  std::string command;         // comando da linha de comando (vazio = nada)
  std::string select;          // nome entre colchetes da linha clicada (vazio = nada)
};

class ConsolePanel {
 public:
  // O atlas e o renderizador SDL sao do editor (mesmos do resto das janelas SpriteUI). O atlas sobe uma vez, na main.
  ConsolePanel(const sui::TextAtlasSet& text, sui::SpriteUiSdlRenderer& sdl) : text_{text}, sdl_{sdl} {
    panel_ = tree_.Create({}, panelStyle());
    // Moldura: mesmas cores do painel (fundo navy) e aba de 30 px.
    frameColors_.body = colors_.panel;
    frameColors_.border = Srgb(0.160f, 0.220f, 0.330f);
    frameColors_.borderDragging = Srgb(0.420f, 0.600f, 0.980f);
    frameColors_.headerActive = Srgb(0.120f, 0.160f, 0.230f);
    frameColors_.headerInactive = Srgb(0.090f, 0.120f, 0.180f);
    frameColors_.headerDragging = Srgb(0.160f, 0.210f, 0.300f);
    frameColors_.glyph = Srgb(0.910f, 0.918f, 0.933f);
  }
  ConsolePanel(const ConsolePanel&) = delete;
  ConsolePanel& operator=(const ConsolePanel&) = delete;

  // Area dada pelo dock (folha, sem a faixa de abas). nullopt: a janela flutua na posicao em que estava.
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

  // Eventos de ponteiro passam antes pelo dock_host, que cuida de foco e docking.
  bool handleEvent(const SDL_Event& e, bool visible) {
    if (!visible) return false;
    switch (e.type) {
      case SDL_EVENT_MOUSE_WHEEL:
        if (contains(e.wheel.mouse_x, e.wheel.mouse_y)) {
          scroll_ = std::clamp(scroll_ - e.wheel.y * Theme::kWheelStep, 0.f, maxScroll_);
          model_.onScrolled(scroll_, maxScroll_);
        }
        return false;
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e.button.button == SDL_BUTTON_LEFT && frameDown(e.button.x, e.button.y)) return true;
        onMouseDown(e);
        return false;
      case SDL_EVENT_MOUSE_BUTTON_UP:
        frame_.pointerUp();
        onMouseUp(e);
        return false;
      case SDL_EVENT_MOUSE_MOTION:
        if (frame_.dragging()) {
          frame_.pointerMove({e.motion.x, e.motion.y});
          return true;
        }
        if (draggingSelection_) moveCaretFromPointer(e.motion.x, e.motion.y, true);
        return false;
      case SDL_EVENT_TEXT_INPUT:
        if (focus_ == Focus::None || !e.text.text) return false;
        insertText(e.text.text);
        return true;
      case SDL_EVENT_KEY_DOWN:
        if (focus_ == Focus::None) return false;
        return keyDown(e.key.key, e.key.mod);
      default:
        return false;
    }
  }

  [[nodiscard]] bool wantsTextCursor(float x, float y) const {
    if (frame_.dragging()) return false;
    if (draggingSelection_) return true;
    const Hit hover = contains(x, y) ? hitTest(x, y) : Hit{};
    return hover.target == Target::Filter || hover.target == Target::Command;
  }

  // Calcula o quadro e desenha. Retorna false so em falha do SDL. Os pedidos do usuario saem em `out`.
  bool draw(SDL_Renderer* renderer, SDL_Window* window, bool visible, const Source& src, Requests& out) {
    if (!visible) {
      resetInteraction();
      return true;
    }
    if (!renderer || !window) return false;
    int winW = 0, winH = 0;
    SDL_GetWindowSize(window, &winW, &winH);
    if (winW <= 0 || winH <= 0) {
      resetInteraction();
      return true;
    }
    window_ = window;

    frame_.placeIn(static_cast<float>(winW), static_cast<float>(winH), dock_);
    const SU::Rect content = frame_.content();
    layoutPanel(sui::Box{content.position.x, content.position.y, content.size.x, content.size.y});
    if (closeRequested_) {
      out.close = true;
      closeRequested_ = false;
      resetInteraction();
    }
    if (helpRequested_) {
      out.help = "console";
      helpRequested_ = false;
    }
    if (pending_.target != Target::None) {
      applyClick(pending_, src.entries, out);
      pending_ = Hit{};
    }
    if (dirty_ || src.serial != builtSerial_ || listW() != builtWidth_) rebuildList(src.entries, src.serial, listW());
    buildChrome(src);
    updateScroll();

    float mx = 0.f, my = 0.f;
    const SDL_MouseButtonFlags mouse = SDL_GetMouseState(&mx, &my);
    const Hit hover = contains(mx, my) ? hitTest(mx, my) : Hit{};
    buildItems(src, hover, (mouse & SDL_BUTTON_LMASK) != 0);
    packet_ = extractor_.Build(++frameId_, items_, nullptr, clips_);
    return sdl_.draw(renderer, packet_);
  }

 private:
  struct Box {
    Target target = Target::None;
    int index = -1;
    float x = 0.f, y = 0.f, w = 0.f, h = 0.f;   // em coordenadas do painel
    std::string label;
    SU::Vec4 textColor{};
    bool on = false;
  };

  // Linha de log ja quebrada e medida. y e a altura na lista, sem rolagem.
  struct Entry {
    float y = 0.f, h = 0.f;
    SU::Vec4 color{};
    std::string name;   // nome entre colchetes; vazio se nao houver
    ST::TextLayoutResult text;
  };

  struct Bounds {
    float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
  };

  static SU::FlexItem panelStyle() {
    SU::FlexItem panel;
    panel.width = {SU::SizeMode::Fixed, 1.f};
    panel.height = {SU::SizeMode::Fixed, 1.f};
    panel.direction = SU::LayoutDirection::Column;
    panel.alignItems = SU::AlignItems::Stretch;
    return panel;
  }

  // ---------- Eventos ----------
  // Clique na moldura: X pede o fechamento (vai em Requests::close no proximo draw); grip inicia o arraste.
  // Devolve true quando o clique foi da moldura, para ele nao chegar ao conteudo.
  bool frameDown(float x, float y) {
    const wf::Event ev = frame_.pointerDown({x, y});
    if (ev == wf::Event::Close) closeRequested_ = true;
    if (ev == wf::Event::Help) helpRequested_ = true;
    return ev != wf::Event::None;
  }

  void onMouseDown(const SDL_Event& e) {
    if (e.button.button != SDL_BUTTON_LEFT) return;
    pressed_ = Hit{};
    const float x = e.button.x, y = e.button.y;
    if (!contains(x, y)) {
      setFocus(Focus::None);
      draggingSelection_ = false;
      return;
    }
    const Hit h = hitTest(x, y);
    setFocus(h.target == Target::Filter ? Focus::Filter : h.target == Target::Command ? Focus::Command : Focus::None);
    pressed_ = h;
    draggingSelection_ = focus_ != Focus::None;
    if (draggingSelection_) moveCaretFromPointer(x, y, false);
  }

  // Botao dispara ao soltar sobre o mesmo alvo; campos so ganham foco no clique (ja tratado acima).
  void onMouseUp(const SDL_Event& e) {
    if (e.button.button != SDL_BUTTON_LEFT) return;
    const Hit h = hitTest(e.button.x, e.button.y);
    const bool isField = h.target == Target::Filter || h.target == Target::Command;
    if (pressed_.target != Target::None && h == pressed_ && !isField) pending_ = h;
    pressed_ = Hit{};
    draggingSelection_ = false;
  }

  void moveCaretFromPointer(float x, float y, bool extend) {
    const Target target = focus_ == Focus::Filter ? Target::Filter : focus_ == Focus::Command ? Target::Command : Target::None;
    const auto box = std::find_if(boxes_.begin(), boxes_.end(), [target](const Box& b) { return b.target == target; });
    if (box == boxes_.end()) return;

    cm::LineEdit& line = focus_ == Focus::Filter ? model_.filter() : model_.command();
    const float innerX = panelBounds_.x + box->x + Theme::kFieldInset;
    const float innerW = std::max(0.f, box->w - 2.f * Theme::kFieldInset);
    const ST::TextLayoutResult layout = layoutOf(line.text(), Theme::kFont);
    const float caretX = ST::GetCaretRect(layout, static_cast<std::uint32_t>(line.caret())).position.x;
    const float shift = std::max(0.f, caretX - innerW + 4.f);
    const float localX = std::max(0.f, x - innerX + shift);
    const float localY = std::clamp(y - (panelBounds_.y + box->y) - (box->h - layout.lineHeight) * 0.5f,
                                    0.f, layout.lineHeight);
    line.moveCaretTo(ST::FindInsertionOffset(layout, {localX, localY}), extend);
  }

  void insertText(const char* utf8) {
    const std::string s(utf8);
    if (focus_ == Focus::Filter) {
      model_.filter().insert(s);
      dirty_ = true;
    } else {
      model_.command().insert(s);
    }
  }

  // Tecla com campo focado: sempre consumida (true). Edicao do filtro marca a lista para refazer.
  bool keyDown(SDL_Keycode key, SDL_Keymod mod) {
    const bool onFilter = focus_ == Focus::Filter;
    cm::LineEdit& line = onFilter ? model_.filter() : model_.command();
    const bool ctrl = (mod & SDL_KMOD_CTRL) != 0;
    const bool shift = (mod & SDL_KMOD_SHIFT) != 0;
    const bool editsText = key == SDLK_BACKSPACE || key == SDLK_DELETE || (ctrl && (key == SDLK_X || key == SDLK_V));
    if (editsText && onFilter) dirty_ = true;
    switch (key) {
      case SDLK_ESCAPE: setFocus(Focus::None); break;
      case SDLK_RETURN:
      case SDLK_KP_ENTER:
        if (!onFilter) pending_ = Hit{Target::Run, -1};
        break;
      case SDLK_BACKSPACE: line.backspace(); break;
      case SDLK_DELETE: line.del(); break;
      case SDLK_A:
        if (ctrl) line.selectAll();
        break;
      case SDLK_LEFT: line.left(shift); break;
      case SDLK_RIGHT: line.right(shift); break;
      case SDLK_HOME: line.home(shift); break;
      case SDLK_END: line.end(shift); break;
      case SDLK_UP:
        if (!onFilter) model_.historyUp();
        break;
      case SDLK_DOWN:
        if (!onFilter) model_.historyDown();
        break;
      case SDLK_C:
        if (ctrl && !line.empty()) {
          const std::string text = line.hasSelection() ? line.selectedText() : line.text();
          SDL_SetClipboardText(text.c_str());
        }
        break;
      case SDLK_X:
        if (ctrl && !line.empty()) {
          if (line.hasSelection()) {
            SDL_SetClipboardText(line.selectedText().c_str());
            line.eraseSelection();
          } else {
            SDL_SetClipboardText(line.text().c_str());
            line.clear();
          }
        }
        break;
      case SDLK_V:
        if (ctrl) pasteFirstLine(line);
        break;
      default:
        break;
    }
    return true;
  }

  // Cola so a primeira linha do clipboard (o campo tem uma linha).
  static void pasteFirstLine(cm::LineEdit& line) {
    char* clip = SDL_GetClipboardText();
    if (!clip) return;
    const std::string text(clip);
    SDL_free(clip);
    line.insert(text.substr(0, text.find('\n')));
  }

  void applyClick(const Hit& h, const cm::LogEntries& entries, Requests& out) {
    switch (h.target) {
      case Target::Clear:
        out.clear = true;
        scroll_ = 0.f;
        model_.setFollow(true);
        break;
      case Target::Check:
        out.checkScripts = true;
        break;
      case Target::Copy:
        SDL_SetClipboardText(model_.copyText(entries).c_str());
        break;
      case Target::Follow:
        model_.setFollow(!model_.follow());
        break;
      case Target::Level:
        model_.toggle(static_cast<cm::Group>(h.index));
        dirty_ = true;
        break;
      case Target::Run:
        out.command = model_.submit();
        break;
      case Target::Row:
        if (h.index >= 0 && h.index < static_cast<int>(entries_.size())) out.select = entries_[static_cast<std::size_t>(h.index)].name;
        break;
      default:
        break;
    }
  }

  // Muda o foco e liga/desliga o texto do SDL (digitacao so chega com ele ligado).
  void setFocus(Focus next) {
    if (next == focus_) return;
    focus_ = next;
    if (next == Focus::None) {
      draggingSelection_ = false;
      if (textWindow_) SDL_StopTextInput(textWindow_);
      textWindow_ = nullptr;
    } else if (!textWindow_ && window_) {
      SDL_StartTextInput(window_);
      textWindow_ = window_;
    }
  }

  void resetInteraction() {
    pressed_ = Hit{};
    pending_ = Hit{};
    draggingSelection_ = false;
    frame_.reset();
    setFocus(Focus::None);
  }

  bool contains(float x, float y) const {
    return x >= panelBounds_.x && y >= panelBounds_.y && x < panelBounds_.x + panelBounds_.w && y < panelBounds_.y + panelBounds_.h;
  }

  bool inside(float bx, float by, float bw, float bh, float x, float y) const {
    return x >= bx && y >= by && x < bx + bw && y < by + bh;
  }

  Hit hitTest(float x, float y) const {
    for (const Box& b : boxes_) {
      if (inside(panelBounds_.x + b.x, panelBounds_.y + b.y, b.w, b.h, x, y)) return Hit{b.target, b.index};
    }
    if (inList(x, y)) {
      const float ry = y - (panelBounds_.y + listTop_) + scroll_;
      for (std::size_t r = 0; r < entries_.size(); ++r) {
        if (ry >= entries_[r].y && ry < entries_[r].y + entries_[r].h) return Hit{Target::Row, static_cast<int>(r)};
      }
    }
    return Hit{};
  }

  bool inList(float x, float y) const {
    return inside(listX(), panelBounds_.y + listTop_, listW(), viewportH_, x, y);
  }

  // ---------- Geometria ----------
  void layoutPanel(sui::Box bounds) {
    panelW_ = std::max(1.f, bounds.w);
    panelH_ = std::max(1.f, bounds.h);
    panelBounds_ = {bounds.x, bounds.y, panelW_, panelH_};
    setSize(panel_, panelW_, panelH_);
    tree_.Layout({{panelBounds_.x, panelBounds_.y}, {panelW_, panelH_}});

    listTop_ = 2.f * Theme::kRowH + Theme::kListGap;
    const float commandY = panelH_ - Theme::kRowH;
    viewportH_ = std::max(0.f, commandY - Theme::kListGap - listTop_);
  }

  float listX() const { return panelBounds_.x + Theme::kPad * 0.5f; }
  float listW() const { return std::max(40.f, panelW_ - Theme::kPad - Theme::kScrollLane); }

  void setSize(SU::UIHandle handle, float width, float height) {
    auto* el = tree_.Get(handle);
    if (!el) return;
    SU::FlexItem style = el->node.style;
    style.width.value = width;
    style.height.value = height;
    tree_.SetStyle(handle, style);
  }

  void updateScroll() {
    maxScroll_ = std::max(0.f, contentH_ - viewportH_);
    if (model_.follow()) scroll_ = maxScroll_;
    scroll_ = std::clamp(scroll_, 0.f, maxScroll_);
  }

  // ---------- Texto: medida, corte e itens ----------
  ST::TextLayoutResult layoutOf(const std::string& utf8, float size) const {
    ST::TextLayoutConfig cfg;
    cfg.fontSize = size;
    cfg.alignment = ST::TextAlignment::Left;
    cfg.wrap = ST::TextWrap::None;
    const ST::TruetypeAtlas& atlas = text_.atlasFor(size);
    const auto glyphs = ST::ShapeTruetype(utf8, atlas);
    return ST::LayoutText(utf8, glyphs, atlas.metrics, cfg);
  }

  // Texto quebrado por palavra em maxW (uma linha do log pode ocupar varias).
  ST::TextLayoutResult wrapped(const std::string& utf8, float size, float maxW) const {
    ST::TextLayoutConfig cfg;
    cfg.fontSize = size;
    cfg.maxWidth = maxW;
    cfg.alignment = ST::TextAlignment::Left;
    cfg.wrap = ST::TextWrap::Word;
    const ST::TruetypeAtlas& atlas = text_.atlasFor(size);
    const auto glyphs = ST::ShapeTruetype(utf8, atlas);
    return ST::LayoutText(utf8, glyphs, atlas.metrics, cfg);
  }

  float textWidth(const std::string& s, float size) const { return layoutOf(s, size).size.x; }

  // Corta com "..." para caber em maxW (uma linha). Nao corta no meio de um caractere UTF-8.
  std::string fit(const std::string& s, float size, float maxW) const {
    const float full = textWidth(s, size);
    if (full <= maxW) return s;
    std::size_t cut = static_cast<std::size_t>(std::floor(static_cast<float>(s.size()) * maxW / std::max(1.f, full)));
    cut = std::min(cut, s.size());
    while (cut > 0) {
      while (cut > 0 && cut < s.size() && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
      const std::string candidate = s.substr(0, cut) + "...";
      if (textWidth(candidate, size) <= maxW) return candidate;
      --cut;
    }
    return "...";
  }

  // Texto de uma linha, centrado verticalmente em yMid. x e a borda (Left), o centro (Center) ou a direita (Right).
  void addText(float x, float yMid, const std::string& s, float size, SU::Vec4 color, float maxW, float z, std::uint32_t clip,
               HAlign align = HAlign::Left) {
    if (s.empty() || maxW <= 0.f) return;
    const ST::TextLayoutResult lay = layoutOf(fit(s, size, maxW), size);
    float left = x;
    if (align == HAlign::Center) left -= lay.size.x * 0.5f;
    if (align == HAlign::Right) left -= lay.size.x;
    appendLayout(lay, {left, yMid - lay.lineHeight * 0.5f}, color, z, clip, text_.sliceFor(size));
  }

  // Item de texto ja medido, na posicao (topo esquerdo). Os glifos recebem paint order em sequencia.
  void appendLayout(const ST::TextLayoutResult& lay, SU::Vec2 pos, SU::Vec4 color, float z, std::uint32_t clip, std::uint32_t slice) {
    SU::UILayoutRect box;
    box.resolvedPosition = pos;
    box.resolvedSize = {lay.size.x, lay.size.y};
    const auto glyphs = ST::GenerateTextRenderItems(lay, box, color, slice, clip, z, paint_, panel_, 0u);
    paint_ += glyphs.size() + 1;
    items_.insert(items_.end(), glyphs.begin(), glyphs.end());
  }

  void addSolid(float x, float y, float w, float h, SU::Vec4 color, float z, std::uint32_t clip) {
    if (w <= 0.f || h <= 0.f) return;
    SR::UIRenderItem item;
    item.handle = panel_;
    item.layout.resolvedPosition = {x, y};
    item.layout.resolvedSize = {w, h};
    item.tintPremultipliedLinear = color;
    item.textureSlice = SR::WhiteTextureSlice;
    item.clipIndex = clip;
    item.zOrder = z;
    item.paintOrder = paint_++;
    items_.push_back(item);
  }

  std::uint32_t addClip(float x, float y, float w, float h) {
    clips_.push_back({x, y, x + w, y + h});
    return static_cast<std::uint32_t>(clips_.size() - 1);
  }

  // ---------- Cabecalho, botoes, filtros e linha de comando (medidos a cada quadro) ----------
  Box& pushBox(Target t, int index, float x, float y, float w, float h) {
    boxes_.push_back(Box{t, index, x, y, w, h, {}, {}, false});
    return boxes_.back();
  }

  // Botao com rotulo. Retorna a posicao x do proximo.
  float addButton(Target t, const char* label, float x, float y, bool on = false) {
    const float w = textWidth(label, Theme::kFont) + 2.f * Theme::kBtnPad;
    Box& b = pushBox(t, -1, x, y, w, Theme::kBtnH);
    b.label = label;
    b.textColor = colors_.text;
    b.on = on;
    return x + w + Theme::kBtnGap;
  }

  void buildChrome(const Source& src) {
    boxes_.clear();
    const float pad = Theme::kPad;
    const float buttonRowY = 0.f;
    const float btnY = buttonRowY + (Theme::kRowH - Theme::kBtnH) * 0.5f;

    float x = addButton(Target::Clear, T("Clear"), pad, btnY);
    x = addButton(Target::Check, T("Check scripts"), x, btnY);
    x = addButton(Target::Copy, T("Copy"), x, btnY);
    x = addButton(Target::Follow, T("Auto-scroll"), x, btnY, model_.follow());
    const float buttonsEnd = x - Theme::kBtnGap;

    visibleCount_ = model_.visibleIndices(src.entries).size();
    rightText_ = F("%zu of %zu", visibleCount_, src.entries.size()) + "   " +
                 F("errors: %zu  warnings: %zu", src.debugErrors, src.debugWarnings);
    rightMaxW_ = std::max(0.f, panelW_ - pad - buttonsEnd - 2.f * Theme::kBtnGap);

    buildFilterRow(src.entries);
    buildCommandRow();
  }

  void buildFilterRow(const cm::LogEntries& entries) {
    const float pad = Theme::kPad;
    const float chipY = Theme::kRowH + (Theme::kRowH - Theme::kBtnH) * 0.5f;
    const cm::Counts counts = cm::ConsoleModel::countAll(entries);
    const char* names[cm::kGroupCount] = {T("Info"), T("Warnings"), T("Errors")};
    const SU::Vec4 colors[cm::kGroupCount] = {colors_.infoText, colors_.warnText, colors_.errorText};

    float x = pad;
    for (int g = 0; g < cm::kGroupCount; ++g) {
      const auto group = static_cast<cm::Group>(g);
      const bool on = model_.enabled(group);
      const std::string label = std::string(names[g]) + " " + std::to_string(counts.of(group));
      const float w = textWidth(label, Theme::kFont) + 2.f * Theme::kBtnPad;
      Box& b = pushBox(Target::Level, g, x, chipY, w, Theme::kBtnH);
      b.label = label;
      b.on = on;
      b.textColor = on ? colors[g] : colors_.textDim;
      x += w + Theme::kBtnGap;
    }
    pushBox(Target::Filter, -1, x, Theme::kRowH + (Theme::kRowH - Theme::kFieldH) * 0.5f,
            std::max(40.f, panelW_ - pad - x), Theme::kFieldH);
  }

  void buildCommandRow() {
    const float pad = Theme::kPad;
    const float rowY = panelH_ - Theme::kRowH;
    const std::string runLabel = T("Run");
    const float runW = textWidth(runLabel, Theme::kFont) + 2.f * Theme::kBtnPad;
    const float runX = panelW_ - pad - runW;
    pushBox(Target::Command, -1, pad, rowY + (Theme::kRowH - Theme::kFieldH) * 0.5f, std::max(40.f, runX - Theme::kBtnGap - pad),
            Theme::kFieldH);
    Box& run = pushBox(Target::Run, -1, runX, rowY + (Theme::kRowH - Theme::kBtnH) * 0.5f, runW, Theme::kBtnH);
    run.label = runLabel;
    run.textColor = colors_.text;
  }

  // ---------- Lista: linhas quebradas, cor por nivel ----------
  SU::Vec4 colorOf(cm::Group g) const {
    if (g == cm::Group::Error) return colors_.errorText;
    if (g == cm::Group::Warn) return colors_.warnText;
    return colors_.infoText;
  }

  void rebuildList(const cm::LogEntries& entries, std::uint64_t serial, float width) {
    entries_.clear();
    const std::vector<std::size_t> visible = model_.visibleIndices(entries);
    const float textW = width - 2.f * Theme::kListInset;
    float y = Theme::kRowGap;
    for (std::size_t i : visible) {
      const auto& [level, msg] = entries[i];
      Entry e;
      e.text = wrapped(msg, Theme::kFont, textW);
      e.h = std::max(e.text.lineHeight, e.text.size.y) + Theme::kRowGap;
      e.y = y;
      e.color = colorOf(cm::groupOf(level));
      e.name = cm::bracketName(msg);
      y += e.h;
      entries_.push_back(std::move(e));
    }
    contentH_ = y + Theme::kRowGap;
    builtSerial_ = serial;
    builtWidth_ = width;
    dirty_ = false;
  }

  // ---------- Moldura: corpo, aba, X (SpriteUI), titulo e grip (escritos aqui, a moldura nao os desenha) ----------
  void addFrame() {
    const SU::Rect frame = frame_.rect();
    // Por tras do conteudo (zOrder -2), como na janela Editor info.
    SW::WindowFrameState state;
    state.active = true;
    state.dragging = frame_.dragging();
    state.docked = frame_.docked();
    auto frameItems = SW::BuildWindowFrameItems(panel_, frame, frame_.metrics(), state, frameColors_, -2.f, paint_);
    paint_ += frameItems.size() + 1;
    items_.insert(items_.end(), frameItems.begin(), frameItems.end());

    const std::uint32_t frameClip = addClip(frame.position.x, frame.position.y, frame.size.x, frame.size.y);
    const SU::Rect title = frame_.titleRect();
    const SU::Rect grip = frame_.gripRect();
    // "?" (circulo com o simbolo) logo depois do grip; o nome vem depois dele, em negrito (duas passadas, 0,7 px).
    const SU::Rect help = frame_.helpRect();
    auto helpItems = window_help_icon::BuildCircleItems(panel_, help, SU::Vec4{0.250f, 0.400f, 0.700f, 1.f}, -1.9f, paint_);
    items_.insert(items_.end(), helpItems.begin(), helpItems.end());
    addText(help.position.x + help.size.x * 0.5f, help.position.y + help.size.y * 0.5f, "?", Theme::kFont, colors_.text, help.size.x, 0.96f,
            frameClip, HAlign::Center);
    const float titleX = frame_.titleTextX();
    const float titleW = title.position.x + title.size.x - titleX;
    addText(titleX, title.position.y + title.size.y * 0.5f, T("Console"), Theme::kFont + 1.f, colors_.text, titleW, 0.95f, frameClip);
    addText(titleX + 0.7f, title.position.y + title.size.y * 0.5f, T("Console"), Theme::kFont + 1.f, colors_.text, titleW, 0.95f, frameClip);
    // Grip: 2 colunas x 3 pontos, no centro do icone.
    const float cx = grip.position.x + grip.size.x * 0.5f, cy = grip.position.y + grip.size.y * 0.5f;
    for (int c = 0; c < 2; ++c) {
      for (int r = 0; r < 3; ++r) {
        addSolid(cx - 3.f + 4.f * static_cast<float>(c), cy - 4.f + 4.f * static_cast<float>(r), 2.f, 2.f, colors_.textDim,
                 0.95f, frameClip);
      }
    }
  }

  // ---------- Quadro ----------
  void buildItems(const Source& src, const Hit& hover, bool down) {
    items_.clear();
    clips_.clear();
    paint_ = 1;
    addFrame();
    const float px = panelBounds_.x, py = panelBounds_.y;
    const std::uint32_t panelClip = addClip(px, py, panelW_, panelH_);
    addSolid(px, py, panelW_, panelH_, colors_.panel, 0.f, panelClip);
    addText(px + panelW_ - Theme::kPad, py + Theme::kRowH * 0.5f, rightText_, Theme::kSmallFont, colors_.textDim,
            rightMaxW_, 0.3f, panelClip, HAlign::Right);

    for (const Box& b : boxes_) {
      if (b.target == Target::Filter || b.target == Target::Command) {
        addField(b, panelClip);
      } else {
        addBox(b, hover, down, panelClip);
      }
    }
    addList(src, hover);
  }

  void addBox(const Box& b, const Hit& hover, bool down, std::uint32_t clip) {
    const bool hot = hover.target == b.target && hover.index == b.index;
    const bool pressed = hot && down;
    const float x = panelBounds_.x + b.x, y = panelBounds_.y + b.y;
    addSolid(x, y, b.w, b.h, backgroundOf(b, hot, pressed), 0.2f, clip);
    addText(x + b.w * 0.5f, y + b.h * 0.5f, b.label, Theme::kFont, b.textColor, b.w, 0.3f, clip, HAlign::Center);
  }

  SU::Vec4 backgroundOf(const Box& b, bool hot, bool pressed) const {
    if (pressed) return colors_.btnPressed;
    if (b.target == Target::Follow && b.on) return colors_.btnOn;
    if (b.target == Target::Level && !b.on) return hot ? colors_.btn : colors_.panel;
    return hot ? colors_.btnHover : colors_.btn;
  }

  // Campo de texto: fundo, texto (ou dica quando vazio e sem foco), caret quando tem foco. O texto rola para o caret.
  void addField(const Box& b, std::uint32_t panelClip) {
    const bool isFilter = b.target == Target::Filter;
    const cm::LineEdit& line = isFilter ? model_.filter() : model_.command();
    const bool focused = focus_ == (isFilter ? Focus::Filter : Focus::Command);
    const float x = panelBounds_.x + b.x, y = panelBounds_.y + b.y;
    addSolid(x, y, b.w, b.h, focused ? colors_.fieldFocus : colors_.field, 0.15f, panelClip);
    const float innerX = x + Theme::kFieldInset;
    const float innerW = std::max(0.f, b.w - 2.f * Theme::kFieldInset);
    const std::uint32_t clip = addClip(x, y, b.w, b.h);
    const float midY = y + b.h * 0.5f;

    if (line.empty() && !focused) {
      const char* hint = isFilter ? T("filter (e.g. object name or TOOL-S007)") : "test ECS: 10000   (help)";
      addText(innerX, midY, hint, Theme::kFont, colors_.textDim, innerW, 0.35f, clip);
      return;
    }
    const ST::TextLayoutResult layout = layoutOf(line.text(), Theme::kFont);
    const float caretX = ST::GetCaretRect(layout, static_cast<std::uint32_t>(line.caret())).position.x;
    const float shift = std::max(0.f, caretX - innerW + 4.f);
    if (line.hasSelection()) {
      const auto [begin, end] = line.selectionRange();
      const float firstX = ST::GetCaretRect(layout, static_cast<std::uint32_t>(begin)).position.x;
      const float lastX = ST::GetCaretRect(layout, static_cast<std::uint32_t>(end)).position.x;
      const float top = y + (b.h - layout.lineHeight) * 0.5f;
      const float left = innerX + std::min(firstX, lastX) - shift;
      const float width = std::abs(lastX - firstX);
      if (width > 0.f) addSolid(left, top, width, layout.lineHeight, colors_.selection, 0.25f, clip);
    }
    addText(innerX - shift, midY, line.text(), Theme::kFont, colors_.text, 1e6f, 0.35f, clip);
    if (focused) addSolid(innerX + caretX - shift, y + 4.f, 1.f, b.h - 8.f, colors_.caret, 0.4f, clip);
  }

  void addList(const Source& src, const Hit& hover) {
    const float lx = listX(), ly = panelBounds_.y + listTop_;
    const float lw = listW();
    const std::uint32_t clip = addClip(lx, ly, lw, viewportH_);
    if (src.entries.empty()) {
      addText(lx + Theme::kListInset, ly + 6.f, T("No messages yet."), Theme::kFont, colors_.textDim, lw, 0.3f, clip);
    } else if (entries_.empty()) {
      addText(lx + Theme::kListInset, ly + 6.f, T("No messages match the filter."), Theme::kFont, colors_.textDim, lw, 0.3f, clip);
    }
    for (std::size_t r = 0; r < entries_.size(); ++r) {
      const Entry& e = entries_[r];
      if (!rowVisible(e)) continue;
      const float sy = ly + e.y - scroll_;
      if (hover.target == Target::Row && hover.index == static_cast<int>(r)) addSolid(lx, sy, lw, e.h, colors_.rowHover, 0.1f, clip);
    }
    for (const Entry& e : entries_) {
      if (!rowVisible(e)) continue;
      appendLayout(e.text, {lx + Theme::kListInset, ly + e.y - scroll_}, e.color, 0.3f, clip, text_.sliceFor(Theme::kFont));
    }
    addScrollThumb(lx, ly, lw, clip);
  }

  bool rowVisible(const Entry& e) const {
    const float top = e.y - scroll_;
    return top + e.h >= 0.f && top <= viewportH_;
  }

  void addScrollThumb(float lx, float ly, float lw, std::uint32_t clip) {
    if (contentH_ <= viewportH_ + 0.5f || viewportH_ <= 0.f) return;
    const float trackH = std::max(1.f, viewportH_ - 8.f);
    const float thumbH = std::min(trackH, std::max(24.f, trackH * viewportH_ / contentH_));
    const float thumbY = ly + 4.f + (maxScroll_ > 0.f ? scroll_ / maxScroll_ * (trackH - thumbH) : 0.f);
    addSolid(lx + lw - Theme::kScrollW - 2.f, thumbY, Theme::kScrollW, thumbH, colors_.scroll, 0.4f, clip);
  }

  const sui::TextAtlasSet& text_;
  Colors colors_;
  cm::ConsoleModel model_;
  sui::SpriteUiSdlRenderer& sdl_;

  SU::LayoutTree tree_;
  SU::UIHandle panel_{};
  SR::RenderExtractor extractor_;
  SR::FramePacket packet_;
  std::uint64_t frameId_ = 0;

  // Desenho do quadro (reconstruido a cada draw)
  std::vector<SR::UIRenderItem> items_;
  std::vector<SR::ClipRectGPU> clips_;
  std::uint64_t paint_ = 1;

  // Cabecalho, botoes e campos (medidos a cada quadro)
  std::vector<Box> boxes_;
  std::string rightText_;
  float rightMaxW_ = 0.f;
  std::size_t visibleCount_ = 0;

  // Lista (reconstruida so quando muda)
  std::vector<Entry> entries_;
  float contentH_ = 0.f, viewportH_ = 0.f, listTop_ = 0.f, maxScroll_ = 0.f, scroll_ = 0.f;
  std::uint64_t builtSerial_ = ~0ull;
  float builtWidth_ = -1.f;
  bool dirty_ = true;

  // Janela e interacao
  Bounds panelBounds_{};
  float panelW_ = 0.f, panelH_ = 0.f;
  SDL_Window* window_ = nullptr;
  SDL_Window* textWindow_ = nullptr;   // janela em que o texto do SDL foi ligado (nullptr = desligado)
  Hit pressed_{}, pending_{};
  Focus focus_ = Focus::None;
  bool draggingSelection_ = false;

  // Moldura flutuante (titulo, grip, X). A geometria e as regras ficam em window_frame.hpp.
  wf::Frame frame_;
  std::optional<SU::Rect> dock_;
  SW::WindowFrameColors frameColors_{};
  bool closeRequested_ = false;
  bool helpRequested_ = false;   // "?" clicado: o proximo draw pede a Help
};

}  // namespace editor::console_panel
