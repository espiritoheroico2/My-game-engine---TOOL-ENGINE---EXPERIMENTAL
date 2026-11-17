#pragma once

// Janela "Test map of chunks" com SpriteUI (troca testMapWindow de extra_ui.hpp; ver kTestMapUsesSpriteUi em main.cpp).
// Limites e contagens: test_map_model.hpp. Aqui: caixa centralizada, trilho do lado (arrasta com o mouse), contagens,
// aviso de mapa grande e o botao "Generate map". A geracao em si fica no editor: handleEvent devolve Action::Generate.

#include "i18n.hpp"
#include "sui_panel.hpp"
#include "test_map_model.hpp"

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
inline constexpr float kMargin = 24.f;
inline constexpr float kTitleH = 36.f;
inline constexpr float kCloseSize = 26.f;
inline constexpr float kCloseInset = 6.f;
inline constexpr float kPad = 16.f;
inline constexpr float kGap = 8.f;
inline constexpr float kFont = 14.f;
inline constexpr float kTitleFont = 18.f;
inline constexpr float kKnob = 14.f;
inline constexpr float kTrackH = 6.f;
inline constexpr float kButtonW = 160.f;
inline constexpr float kButtonH = 28.f;

// Cores que a janela usa alem das do sui::Theme.
struct Colors {
  SU::Vec4 track = sui::Srgb(0.16f, 0.18f, 0.22f);
  SU::Vec4 fill = sui::Srgb(0.20f, 0.52f, 0.86f);
  SU::Vec4 button = sui::Srgb(0.12f, 0.42f, 0.72f);
  SU::Vec4 warning = sui::Srgb(1.f, 0.8f, 0.3f);
};

class TestMapPanel {
 public:
  TestMapPanel(const ST::TruetypeAtlas& atlas, sui::SpriteUiSdlRenderer& sdl) : sdl_{sdl}, canvas_{atlas} {}
  TestMapPanel(const TestMapPanel&) = delete;
  TestMapPanel& operator=(const TestMapPanel&) = delete;

  // Eventos do SDL antes do ImGui. `open` = janela aberta; `side` = lado do mapa (o arraste muda). Chamar antes do ImGui.
  Action handleEvent(const SDL_Event& e, bool& open, int& side) {
    if (!open) return Action::None;
    switch (e.type) {
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e.button.button != SDL_BUTTON_LEFT || !contains(e.button.x, e.button.y)) return Action::None;
        return press(e.button.x, e.button.y, open, side);
      case SDL_EVENT_MOUSE_MOTION:
        if (!dragging_) return Action::None;
        side = SideAt(e.motion.x, side);
        return Action::Consumed;
      case SDL_EVENT_MOUSE_BUTTON_UP:
        if (!dragging_ || e.button.button != SDL_BUTTON_LEFT) return Action::None;
        dragging_ = false;
        return Action::Consumed;
      case SDL_EVENT_MOUSE_WHEEL:
        return contains(e.wheel.mouse_x, e.wheel.mouse_y) ? Action::Consumed : Action::None;
      default:
        return Action::None;
    }
  }

  // Desenha a janela quando `open`. Retorna false so em falha do SDL.
  // `language` = I18n::get().code(): o texto muda quando o idioma muda.
  bool draw(SDL_Renderer* renderer, SDL_Window* window, bool open, int side, const std::string& language) {
    theme_ = sui::Theme::fromTokens();   // cores e forma atuais (Aparencia)
    if (!open) return true;
    if (!renderer || !window) return false;
    int ww = 0, wh = 0;
    if (!SDL_GetWindowSize(window, &ww, &wh)) return false;
    if (ww <= 0 || wh <= 0) return true;   // janela minimizada
    const int shown = ClampSide(side);
    const float w = std::min(kWidth, static_cast<float>(ww) - 2.f * kMargin);
    if (!built_ || shown != builtSide_ || language != builtLanguage_ || w != builtWidth_) rebuild(shown, language, w);
    panel_ = {(static_cast<float>(ww) - w) * 0.5f, (static_cast<float>(wh) - contentH_) * 0.5f, w, contentH_};
    return drawPacket(renderer, shown);
  }

 private:
  // Posicoes em coordenadas locais do painel (origem no canto superior esquerdo).
  struct Layout {
    float trackW = 0.f;
    float trackY = 0.f;     // linha central do trilho
    SU::Vec2 button{};
  };

  [[nodiscard]] bool contains(float x, float y) const {
    return x >= panel_.x && y >= panel_.y && x < panel_.x + panel_.w && y < panel_.y + panel_.h;
  }

  [[nodiscard]] static bool inside(float x, float y, float left, float top, float w, float h) {
    return x >= left && y >= top && x < left + w && y < top + h;
  }

  Action press(float x, float y, bool& open, int& side) {
    const float lx = x - panel_.x, ly = y - panel_.y;
    if (inside(lx, ly, panel_.w - kCloseSize - kCloseInset, (kTitleH - kCloseSize) * 0.5f, kCloseSize, kCloseSize)) {
      open = false;
      dragging_ = false;
      return Action::Consumed;
    }
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
    const float fraction = (x - panel_.x - kPad) / layout_.trackW;
    return SideOfFraction(fraction);
  }

  // Monta o texto estatico e a posicao dos controles para o lado, o idioma e a largura. Roda so quando um deles muda.
  void rebuild(int side, const std::string& language, float panelW) {
    canvas_.clear();
    built_ = true;
    builtSide_ = side;
    builtLanguage_ = language;
    builtWidth_ = panelW;
    const float width = panelW - 2.f * kPad;
    const float titleLh = canvas_.lineHeight(kTitleFont);
    canvas_.addText(kPad, (kTitleH - titleLh) * 0.5f, T("Test map of chunks"), kTitleFont, theme_.text, width - kCloseSize);
    canvas_.addSolid(0.f, kTitleH - 1.f, panelW, 1.f, theme_.divider, 0.2f);
    const ST::TextLayoutResult x = canvas_.layout("X", kTitleFont);
    canvas_.addText(panelW - kCloseSize - kCloseInset + (kCloseSize - x.size.x) * 0.5f, (kTitleH - x.lineHeight) * 0.5f, "X", kTitleFont, theme_.text, kCloseSize);

    float y = kTitleH + kPad;
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

  bool drawPacket(SDL_Renderer* renderer, int side) {
    std::vector<SR::UIRenderItem> items;
    const std::vector<SR::ClipRectGPU> clips{{panel_.x, panel_.y, panel_.x + panel_.w, panel_.y + panel_.h}};
    const SU::UIHandle owner = sui::handleFor(0);
    std::uint64_t paint = 1;
    items.push_back(sui::Canvas::solidItem(panel_.x, panel_.y, panel_.w, panel_.h, theme_.panel, 0.f, owner, 0u, paint++));
    canvas_.appendItems(items, panel_.x, panel_.y, 0.f, panel_.y, panel_.y + panel_.h, owner, 0u, paint);
    appendSlider(items, side, owner, paint);
    packet_ = extractor_.Build(++frameId_, items, nullptr, clips);
    if (!SR::IsFramePacketValid(packet_)) return true;
    return sdl_.draw(renderer, packet_);
  }

  // Trilho, parte preenchida e cursor do lado do mapa. Desenhado por quadro: o cursor muda ao arrastar.
  void appendSlider(std::vector<SR::UIRenderItem>& items, int side, SU::UIHandle owner, std::uint64_t& paint) const {
    const float left = panel_.x + kPad;
    const float trackTop = panel_.y + layout_.trackY - kTrackH * 0.5f;
    const float fraction = FractionOfSide(side);
    items.push_back(sui::Canvas::solidItem(left, trackTop, layout_.trackW, kTrackH, colors_.track, 0.4f, owner, 0u, paint++));
    items.push_back(sui::Canvas::solidItem(left, trackTop, layout_.trackW * fraction, kTrackH, colors_.fill, 0.45f, owner, 0u, paint++));
    const float knobX = left + layout_.trackW * fraction - kKnob * 0.5f;
    items.push_back(sui::Canvas::solidItem(knobX, panel_.y + layout_.trackY - kKnob * 0.5f, kKnob, kKnob, theme_.text, 0.5f, owner, 0u, paint++));
  }

  sui::Theme theme_;
  Colors colors_;
  sui::SpriteUiSdlRenderer& sdl_;
  sui::Canvas canvas_;
  SR::RenderExtractor extractor_;
  SR::FramePacket packet_;
  std::uint64_t frameId_ = 0;

  Layout layout_;
  sui::Box panel_;
  float contentH_ = 0.f;
  bool built_ = false;
  int builtSide_ = 0;
  std::string builtLanguage_;
  float builtWidth_ = 0.f;
  bool dragging_ = false;
};

}  // namespace editor::testmap
