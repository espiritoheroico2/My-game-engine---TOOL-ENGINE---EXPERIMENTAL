#pragma once

// Janela "Node" com SpriteUI, na moldura padrao do editor (framed::Chrome: titulo, "?", X, grip, borda e dock).
// Edicao individual: mostra o objeto isolado e os filhos dele (recursivo). Regras da subarvore: node_model.hpp.
// Topo fixo: nome do objeto isolado e o botao "Exit isolation". Abaixo, a lista rolavel; clicar numa linha a seleciona.
// A janela nao mexe no Editor: handleEvent devolve o pedido (Result) e o main.cpp aplica; draw recebe os dados prontos.

#include "framed_window.hpp"
#include "i18n.hpp"
#include "sui_panel.hpp"
#include "theme_colors.hpp"
#include "ui_scrollbar.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace editor::nodeui {

namespace SU = ToolEngine::SpriteUi;
namespace SR = ToolEngine::SpriteUi::Rendering;
namespace ST = ToolEngine::SpriteUi::Text;

inline constexpr float kWidth = 320.f;
inline constexpr float kHeight = 380.f;
inline constexpr float kPad = 10.f;
inline constexpr float kFont = 13.f;
inline constexpr float kToolbarH = 38.f;
inline constexpr float kRowH = 24.f;
inline constexpr float kIndent = 14.f;
inline constexpr float kButtonW = 130.f;
inline constexpr float kButtonH = 26.f;
inline constexpr float kScrollStep = 40.f;

// Uma linha da lista: chave do objeto (a mesma da Hierarchy), nome e profundidade (0 = raiz isolada).
struct Item {
  std::uint64_t key = 0;
  std::string label;
  int depth = 0;
};

// Dados do quadro: nome do isolado, se ha isolamento, as linhas e a selecao do Editor.
struct Source {
  std::string title;
  bool isolated = false;
  std::vector<Item> rows;
  std::uint64_t selected = 0;
};

// O que um evento pediu ao Editor: nada, so consumido, selecionar a chave `key` ou sair do isolamento.
struct Result {
  enum class Kind : std::uint8_t { None, Consumed, Select, Exit };
  Kind kind = Kind::None;
  std::uint64_t key = 0;
};

// Cores da paleta em uso (theme_colors.hpp), refeitas em draw(): nada de cor literal.
struct Colors {
  SU::Vec4 button, buttonOff, rowSel, dim;
  Colors() {
    const editor::theme_colors::Colors t = editor::theme_colors::Active();
    button = sui::PaletteColor(t.btn);
    buttonOff = sui::PaletteColor(t.field);
    rowSel = sui::PaletteColor(t.rowSel);
    dim = sui::PaletteColor(t.textDim);
  }
};

// Janela Node: lista o objeto isolado e os filhos; pede selecao e saida do isolamento ao host.
class Panel {
 public:
  Panel(const sui::TextAtlasSet& text, sui::SpriteUiSdlRenderer& sdl) : sdl_{sdl}, listCanvas_{text}, barCanvas_{text}, chrome_{text, sdl} {}
  Panel(const Panel&) = delete;
  Panel& operator=(const Panel&) = delete;

  // Moldura padrao (titulo, "?", X, grip, dock). O host registra no dock e le consumeClose()/consumeHelp().
  framed::Chrome& chrome() { return chrome_; }

  // Eventos do SDL. `open` = janela aberta.
  Result handleEvent(const SDL_Event& e, bool open) {
    using K = Result::Kind;
    if (!open || !chrome_.visible()) return {};
    if (chrome_.handleEvent(e)) return {K::Consumed, 0};   // X, "?" e minimizar da moldura
    switch (e.type) {
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e.button.button != SDL_BUTTON_LEFT || !body_.contains(e.button.x, e.button.y)) return {};
        if (bar_.pointerDown(e.button.x, e.button.y, scroll_)) {   // barra de rolagem unica: arrasta ou pagina
          scroll_ = ui_scrollbar::clampScroll(scroll_, contentH_, listH());
          return {K::Consumed, 0};
        }
        return press(e.button.x, e.button.y);
      case SDL_EVENT_MOUSE_MOTION:
        if (bar_.pointerMove(e.motion.x, e.motion.y, scroll_)) scroll_ = ui_scrollbar::clampScroll(scroll_, contentH_, listH());
        return bar_.dragging() ? Result{K::Consumed, 0} : Result{};   // o arraste segue com o mouse fora da janela
      case SDL_EVENT_MOUSE_BUTTON_UP:
        return e.button.button == SDL_BUTTON_LEFT && bar_.pointerUp() ? Result{K::Consumed, 0} : Result{};
      case SDL_EVENT_MOUSE_WHEEL:
        if (!body_.contains(e.wheel.mouse_x, e.wheel.mouse_y)) return {};
        scroll_ = ui_scrollbar::clampScroll(scroll_ - e.wheel.y * kScrollStep, contentH_, listH());
        return {K::Consumed, 0};
      default:
        return {};
    }
  }

  // Desenha a moldura e o corpo quando `open`. Retorna false so em falha do SDL.
  bool draw(SDL_Renderer* renderer, SDL_Window* window, bool open, const Source& src) {
    if (!open) {
      chrome_.hide();
      return true;
    }
    if (!renderer || !window) return false;
    int ww = 0, wh = 0;
    if (!SDL_GetWindowSize(window, &ww, &wh)) return false;
    if (ww <= 0 || wh <= 0) return true;   // janela minimizada
    const sui::Box body = chrome_.prepare(static_cast<float>(ww), static_cast<float>(wh), kWidth, kHeight);
    if (!chrome_.draw(renderer, T("Node"))) return false;
    if (body.w <= 0.f || body.h <= 0.f) {   // minimizada: so o cabecalho
      body_ = {};
      return true;
    }
    body_ = body;
    theme_ = sui::Theme{};   // paleta em uso (Configuracoes > Appearance)
    colors_ = Colors{};
    isolated_ = src.isolated;
    rows_.clear();
    rows_.reserve(src.rows.size());
    for (const Item& it : src.rows) rows_.push_back(it.key);
    scroll_ = ui_scrollbar::clampScroll(scroll_, contentH_, listH());
    rebuild(src);
    scroll_ = ui_scrollbar::clampScroll(scroll_, contentH_, listH());
    return drawPacket(renderer);
  }

 private:
  // Altura da area da lista (o corpo menos o topo fixo).
  [[nodiscard]] float listH() const { return std::max(0.f, body_.h - kToolbarH); }

  [[nodiscard]] static bool inside(float x, float y, float left, float top, float w, float h) {
    return x >= left && y >= top && x < left + w && y < top + h;
  }

  // Retangulo do botao "Exit isolation", em coordenadas do corpo.
  [[nodiscard]] SU::Vec2 buttonPos() const { return {body_.w - kPad - kButtonW, (kToolbarH - kButtonH) * 0.5f}; }

  // Clique esquerdo no corpo: botao Exit, linha da lista (seleciona) ou area vazia (consumida).
  Result press(float x, float y) {
    using K = Result::Kind;
    const float lx = x - body_.x, ly = y - body_.y;
    const SU::Vec2 b = buttonPos();
    if (inside(lx, ly, b.x, b.y, kButtonW, kButtonH)) return isolated_ ? Result{K::Exit, 0} : Result{K::Consumed, 0};
    if (ly < kToolbarH) return {K::Consumed, 0};
    const float row = (ly - kToolbarH + scroll_) / kRowH;
    if (row < 0.f) return {K::Consumed, 0};
    const std::size_t i = static_cast<std::size_t>(std::floor(row));
    if (i < rows_.size()) return {K::Select, rows_[i]};
    return {K::Consumed, 0};
  }

  // Monta o topo e a lista deste quadro. So as linhas visiveis entram no canvas (a lista pode ser grande).
  void rebuild(const Source& src) {
    barCanvas_.clear();
    listCanvas_.clear();
    const float width = body_.w - 2.f * kPad;
    // Topo: nome do isolado (ou aviso) e o botao.
    const float lh = barCanvas_.lineHeight(kFont);
    const float nameW = std::max(0.f, width - kButtonW - kPad);
    const std::string name = src.isolated ? src.title : std::string(T("No isolation"));
    barCanvas_.addText(kPad, (kToolbarH - lh) * 0.5f, name, kFont, src.isolated ? theme_.text : colors_.dim, nameW);
    const SU::Vec2 b = buttonPos();
    barCanvas_.addSolid(b.x, b.y, kButtonW, kButtonH, src.isolated ? colors_.button : colors_.buttonOff, 0.3f, 3.f);
    const std::string label = T("Exit isolation");
    const ST::TextLayoutResult ll = barCanvas_.layout(label, kFont);
    barCanvas_.addText(b.x + (kButtonW - ll.size.x) * 0.5f, b.y + (kButtonH - ll.lineHeight) * 0.5f, label, kFont, src.isolated ? theme_.text : colors_.dim, kButtonW);

    // Lista (coordenadas locais da lista, com rolagem aplicada no desenho).
    if (src.rows.empty()) {
      const std::string hint = T("Right-click an object in the Hierarchy and choose Edit individual.");
      const float h = listCanvas_.addParagraph(kPad, kPad, hint, kFont, colors_.dim, width);
      contentH_ = h + 2.f * kPad;
      return;
    }
    const float vh = listH();
    const std::size_t first = static_cast<std::size_t>(std::max(0.f, std::floor(scroll_ / kRowH)));
    const std::size_t last = std::min(src.rows.size(), first + static_cast<std::size_t>(std::ceil(vh / kRowH)) + 2);
    const float textDy = (kRowH - listCanvas_.lineHeight(kFont)) * 0.5f;
    for (std::size_t i = first; i < last; ++i) {
      const Item& it = src.rows[i];
      const float y = static_cast<float>(i) * kRowH;
      if (it.key == src.selected) listCanvas_.addSolid(0.f, y, body_.w, kRowH, colors_.rowSel, 0.25f);
      const float x = kPad + kIndent * static_cast<float>(it.depth);
      listCanvas_.addText(x, y + textDy, it.label.empty() ? std::string(T("(unnamed)")) : it.label, kFont, theme_.text, std::max(0.f, body_.w - x - kPad - ui_scrollbar::kWidth));
    }
    contentH_ = static_cast<float>(src.rows.size()) * kRowH;
  }

  // Itens do corpo (topo, lista recortada e barra de rolagem) enviados ao renderizador SpriteUI.
  bool drawPacket(SDL_Renderer* renderer) {
    std::vector<SR::UIRenderItem> items;
    const float listTop = body_.y + kToolbarH;
    // Recorte 0 = corpo inteiro (topo, barra); recorte 1 = so a lista (a linha parcial nao invade o topo).
    const std::vector<SR::ClipRectGPU> clips{{body_.x, body_.y, body_.x + body_.w, body_.y + body_.h},
                                             {body_.x, listTop, body_.x + body_.w, body_.y + body_.h}};
    const SU::UIHandle owner = sui::handleFor(0);
    std::uint64_t paint = 1;
    barCanvas_.appendItems(items, body_.x, body_.y, 0.f, body_.y, listTop, owner, 0u, paint);
    listCanvas_.appendItems(items, body_.x, listTop, scroll_, listTop, body_.y + body_.h, owner, 1u, paint);
    // Barra de rolagem unica do editor (ui_scrollbar.hpp): so aparece quando a lista passa da area.
    bar_.layout({body_.x + body_.w - ui_scrollbar::kWidth - 2.f, listTop, ui_scrollbar::kWidth, listH()}, contentH_, listH());
    for (const ui_scrollbar::Quad& q : bar_.quads(palette::active(), scroll_)) {
      if (q.rect.w > 0.f && q.rect.h > 0.f) items.push_back(sui::Canvas::solidItem(q.rect.x, q.rect.y, q.rect.w, q.rect.h, sui::PaletteColor(q.color), 0.8f, owner, 0u, paint++));
    }
    packet_ = extractor_.Build(++frameId_, items, nullptr, clips);
    if (!SR::IsFramePacketValid(packet_)) return true;
    return sdl_.draw(renderer, packet_);
  }

  sui::Theme theme_;
  Colors colors_;
  sui::SpriteUiSdlRenderer& sdl_;
  sui::Canvas listCanvas_;
  sui::Canvas barCanvas_;
  SR::RenderExtractor extractor_;
  SR::FramePacket packet_;
  std::uint64_t frameId_ = 0;

  framed::Chrome chrome_;
  sui::Box body_;   // corpo da moldura neste quadro (vazio: escondida ou minimizada)
  ui_scrollbar::Scrollbar bar_;
  std::vector<std::uint64_t> rows_;   // chave de cada linha, na ordem exibida (para o clique)
  bool isolated_ = false;
  float scroll_ = 0.f;
  float contentH_ = 0.f;
};

}  // namespace editor::nodeui
