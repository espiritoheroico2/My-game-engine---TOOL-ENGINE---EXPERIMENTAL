#pragma once

// Barra de status feita com SpriteUI.
// Os textos vem do statusbar_model.hpp. A faixa fica no pe da janela, no mesmo renderer.
// Sem interacao: so mostra os textos. Remonta o conteudo so quando o modelo muda ou a largura da janela muda.

#include "sui_panel.hpp"
#include "statusbar_model.hpp"

#include <cstdint>
#include <vector>

namespace editor::statusbar {

namespace SU = ToolEngine::SpriteUi;
namespace SR = ToolEngine::SpriteUi::Rendering;
namespace ST = ToolEngine::SpriteUi::Text;

class StatusBarPanel {
 public:
  StatusBarPanel(const sui::TextAtlasSet& text, sui::SpriteUiSdlRenderer& sdl) : canvas_{text}, sdl_{sdl} {}

  // Desenha a faixa de `barHeight` px no pe de uma janela `windowW` x `windowH`. Retorna false so em falha do SDL.
  bool draw(SDL_Renderer* renderer, const Info& info, Translate tr, double nowSec, float windowW, float windowH, float barHeight) {
    theme_ = sui::Theme{};   // cores da paleta em uso (Configuracoes > Appearance)
    if (model_.refresh(nowSec, info, tr) || windowW != builtWidth_ || barHeight != builtHeight_) rebuild(windowW, barHeight);
    const sui::Box bar{0.f, windowH - barHeight, windowW, barHeight};

    std::vector<SR::UIRenderItem> items;
    const std::vector<SR::ClipRectGPU> clips{{bar.x, bar.y, bar.x + bar.w, bar.y + bar.h}};
    const sui::SU::UIHandle owner = sui::handleFor(0);
    std::uint64_t paint = 1;
    items.push_back(sui::Canvas::solidItem(bar.x, bar.y, bar.w, bar.h, theme_.panel, 0.f, owner, 0u, paint++));
    canvas_.appendItems(items, bar.x, bar.y, 0.f, bar.y, bar.y + bar.h, owner, 0u, paint);

    packet_ = extractor_.Build(++frameId_, items, nullptr, clips);
    if (!SR::IsFramePacketValid(packet_)) return true;
    return sdl_.draw(renderer, packet_);
  }

 private:
  // Monta os textos em coordenadas locais da faixa: segmentos da esquerda, idioma a direita.
  void rebuild(float windowW, float barHeight) {
    canvas_.clear();
    builtWidth_ = windowW;
    builtHeight_ = barHeight;
    const float fontSize = kFontSize;
    const float textY = (barHeight - canvas_.lineHeight(fontSize)) * 0.5f;
    float x = kPad;
    bool first = true;
    for (const Segment& segment : model_.segments()) {
      if (!first) {
        canvas_.addSolid(x, kSeparatorInset, 1.f, barHeight - 2.f * kSeparatorInset, theme_.divider, 0.2f);
        x += kSeparatorGap;
      }
      first = false;
      x += canvas_.addText(x, textY, segment.text, fontSize, colorOf(segment.tone), windowW * 0.5f);
    }
    canvas_.addText(windowW - kPad, textY, model_.language(), fontSize, theme_.textDim, windowW * 0.25f, true);
  }

  [[nodiscard]] const SU::Vec4& colorOf(Tone tone) const {
    switch (tone) {
      case Tone::Error: return theme_.error;
      case Tone::Dim: return theme_.textDim;
      case Tone::Normal: break;
    }
    return theme_.text;
  }

  static constexpr float kFontSize = 12.f;
  static constexpr float kPad = 10.f;
  static constexpr float kSeparatorGap = 12.f;
  static constexpr float kSeparatorInset = 4.f;

  sui::Canvas canvas_;
  sui::SpriteUiSdlRenderer& sdl_;
  sui::Theme theme_;
  StatusBarModel model_;
  SR::RenderExtractor extractor_;
  SR::FramePacket packet_;
  std::uint64_t frameId_ = 0;
  float builtWidth_ = -1.f;
  float builtHeight_ = -1.f;
};

}  // namespace editor::statusbar
