#pragma once

// Janela Help com SpriteUI.
// Assunto, busca e blocos do texto: help_model.hpp. Aqui: geometria, desenho e cliques. A janela usa a moldura
// padrao do editor (framed::Chrome: titulo, "?", X, grip e dock) e desenha no mesmo SDL_Renderer das outras.
//
// Teclado: com o foco na busca, o painel recebe as teclas e o editor nao as recebe (W/E/R e Delete nao agem).
// Esc tira o foco da busca. Fora da busca, as teclas seguem para o editor.

#include "framed_window.hpp"
#include "help_model.hpp"
#include "i18n.hpp"
#include "sui_panel.hpp"
#include "theme_colors.hpp"
#include "ui_scrollbar.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace editor::help {

namespace SU = ToolEngine::SpriteUi;
namespace SR = ToolEngine::SpriteUi::Rendering;
namespace ST = ToolEngine::SpriteUi::Text;
namespace Edit = ToolEngine::SpriteUi::Widgets;

using sui::Box;

// Medidas (px). Tamanhos fixos, como no Profiler SpriteUI.
inline constexpr float kStartWidth = 820.f;    // tamanho da moldura na primeira abertura
inline constexpr float kStartHeight = 520.f;
inline constexpr float kPad = sui::kPanelPadding;
inline constexpr float kGap = 8.f;
inline constexpr float kListW = 230.f;
inline constexpr float kSearchH = 28.f;
inline constexpr float kFieldPad = 8.f;
inline constexpr float kRowH = 24.f;
inline constexpr float kRowInset = 10.f;
inline constexpr float kGroupH = 26.f;
inline constexpr float kFont = 12.f;
inline constexpr float kSmallFont = 12.f;
inline constexpr float kCodeFont = 13.f;
inline constexpr float kTitleFont = 12.f;   // titulo do assunto no corpo
inline constexpr float kHeadingFont = 16.f;
inline constexpr float kCodePad = 6.f;
inline constexpr float kCodeInset = 8.f;
inline constexpr float kBulletIndent = 14.f;
inline constexpr float kButtonH = 22.f;
inline constexpr float kButtonPad = 10.f;
inline constexpr float kScrollW = 6.f;
inline constexpr float kWheelStep = 40.f;

// Cores que a janela Help usa alem das do sui::Theme.
struct Colors {
  SU::Vec4 selection, heading, codeBackground, codeText, button, field, fieldFocus, caret, scroll;
  Colors() {   // da paleta em uso (theme_colors.hpp); refeito a cada quadro em draw()
    const editor::theme_colors::Colors t = editor::theme_colors::Active();
    selection = sui::PaletteColor(editor::theme_colors::WithAlpha(t.selection, 0.85f));
    heading = sui::PaletteColor(t.heading);
    codeBackground = sui::PaletteColor(t.field);
    codeText = sui::PaletteColor(t.text);
    button = sui::PaletteColor(t.btn);
    field = sui::PaletteColor(t.field);
    fieldFocus = sui::PaletteColor(t.fieldFocus);
    caret = sui::PaletteColor(t.caret);
    scroll = sui::PaletteColor(t.scroll);
  }
};

// Janela Help: lista de assuntos a esquerda (com busca), texto do assunto a direita e botoes para copiar codigo. Reconstroi o desenho so quando algo muda (dirty_).
class HelpPanel {
 public:
  HelpPanel(const sui::TextAtlasSet& text, sui::SpriteUiSdlRenderer& sdl)
      : text_{text}, sdl_{sdl}, frame_{text, sdl}, list_{text}, body_{text}, hint_{text} {}
  HelpPanel(const HelpPanel&) = delete;
  HelpPanel& operator=(const HelpPanel&) = delete;

  // Moldura padrao (titulo, "?", X, grip, dock). O host registra no dock e le consumeClose()/consumeHelp().
  framed::Chrome& chrome() { return frame_; }

  // Assunto novo (icone "?" ou F1): a janela fica no lugar e o texto volta ao topo.
  void openTopic(const std::string& id) {
    model_.Open(id);
    bodyScroll_ = 0.f;
    dirty_ = true;
  }

  // Chamar para cada evento do SDL. Retorna true quando o evento e da janela.
  // `open` = ed.showHelp: o X fecha a janela.
  bool handleEvent(const SDL_Event& e, SDL_Window* window, bool& open) {
    if (!open || !frame_.visible()) {
      setSearchFocus(false, window);
      open_ = false;
      return false;
    }
    if (frame_.handleEvent(e)) return true;   // X, "?" e minimizar da moldura
    switch (e.type) {
      case SDL_EVENT_TEXT_INPUT:
        if (!model_.SearchFocused()) return false;
        model_.InsertSearchText(e.text.text);
        dirty_ = true;
        return true;
      case SDL_EVENT_KEY_DOWN:
        if (!model_.SearchFocused()) return false;
        keyInSearch(e.key.key, static_cast<SDL_Keymod>(e.key.mod), window);
        return true;
      case SDL_EVENT_MOUSE_WHEEL:
        if (!panel_.contains(e.wheel.mouse_x, e.wheel.mouse_y)) return false;
        wheel(e.wheel.mouse_x, e.wheel.mouse_y, e.wheel.y);
        return true;
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e.button.button != SDL_BUTTON_LEFT || !panel_.contains(e.button.x, e.button.y)) return false;
        if (listBar_.pointerDown(e.button.x, e.button.y, listScroll_) || bodyBar_.pointerDown(e.button.x, e.button.y, bodyScroll_)) return true;
        click(e.button.x, e.button.y, window);
        return true;
      case SDL_EVENT_MOUSE_MOTION:
        listBar_.pointerMove(e.motion.x, e.motion.y, listScroll_);
        bodyBar_.pointerMove(e.motion.x, e.motion.y, bodyScroll_);
        if (listBar_.dragging() || bodyBar_.dragging()) return true;   // o arraste segue com o mouse fora da barra
        if (!model_.Search().DraggingSelection()) return false;
        moveSearchCaret(e.motion.x, e.motion.y);
        dirty_ = true;
        return true;
      case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e.button.button == SDL_BUTTON_LEFT) {
          const bool wasBar = listBar_.pointerUp() | bodyBar_.pointerUp();
          if (wasBar) return true;
        }
        if (e.button.button != SDL_BUTTON_LEFT || !model_.Search().DraggingSelection()) return false;
        model_.Search().PointerUp();
        return true;
      default:
        return false;
    }
  }

  // true se o mouse esta sobre a caixa de busca (o host troca o cursor para texto).
  [[nodiscard]] bool wantsTextCursor(float x, float y) const {
    return open_ && searchBox_.contains(x, y);
  }

  // Desenha a moldura e o corpo quando `open` (e visivel no dock). Retorna false so em falha do SDL.
  bool draw(SDL_Renderer* renderer, SDL_Window* window, bool open, const std::string& language) {
    theme_ = sui::Theme{};   // cores da paleta em uso (Configuracoes > Appearance)
    colors_ = Colors{};
    if (const unsigned long long sig = editor::theme_colors::Signature(); sig != themeSig_) {   // tema novo: refaz o desenho guardado
      themeSig_ = sig;
      dirty_ = true;
    }
    if (model_.Language() != language) {
      model_.SetLanguage(language);
      dirty_ = true;
    }
    open_ = open;
    if (!open) {
      frame_.hide();
      setSearchFocus(false, window);
      return true;
    }
    if (!renderer || !window) return false;
    int ww = 0, wh = 0;
    if (!SDL_GetWindowSize(window, &ww, &wh)) return false;
    if (ww <= 0 || wh <= 0) return true;   // janela minimizada: nada a desenhar
    const Box body = frame_.prepare(static_cast<float>(ww), static_cast<float>(wh), kStartWidth, kStartHeight);
    if (!frame_.draw(renderer, T("Help"))) return false;
    if (body.w <= 0.f || body.h <= 0.f) return true;   // minimizada: so o cabecalho
    if (updateGeometry(body)) dirty_ = true;
    if (dirty_) rebuild();
    listScroll_ = std::clamp(listScroll_, 0.f, listMax_);
    bodyScroll_ = std::clamp(bodyScroll_, 0.f, bodyMax_);
    return drawPacket(renderer);
  }

 private:
  // Linha da lista de assuntos.
  struct Row {
    Box box;               // em coordenadas do conteudo da lista (antes da rolagem)
    std::size_t topic;     // indice em helpTopics()
  };
  // Botao "copiar" de um bloco de codigo no texto.
  struct CopyButton {
    Box box;               // em coordenadas do conteudo do texto (antes da rolagem)
    std::string code;      // texto copiado para a area de transferencia
  };

  // Estilo do campo de busca a partir das cores do tema.
  [[nodiscard]] Edit::TextFieldStyle searchStyle() const {
    Edit::TextFieldStyle style;
    style.background = colors_.field;
    style.focusBackground = colors_.fieldFocus;
    style.text = theme_.text;
    style.selection = colors_.selection;
    style.caret = colors_.caret;
    style.paddingX = kFieldPad;
    style.height = kSearchH;
    style.fontSize = kFont;
    return style;
  }

  // Calcula as caixas internas para o corpo da moldura neste quadro. Retorna true se mudou.
  bool updateGeometry(const Box& body) {
    if (body.x == panel_.x && body.y == panel_.y && body.w == panel_.w && body.h == panel_.h) return false;
    panel_ = body;
    const float top = panel_.y + kPad;
    const float contentH = std::max(0.f, panel_.h - 2.f * kPad);
    const float left = panel_.x + kPad;
    const float listW = std::min(kListW, std::max(0.f, panel_.w * 0.4f));
    searchBox_ = {left, top, listW, kSearchH};
    listBox_ = {left, top + kSearchH + kPad, listW, std::max(0.f, contentH - kSearchH - kPad)};
    bodyBox_ = {left + listW + kPad, top, std::max(0.f, panel_.w - listW - 3.f * kPad), contentH};
    return true;
  }

  // Monta o texto de todas as partes. So roda quando algo mudou (busca, assunto, idioma ou tamanho).
  void rebuild() {
    list_.clear();
    body_.clear();
    hint_.clear();
    rows_.clear();
    copyButtons_.clear();
    buildList();
    buildBody();
    buildHint();
    listMax_ = std::max(0.f, listContentH_ - listBox_.h);
    bodyMax_ = std::max(0.f, bodyContentH_ - bodyBox_.h);
    dirty_ = false;
  }

  // Lista de assuntos filtrada pela busca, com titulo de grupo quando o grupo muda; guarda a caixa de cada linha para o clique.
  void buildList() {
    const float lh = list_.lineHeight(kFont);
    float y = 0.f;
    const char* group = "";
    for (const std::size_t index : model_.VisibleTopics()) {
      const HelpTopic& topic = helpTopics()[index];
      if (std::strcmp(group, topic.group) != 0) {
        group = topic.group;
        y = addGroupHeading(y, T(group));
      }
      if (index == model_.SelectedIndex()) list_.addSolid(0.f, y, listBox_.w, kRowH, colors_.selection, 0.1f);
      rows_.push_back({Box{0.f, y, listBox_.w, kRowH}, index});
      list_.addText(kRowInset, y + (kRowH - lh) * 0.5f, TitleOf(topic, model_.Language()), kFont, theme_.text, listBox_.w - 2.f * kRowInset);
      y += kRowH;
    }
    listContentH_ = y;
  }

  // Titulo de grupo na lista; devolve o Y logo abaixo dele.
  float addGroupHeading(float y, const char* label) {
    const float lh = list_.lineHeight(kSmallFont);
    list_.addText(0.f, y + (kGroupH - lh) * 0.5f, label, kSmallFont, theme_.textDim, listBox_.w);
    list_.addSolid(0.f, y + kGroupH - 1.f, listBox_.w, 1.f, theme_.divider, 0.2f);
    return y + kGroupH;
  }

  // Titulo do assunto atual e seus blocos (titulos, paragrafos, codigo...); guarda a altura total para a rolagem.
  void buildBody() {
    const float width = std::max(0.f, bodyBox_.w - 2.f * kPad);
    float y = kPad;
    const float titleLh = body_.lineHeight(kTitleFont);
    body_.addText(kPad, y, model_.CurrentTitle(), kTitleFont, colors_.heading, width);
    y += titleLh + 4.f;
    body_.addSolid(kPad, y, width, 1.f, theme_.divider, 0.2f);
    y += 2.f * kGap;
    for (const Block& block : model_.CurrentBlocks()) y = addBlock(block, kPad, y, width);
    bodyContentH_ = y + kPad;
  }

  // Desenha um bloco do texto (titulo, paragrafo ou codigo com botao de copiar) e devolve o Y seguinte.
  float addBlock(const Block& block, float x, float y, float width) {
    switch (block.kind) {
      case BlockKind::Heading: {
        const float lh = body_.lineHeight(kHeadingFont);
        body_.addText(x, y + kGap, block.text, kHeadingFont, colors_.heading, width);
        y += kGap + lh + 2.f;
        body_.addSolid(x, y, width, 1.f, theme_.divider, 0.2f);
        return y + kGap;
      }
      case BlockKind::Paragraph: {
        const float h = body_.addParagraph(x, y, block.text, kFont, theme_.text, width);
        return y + h + kGap * 0.5f;
      }
      case BlockKind::Bullet: {
        const float lh = body_.lineHeight(kFont);
        body_.addSolid(x + 2.f, y + lh * 0.5f - 2.f, 4.f, 4.f, theme_.text, 0.3f);
        const float h = body_.addParagraph(x + kBulletIndent, y, block.text, kFont, theme_.text, width - kBulletIndent);
        return y + h + 2.f;
      }
      case BlockKind::Code:
        return addCode(block.text, x, y, width);
    }
    return y;
  }

  // Bloco de codigo: fundo, uma linha por linha de texto, e o botao "Copy code" logo abaixo.
  float addCode(const std::string& code, float x, float y, float width) {
    const std::vector<std::string> lines = SplitLines(code);
    const float lh = body_.lineHeight(kCodeFont);
    const float boxH = static_cast<float>(lines.size()) * lh + 2.f * kCodePad;
    body_.addSolid(x, y, width, boxH, colors_.codeBackground, 0.15f);
    float lineY = y + kCodePad;
    for (const std::string& line : lines) {
      body_.addText(x + kCodeInset, lineY, line, kCodeFont, colors_.codeText, width - 2.f * kCodeInset);
      lineY += lh;
    }
    y += boxH + 4.f;
    const std::string label = T("Copy code");
    const float smallLh = body_.lineHeight(kSmallFont);
    const float buttonW = body_.layout(label, kSmallFont).size.x + 2.f * kButtonPad;
    body_.addSolid(x, y, buttonW, kButtonH, colors_.button, 0.3f);
    body_.addText(x + kButtonPad, y + (kButtonH - smallLh) * 0.5f, label, kSmallFont, theme_.text, buttonW);
    copyButtons_.push_back({Box{x, y, buttonW, kButtonH}, code});
    return y + kButtonH + kGap;
  }

  // Texto "search..." mostrado no campo de busca vazio.
  void buildHint() {
    const float lh = hint_.lineHeight(kFont);
    hint_.addText(0.f, (kSearchH - lh) * 0.5f, T("search..."), kFont, theme_.textDim, searchBox_.w - 2.f * kFieldPad);
  }

  // Desenha: fundo, titulo, lista, texto, busca (TextField da biblioteca) e as barras de rolagem.
  bool drawPacket(SDL_Renderer* renderer) {
    std::vector<SR::UIRenderItem> items;
    std::vector<SR::ClipRectGPU> clips{clipOf(panel_), clipOf(listBox_), clipOf(bodyBox_)};
    const SU::UIHandle owner = sui::handleFor(0);
    std::uint64_t paint = 1;
    list_.appendItems(items, listBox_.x, listBox_.y, listScroll_, listBox_.y, listBox_.y + listBox_.h, owner, 1u, paint);
    appendBar(items, listBar_, listBox_, listContentH_, listScroll_, 1u, owner, paint);
    body_.appendItems(items, bodyBox_.x, bodyBox_.y, bodyScroll_, bodyBox_.y, bodyBox_.y + bodyBox_.h, owner, 2u, paint);
    appendBar(items, bodyBar_, bodyBox_, bodyContentH_, bodyScroll_, 2u, owner, paint);
    appendSearch(items, clips, owner, paint);
    packet_ = extractor_.Build(++frameId_, items, nullptr, clips);
    if (!SR::IsFramePacketValid(packet_)) return true;
    return sdl_.draw(renderer, packet_);
  }

  void appendSearch(std::vector<SR::UIRenderItem>& items, std::vector<SR::ClipRectGPU>& clips, SU::UIHandle owner, std::uint64_t& paint) {
    const SU::Rect box{{searchBox_.x, searchBox_.y}, {searchBox_.w, searchBox_.h}};
    Edit::TextFieldRenderTarget target;
    target.owner = owner;
    target.textureSlice = text_.sliceFor(kFont);
    target.paintOrder = paint;
    const Edit::TextFieldStyle style = searchStyle();
    const auto fieldItems = model_.Search().Build(box, list_.layout(model_.SearchText(), kFont), style, target, clips);
    items.insert(items.end(), fieldItems.begin(), fieldItems.end());
    paint += fieldItems.size() + 1;
    if (!model_.SearchText().empty() || model_.SearchFocused()) return;
    hint_.appendItems(items, searchBox_.x + kFieldPad, searchBox_.y, 0.f, searchBox_.y, searchBox_.y + searchBox_.h, owner,
                      static_cast<std::uint32_t>(clips.size() - 1), paint);
  }

  // Barra de rolagem unica do editor (ui_scrollbar.hpp): so aparece quando o conteudo passa da caixa.
  void appendBar(std::vector<SR::UIRenderItem>& out, ui_scrollbar::Scrollbar& bar, const Box& box, float contentH, float scroll,
                 std::uint32_t clip, SU::UIHandle owner, std::uint64_t& paint) const {
    bar.layout({box.x + box.w - ui_scrollbar::kWidth, box.y, ui_scrollbar::kWidth, box.h}, contentH, box.h);
    for (const ui_scrollbar::Quad& q : bar.quads(palette::active(), scroll)) {
      if (q.rect.w <= 0.f || q.rect.h <= 0.f) continue;
      out.push_back(sui::Canvas::solidItem(q.rect.x, q.rect.y, q.rect.w, q.rect.h, sui::PaletteColor(q.color), 0.8f, owner, clip, paint++));
    }
  }

  static SR::ClipRectGPU clipOf(const Box& b) { return {b.x, b.y, b.x + b.w, b.y + b.h}; }

  // Roda do mouse: rola a lista ou o texto, conforme o que esta sob o ponteiro.
  void wheel(float x, float y, float amount) {
    if (listBox_.contains(x, y)) {
      listScroll_ = std::clamp(listScroll_ - amount * kWheelStep, 0.f, listMax_);
    } else if (bodyBox_.contains(x, y)) {
      bodyScroll_ = std::clamp(bodyScroll_ - amount * kWheelStep, 0.f, bodyMax_);
    }
  }

  // Clique esquerdo no corpo: caixa de busca ganha foco, linha da lista abre o assunto, botao de copiar manda o codigo para a area de transferencia.
  void click(float x, float y, SDL_Window* window) {
    if (searchBox_.contains(x, y)) {
      setSearchFocus(true, window);
      model_.Search().PointerDown({x - searchBox_.x, y - searchBox_.y}, {searchBox_.w, searchBox_.h},
                                  list_.layout(model_.SearchText(), kFont), searchStyle());
      dirty_ = true;
      return;
    }
    setSearchFocus(false, window);
    if (listBox_.contains(x, y)) {
      const float contentY = y - listBox_.y + listScroll_;
      for (const Row& row : rows_) {
        if (row.box.contains(x - listBox_.x, contentY)) selectTopic(row.topic);
      }
    } else if (bodyBox_.contains(x, y)) {
      const float contentX = x - bodyBox_.x, contentY = y - bodyBox_.y + bodyScroll_;
      for (const CopyButton& button : copyButtons_) {
        if (button.box.contains(contentX, contentY)) SDL_SetClipboardText(button.code.c_str());
      }
    }
  }

  // Abre o assunto escolhido e volta o texto ao topo.
  void selectTopic(std::size_t topic) {
    model_.SelectTopic(topic);
    bodyScroll_ = 0.f;
    dirty_ = true;
  }

  // Foco na busca liga e desliga a entrada de texto do SDL (sem isso o SDL nao manda SDL_EVENT_TEXT_INPUT).
  void setSearchFocus(bool focused, SDL_Window* window) {
    if (model_.SearchFocused() == focused) return;
    model_.SetSearchFocus(focused);
    dirty_ = true;
    if (focused) {
      SDL_StartTextInput(window);
    } else {
      SDL_StopTextInput(window);
    }
  }

  // Arrasto do mouse na busca: move o cursor/selecao do campo.
  void moveSearchCaret(float x, float y) {
    const SU::Vec2 local{x - searchBox_.x, y - searchBox_.y};
    Edit::TextField& field = model_.Search();
    const ST::TextLayoutResult layout = list_.layout(model_.SearchText(), kFont);
    field.PointerMove(local, {searchBox_.w, searchBox_.h}, layout, searchStyle());
  }

  // Teclas na busca: Esc tira o foco, Ctrl+A/V, setas e demais edicoes sao repassadas ao modelo.
  void keyInSearch(SDL_Keycode key, SDL_Keymod mod, SDL_Window* window) {
    const bool shift = (mod & SDL_KMOD_SHIFT) != 0;
    const bool ctrl = (mod & SDL_KMOD_CTRL) != 0;
    dirty_ = true;
    if (key == SDLK_ESCAPE) {
      setSearchFocus(false, window);
    } else if (ctrl && key == SDLK_A) {
      model_.SelectAllSearch();
    } else if (ctrl && key == SDLK_V) {
      if (char* clip = SDL_GetClipboardText()) {
        model_.InsertSearchText(clip);
        SDL_free(clip);
      }
    } else if (key == SDLK_LEFT) {
      model_.PressSearch(SearchKey::Left, shift);
    } else if (key == SDLK_RIGHT) {
      model_.PressSearch(SearchKey::Right, shift);
    } else if (key == SDLK_HOME) {
      model_.PressSearch(SearchKey::Home, shift);
    } else if (key == SDLK_END) {
      model_.PressSearch(SearchKey::End, shift);
    } else if (key == SDLK_BACKSPACE) {
      model_.PressSearch(SearchKey::Backspace, shift);
    } else if (key == SDLK_DELETE) {
      model_.PressSearch(SearchKey::Delete, shift);
    }
  }

  const sui::TextAtlasSet& text_;
  sui::SpriteUiSdlRenderer& sdl_;
  framed::Chrome frame_;
  sui::Canvas list_;
  sui::Canvas body_;
  sui::Canvas hint_;
  sui::Theme theme_;
  Colors colors_;
  mutable ui_scrollbar::Scrollbar listBar_, bodyBar_;
  HelpModel model_;
  SR::RenderExtractor extractor_;
  SR::FramePacket packet_;
  std::uint64_t frameId_ = 0;
  bool dirty_ = true;
  unsigned long long themeSig_ = 0;   // assinatura da paleta do ultimo quadro
  bool open_ = false;

  Box panel_, searchBox_, listBox_, bodyBox_;
  std::vector<Row> rows_;
  std::vector<CopyButton> copyButtons_;
  float listContentH_ = 0.f, bodyContentH_ = 0.f;
  float listScroll_ = 0.f, bodyScroll_ = 0.f;
  float listMax_ = 0.f, bodyMax_ = 0.f;
};

}  // namespace editor::help
