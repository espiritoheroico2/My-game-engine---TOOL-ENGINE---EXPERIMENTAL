#pragma once

// Janela "Test map of chunks" com SpriteUI.
// Limites e contagens: test_map_model.hpp. Aqui: moldura padrao do editor (framed::Chrome) com o corpo rolavel por
// dentro: trilho do lado (arrasta com o mouse), contagens, aviso de mapa grande e o botao "Generate map". A geracao em si fica no editor: handleEvent devolve Action::Generate.

#include "framed_window.hpp"
#include "i18n.hpp"
#include "sui_panel.hpp"
#include "test_map_model.hpp"
#include "theme_colors.hpp"
#include "ui_scrollbar.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace editor::testmap {

namespace SU = ToolEngine::SpriteUi;
namespace SR = ToolEngine::SpriteUi::Rendering;
namespace ST = ToolEngine::SpriteUi::Text;

// Resultado de um evento: Generate quando o botao foi clicado (o editor gera o mapa com o lado atual).
enum class Action : std::uint8_t { None, Consumed, Generate };

inline constexpr float kWidth = 420.f;
inline constexpr float kStartContentH = 330.f;   // altura do corpo antes da primeira montagem
inline constexpr float kScrollStep = 40.f;
inline constexpr float kPad = 16.f;
inline constexpr float kGap = 8.f;
inline constexpr float kFont = 14.f;
inline constexpr float kKnob = 14.f;
inline constexpr float kTrackH = 6.f;
inline constexpr float kButtonW = 160.f;
inline constexpr float kButtonH = 28.f;

// Cores que a janela usa alem das do sui::Theme: todas da paleta em uso (theme_colors.hpp), refeitas em draw().
struct Colors {
  SU::Vec4 track, fill, button, warning;
  Colors() {
    const editor::theme_colors::Colors t = editor::theme_colors::Active();
    track = sui::PaletteColor(t.field);
    fill = sui::PaletteColor(t.btnOn);
    button = sui::PaletteColor(t.btnOn);
    warning = sui::PaletteColor(t.error);
  }
};

class TestMapPanel {
 public:
  TestMapPanel(const sui::TextAtlasSet& text, sui::SpriteUiSdlRenderer& sdl) : sdl_{sdl}, canvas_{text}, chrome_{text, sdl} {}
  TestMapPanel(const TestMapPanel&) = delete;
  TestMapPanel& operator=(const TestMapPanel&) = delete;

  // Moldura padrao (titulo, "?", X, grip, dock). O host registra no dock e le consumeClose()/consumeHelp().
  framed::Chrome& chrome() { return chrome_; }

  // Eventos do SDL. `open` = janela aberta; `side` = lado do mapa (o arraste muda).
  Action handleEvent(const SDL_Event& e, bool& open, int& side) {
    if (!open || !chrome_.visible()) return Action::None;
    if (chrome_.handleEvent(e)) return Action::Consumed;   // X, "?" e minimizar da moldura
    switch (e.type) {
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e.button.button != SDL_BUTTON_LEFT || !body_.contains(e.button.x, e.button.y)) return Action::None;
        if (bar_.pointerDown(e.button.x, e.button.y, scroll_)) {   // barra de rolagem unica: arrasta ou pagina
          scroll_ = ui_scrollbar::clampScroll(scroll_, contentH_, body_.h);
          return Action::Consumed;
        }
        return press(e.button.x, e.button.y, side);
      case SDL_EVENT_MOUSE_MOTION:
        if (bar_.pointerMove(e.motion.x, e.motion.y, scroll_)) {
          scroll_ = ui_scrollbar::clampScroll(scroll_, contentH_, body_.h);
          return Action::Consumed;
        }
        if (bar_.dragging()) return Action::Consumed;
        if (!dragging_) return Action::None;
        side = SideAt(e.motion.x, side);
        return Action::Consumed;
      case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e.button.button != SDL_BUTTON_LEFT) return Action::None;
        if (bar_.pointerUp()) return Action::Consumed;
        if (!dragging_) return Action::None;
        dragging_ = false;
        return Action::Consumed;
      case SDL_EVENT_MOUSE_WHEEL:
        if (!body_.contains(e.wheel.mouse_x, e.wheel.mouse_y)) return Action::None;
        scroll_ = ui_scrollbar::clampScroll(scroll_ - e.wheel.y * kScrollStep, contentH_, body_.h);
        return Action::Consumed;
      default:
        return Action::None;
    }
  }

  // Desenha a moldura e o corpo quando `open`. Retorna false so em falha do SDL.
  // `language` = I18n::get().code(): o texto muda quando o idioma muda.
  bool draw(SDL_Renderer* renderer, SDL_Window* window, bool open, int side, const std::string& language) {
    if (!open) {
      chrome_.hide();
      dragging_ = false;
      return true;
    }
    if (!renderer || !window) return false;
    int ww = 0, wh = 0;
    if (!SDL_GetWindowSize(window, &ww, &wh)) return false;
    if (ww <= 0 || wh <= 0) return true;   // janela minimizada
    const int shown = ClampSide(side);
    const sui::Box body = chrome_.prepare(static_cast<float>(ww), static_cast<float>(wh), kWidth, framed::frameHeightFor(contentH_ > 0.f ? contentH_ : kStartContentH));
    if (!chrome_.draw(renderer, T("Test map of chunks"))) return false;
    if (body.w <= 0.f || body.h <= 0.f) {   // minimizada: so o cabecalho
      body_ = {};
      dragging_ = false;
      return true;
    }
    body_ = body;
    theme_ = sui::Theme{};   // paleta em uso (Configuracoes > Appearance)
    colors_ = Colors{};
    const unsigned long long sig = editor::theme_colors::Signature();
    if (!built_ || shown != builtSide_ || language != builtLanguage_ || body.w != builtWidth_ || sig != builtSig_) rebuild(shown, language, body.w, sig);
    scroll_ = ui_scrollbar::clampScroll(scroll_, contentH_, body_.h);
    return drawPacket(renderer, shown);
  }

 private:
  // Posicoes em coordenadas locais do painel (origem no canto superior esquerdo).
  struct Layout {
    float trackW = 0.f;
    float trackY = 0.f;     // linha central do trilho
    SU::Vec2 button{};
  };

  [[nodiscard]] static bool inside(float x, float y, float left, float top, float w, float h) {
    return x >= left && y >= top && x < left + w && y < top + h;
  }

  // Clique esquerdo no corpo: arrasta o trilho ou pede gerar o mapa. Clique em area vazia e consumido.
  Action press(float x, float y, int& side) {
    const float lx = x - body_.x, ly = y - body_.y + scroll_;
    if (inside(lx, ly, kPad, layout_.trackY - kKnob, layout_.trackW, 2.f * kKnob)) {
      dragging_ = true;
      side = SideAt(x, side);
      return Action::Consumed;
    }
    if (inside(lx, ly, layout_.button.x, layout_.button.y, kButtonW, kButtonH)) return Action::Generate;
    return Action::Consumed;
  }

  // Lado do mapa para a posicao x da tela (o trilho ocupa a largura do conteudo).
  [[nodiscard]] int SideAt(float x, int fallback) const {
    if (layout_.trackW <= 0.f) return fallback;
    const float fraction = (x - body_.x - kPad) / layout_.trackW;
    return SideOfFraction(fraction);
  }

  // Monta o texto estatico e a posicao dos controles para o lado, o idioma e a largura. Roda so quando um deles muda.
  void rebuild(int side, const std::string& language, float panelW, unsigned long long sig) {
    canvas_.clear();
    built_ = true;
    builtSide_ = side;
    builtLanguage_ = language;
    builtWidth_ = panelW;
    builtSig_ = sig;
    const float width = panelW - 2.f * kPad;
    float y = kPad;
    const std::string description = T("Creates a square map of terrain chunks (16 x 16 x 16 blocks each) centered at the origin. It replaces the chunks already in the scene. Each chunk is one object, so the cost grows with the square of the side: use Settings > Render > Draw distance to see only what is near the camera.");
    y += canvas_.addParagraph(kPad, y, description, kFont, theme_.text, width) + 2.f * kPad;

    const float lh = canvas_.lineHeight(kFont);
    canvas_.addText(kPad, y, T("Chunks per side"), kFont, theme_.text, width * 0.6f);
    canvas_.addText(panelW - kPad, y, std::to_string(side), kFont, theme_.text, width * 0.4f, true);
    y += lh + kGap;

    layout_.trackW = width;
    layout_.trackY = y + kKnob * 0.5f;
    y += kKnob + 2.f * kPad;

    const Summary summary = Summarize(side);
    char readout[160];
    std::snprintf(readout, sizeof readout, "%s: %lld   |   %s: %lld x %lld", T("Chunks"), summary.chunks, T("Size in blocks"), summary.blocks, summary.blocks);
    canvas_.addText(kPad, y, readout, kFont, theme_.text, width);
    y += lh + kGap;

    if (summary.large) {
      y += canvas_.addParagraph(kPad, y, T("Large map: expect a slow generation and a heavy scene."), kFont, colors_.warning, width) + kGap;
    }

    y += kPad;
    layout_.button = {kPad, y};
    canvas_.addSolid(kPad, y, kButtonW, kButtonH, colors_.button, 0.3f);
    const std::string label = T("Generate map");
    const ST::TextLayoutResult labelLayout = canvas_.layout(label, kFont);
    canvas_.addText(kPad + (kButtonW - labelLayout.size.x) * 0.5f, y + (kButtonH - labelLayout.lineHeight) * 0.5f, label, kFont, theme_.text, kButtonW);
    contentH_ = y + kButtonH + kPad;
  }

  // Itens do corpo (canvas em cache, trilho e barra de rolagem) enviados ao renderizador SpriteUI.
  bool drawPacket(SDL_Renderer* renderer, int side) {
    std::vector<SR::UIRenderItem> items;
    const std::vector<SR::ClipRectGPU> clips{{body_.x, body_.y, body_.x + body_.w, body_.y + body_.h}};
    const SU::UIHandle owner = sui::handleFor(0);
    std::uint64_t paint = 1;
    canvas_.appendItems(items, body_.x, body_.y, scroll_, body_.y, body_.y + body_.h, owner, 0u, paint);
    appendSlider(items, side, owner, paint);
    // Barra de rolagem unica do editor (ui_scrollbar.hpp): so aparece quando o conteudo passa do corpo.
    bar_.layout({body_.x + body_.w - ui_scrollbar::kWidth - 2.f, body_.y, ui_scrollbar::kWidth, body_.h}, contentH_, body_.h);
    for (const ui_scrollbar::Quad& q : bar_.quads(palette::active(), scroll_)) {
      if (q.rect.w > 0.f && q.rect.h > 0.f) items.push_back(sui::Canvas::solidItem(q.rect.x, q.rect.y, q.rect.w, q.rect.h, sui::PaletteColor(q.color), 0.8f, owner, 0u, paint++));
    }
    packet_ = extractor_.Build(++frameId_, items, nullptr, clips);
    if (!SR::IsFramePacketValid(packet_)) return true;
    return sdl_.draw(renderer, packet_);
  }

  // Trilho, parte preenchida e cursor do lado do mapa. Desenhado por quadro: o cursor muda ao arrastar.
  void appendSlider(std::vector<SR::UIRenderItem>& items, int side, SU::UIHandle owner, std::uint64_t& paint) const {
    const float left = body_.x + kPad;
    const float trackTop = body_.y - scroll_ + layout_.trackY - kTrackH * 0.5f;
    const float fraction = FractionOfSide(side);
    items.push_back(sui::Canvas::solidItem(left, trackTop, layout_.trackW, kTrackH, colors_.track, 0.4f, owner, 0u, paint++));
    items.push_back(sui::Canvas::solidItem(left, trackTop, layout_.trackW * fraction, kTrackH, colors_.fill, 0.45f, owner, 0u, paint++));
    const float knobX = left + layout_.trackW * fraction - kKnob * 0.5f;
    items.push_back(sui::Canvas::solidItem(knobX, body_.y - scroll_ + layout_.trackY - kKnob * 0.5f, kKnob, kKnob, theme_.text, 0.5f, owner, 0u, paint++));
  }

  sui::Theme theme_;
  Colors colors_;
  sui::SpriteUiSdlRenderer& sdl_;
  sui::Canvas canvas_;
  SR::RenderExtractor extractor_;
  SR::FramePacket packet_;
  std::uint64_t frameId_ = 0;

  Layout layout_;
  framed::Chrome chrome_;
  sui::Box body_;   // corpo da moldura neste quadro (vazio: escondida ou minimizada)
  ui_scrollbar::Scrollbar bar_;
  float scroll_ = 0.f;
  float contentH_ = 0.f;
  bool built_ = false;
  int builtSide_ = 0;
  std::string builtLanguage_;
  float builtWidth_ = 0.f;
  unsigned long long builtSig_ = 0;   // assinatura da paleta da ultima montagem
  bool dragging_ = false;
};

}  // namespace editor::testmap
