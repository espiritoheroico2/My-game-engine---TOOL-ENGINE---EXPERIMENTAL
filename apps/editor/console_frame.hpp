#pragma once

// Moldura flutuante do Console (SpriteUI): posicao da janela, arraste pelo grip, X para fechar.
// So regras e geometria, sem SDL nem desenho. O painel (console_panel.hpp) desenha a moldura e repassa os
// cliques para ca. Testado sem janela em tests/test_console_frame.cpp (Editor.ConsoleFrame).
//
// Padrao de moldura (docs/trocarjanelas.md, secao 1.1): grip a esquerda do titulo (so ele arrasta), X a direita.
// O icone "?" ainda nao existe aqui (pendente, ver docs/trocarjanelas.md, secao 4.3).
#include "SpriteUi/Widgets/DockInteraction.hpp"
#include "SpriteUi/Widgets/WindowFrame.hpp"
#include "SpriteUi/Widgets/WindowFramePaint.hpp"

#include <algorithm>
#include <cmath>

namespace editor::console_frame {

namespace SU = ToolEngine::SpriteUi;
namespace SW = ToolEngine::SpriteUi::Widgets;

struct Metrics {
  static constexpr float kWidth = 720.f;    // mesmo tamanho que o ImGui usava (720 x 280)
  static constexpr float kHeight = 280.f;
  static constexpr float kMargin = 16.f;    // folga minima da borda da janela ao cabecalho
  static constexpr float kGripWidth = 14.f;
  static constexpr float kBottomGap = 40.f;  // primeira posicao: a janela fica acima da barra de status
};

enum class Event : unsigned char { None, Close, DragStart };

class Frame {
 public:
  // Primeira vez: centralizada na parte de baixo. Depois disso a posicao e a que o usuario arrastou.
  // A janela nunca sai da area visivel: a posicao e prendida a cada quadro.
  void place(float windowW, float windowH) {
    const float w = std::min(Metrics::kWidth, std::max(1.f, windowW - 2.f * Metrics::kMargin));
    const float h = std::min(Metrics::kHeight, std::max(1.f, windowH - 2.f * Metrics::kMargin));
    if (!placed_) {
      pos_ = {std::floor((windowW - w) * 0.5f), std::max(0.f, std::floor(windowH - h - Metrics::kBottomGap))};
      placed_ = true;
    }
    pos_.x = std::clamp(pos_.x, 0.f, std::max(0.f, windowW - w));
    pos_.y = std::clamp(pos_.y, 0.f, std::max(0.f, windowH - h));
    frame_ = SU::Rect{pos_, {w, h}};
  }

  [[nodiscard]] SU::Rect rect() const { return frame_; }
  [[nodiscard]] SU::Rect content() const { return SW::WindowFrameContent(frame_, metrics_); }
  [[nodiscard]] SU::Rect header() const { return SW::WindowFrameHeader(frame_, metrics_, false); }
  [[nodiscard]] SU::Rect titleRect() const { return SW::WindowFrameTitleRect(frame_, metrics_, false); }
  [[nodiscard]] SU::Rect gripRect() const { return SW::WindowGripRect(header(), Metrics::kGripWidth); }
  [[nodiscard]] const SW::WindowFrameMetrics& metrics() const { return metrics_; }
  [[nodiscard]] bool dragging() const { return dragging_; }
  [[nodiscard]] bool placed() const { return placed_; }
  [[nodiscard]] SU::Vec2 position() const { return pos_; }

  // Ponteiro pressionado. X fecha; so o grip inicia o arraste. Qualquer outro ponto nao faz nada.
  Event pointerDown(SU::Vec2 p) {
    if (!placed_) return Event::None;
    if (SW::WindowFramePartAt(frame_, metrics_, false, p) == SW::WindowFramePart::Close) return Event::Close;
    if (inside(gripRect(), p)) {
      dragging_ = true;
      grab_ = {p.x - pos_.x, p.y - pos_.y};
      return Event::DragStart;
    }
    return Event::None;
  }

  // Movimento com o botao preso: a janela segue o ponteiro. Sem arraste, nada muda.
  void pointerMove(SU::Vec2 p) {
    if (dragging_) pos_ = {p.x - grab_.x, p.y - grab_.y};
  }

  void pointerUp() { dragging_ = false; }

  // Estado de interacao que o painel precisa resetar quando a janela some.
  void reset() { dragging_ = false; }

 private:
  static bool inside(SU::Rect r, SU::Vec2 p) {
    return p.x >= r.position.x && p.x < r.position.x + r.size.x && p.y >= r.position.y && p.y < r.position.y + r.size.y;
  }

  SW::WindowFrameMetrics metrics_{[] {
    SW::WindowFrameMetrics m{};
    m.headerHeight = 30.f;
    m.cornerRadius = 6.f;
    return m;
  }()};
  SU::Rect frame_{};
  SU::Vec2 pos_{};
  SU::Vec2 grab_{};
  bool placed_ = false;
  bool dragging_ = false;
};

}  // namespace editor::console_frame
