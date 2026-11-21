#pragma once

// Moldura flutuante do Console (SpriteUI): posicao da janela, arraste pelo grip, X para fechar.
// So regras e geometria, sem SDL nem desenho. O painel (console_panel.hpp) desenha a moldura e repassa os
// cliques para ca. Testado sem janela em tests/test_window_frame.cpp (Editor.ConsoleFrame).
//
// Padrao de moldura (docs/trocarjanelas.md, secao 1.1), da esquerda para a direita no cabecalho: grip (so ele
// arrasta), icone "?" (abre a Help na secao da janela), titulo, minimizar e X. O minimizar recolhe a janela ao
// cabecalho; a altura volta quando clicar de novo.
#include "SpriteUi/Widgets/DockInteraction.hpp"
#include "SpriteUi/Widgets/WindowFrame.hpp"
#include "SpriteUi/Widgets/WindowFramePaint.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace editor::window_frame {

namespace SU = ToolEngine::SpriteUi;
namespace SW = ToolEngine::SpriteUi::Widgets;

struct Metrics {
  static constexpr float kWidth = 720.f;    // mesmo tamanho que o ImGui usava (720 x 280)
  static constexpr float kHeight = 280.f;
  static constexpr float kMargin = 16.f;    // folga minima da borda da janela ao cabecalho
  static constexpr float kGripWidth = 14.f;
  static constexpr float kBottomGap = 40.f;  // primeira posicao: a janela fica acima da barra de status
  static constexpr float kHelpSize = 18.f;   // diametro do circulo do "?"
  static constexpr float kHelpGap = 4.f;     // espaco entre o grip e o circulo, e entre o circulo e o titulo
};

enum class Event : unsigned char { None, Close, DragStart, Help, Minimize };

class Frame {
 public:
  // Primeira vez: na parte de baixo, centralizada (firstX < 0) ou em firstX. Depois disso a posicao e a que o
  // usuario arrastou. A janela nunca sai da area visivel: a posicao e prendida a cada quadro.
  // width: largura pedida (a janela encolhe se a tela for menor).
  void place(float windowW, float windowH, float width = Metrics::kWidth, float height = Metrics::kHeight, float firstX = -1.f) {
    const float w = std::min(width, std::max(1.f, windowW - 2.f * Metrics::kMargin));
    const float h = std::min(height, std::max(1.f, windowH - 2.f * Metrics::kMargin));
    const float shown = minimized_ ? std::min(metrics_.headerHeight, h) : h;   // minimizada: so o cabecalho
    fullHeight_ = h;
    if (!placed_) {
      const float x = firstX >= 0.f ? firstX : (windowW - w) * 0.5f;
      pos_ = {std::floor(x), std::max(0.f, std::floor(windowH - h - Metrics::kBottomGap))};
      placed_ = true;
    }
    pos_.x = std::clamp(pos_.x, 0.f, std::max(0.f, windowW - w));
    pos_.y = std::clamp(pos_.y, 0.f, std::max(0.f, windowH - shown));
    frame_ = SU::Rect{pos_, {w, shown}};
  }

  // Janela no dock: `dock` e o retangulo do conteudo que o dock manda neste quadro (o arraste nao move a janela
  // acoplada). Sem `dock`: volta a flutuar na posicao em que estava (undock). Minimizada, so o cabecalho.
  void placeIn(float windowW, float windowH, std::optional<SU::Rect> dock, float width = Metrics::kWidth,
               float height = Metrics::kHeight, float firstX = -1.f) {
    if (dock) {
      docked_ = true;
      fullHeight_ = dock->size.y;
      const float shown = minimized_ ? std::min(metrics_.headerHeight, dock->size.y) : dock->size.y;
      frame_ = SU::Rect{dock->position, {dock->size.x, shown}};
      placed_ = true;   // acoplada conta como colocada: o painel desenha so depois disso
      return;
    }
    if (docked_) undock(frame_.position);
    place(windowW, windowH, width, height, firstX);
  }

  // Sai do dock na posicao `at` (flutuante). A janela nao e recentrada depois disso.
  void undock(SU::Vec2 at) {
    docked_ = false;
    pos_ = at;
    placed_ = true;
  }

  [[nodiscard]] bool docked() const { return docked_; }

  [[nodiscard]] SU::Rect rect() const { return frame_; }
  [[nodiscard]] SU::Rect content() const { return SW::WindowFrameContent(frame_, metrics_); }
  [[nodiscard]] SU::Rect header() const { return SW::WindowFrameHeader(frame_, metrics_, false); }
  [[nodiscard]] SU::Rect titleRect() const { return SW::WindowFrameTitleRect(frame_, metrics_, false); }
  [[nodiscard]] SU::Rect gripRect() const { return SW::WindowGripRect(header(), Metrics::kGripWidth); }

  // Circulo do "?": logo depois do grip, centralizado na altura do cabecalho.
  [[nodiscard]] SU::Rect helpRect() const {
    const SU::Rect grip = gripRect();
    const SU::Rect head = header();
    const float x = grip.position.x + grip.size.x + Metrics::kHelpGap;
    const float y = head.position.y + (head.size.y - Metrics::kHelpSize) * 0.5f;
    return SU::Rect{{x, y}, {Metrics::kHelpSize, Metrics::kHelpSize}};
  }
  // Onde comeca o texto do titulo: depois do circulo do "?".
  [[nodiscard]] float titleTextX() const { return helpRect().position.x + helpRect().size.x + Metrics::kHelpGap; }
  [[nodiscard]] bool minimized() const { return minimized_; }
  [[nodiscard]] const SW::WindowFrameMetrics& metrics() const { return metrics_; }
  [[nodiscard]] bool dragging() const { return dragging_; }
  [[nodiscard]] bool placed() const { return placed_; }
  [[nodiscard]] SU::Vec2 position() const { return pos_; }

  // Ponteiro pressionado. X fecha; so o grip inicia o arraste. Qualquer outro ponto nao faz nada.
  Event pointerDown(SU::Vec2 p) {
    if (!placed_) return Event::None;
    const SW::WindowFramePart part = SW::WindowFramePartAt(frame_, metrics_, false, p);
    if (part == SW::WindowFramePart::Close) return Event::Close;
    if (part == SW::WindowFramePart::Minimize) {
      toggleMinimized();
      return Event::Minimize;
    }
    if (inside(helpRect(), p)) return Event::Help;
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

  // Minimizar (botao da moldura): recolhe ao cabecalho e volta a altura cheia no segundo clique. Usa a altura da
  // ultima place(), entao chame place() no proximo quadro para aplicar.
  void toggleMinimized() {
    minimized_ = !minimized_;
    frame_.size.y = minimized_ ? std::min(metrics_.headerHeight, frame_.size.y) : fullHeight_;
  }

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
  bool minimized_ = false;
  bool docked_ = false;
  float fullHeight_ = Metrics::kHeight;   // altura cheia, guardada para voltar do minimizado
};

}  // namespace editor::window_frame
