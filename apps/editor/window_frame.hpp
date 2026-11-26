#pragma once

#include "ui_style.hpp"
#include "window_bounds.hpp"

// Moldura flutuante do Console (SpriteUI): posicao da janela, arraste pelo grip, X para fechar.
// So regras e geometria, sem SDL nem desenho. O painel (console_panel.hpp) desenha a moldura e repassa os
// cliques para ca. Testado sem janela em tests/test_window_frame.cpp (Editor.ConsoleFrame).
//
// Padrao de moldura da esquerda para a direita no cabecalho: grip (so ele
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
  static constexpr float kWidth = 720.f;    // tamanho padrao (720 x 280)
  static constexpr float kHeight = 280.f;
  static constexpr float kMargin = 16.f;    // folga minima da borda da janela ao cabecalho
  static constexpr float kGripWidth = 14.f;
  static constexpr float kBottomGap = 40.f;  // primeira posicao: a janela fica acima da barra de status
  static constexpr float kHelpSize = 18.f;   // diametro do circulo do "?"
  static constexpr float kHelpGap = 4.f;     // espaco entre o grip e o circulo, e entre o circulo e o titulo
  static constexpr float kResizeBand = 6.f;  // faixa (px) em volta da moldura que redimensiona
  static constexpr float kKeepGrab = 80.f;   // px visiveis de uma janela solta quase toda fora da area
  static constexpr float kMinWidth = 220.f;  // menor largura ao redimensionar
};

enum class Event : unsigned char { None, Close, DragStart, Help, Minimize, Resize };

class Frame {
 public:
  // Primeira vez: na parte de baixo, centralizada (firstX < 0) ou em firstX. Depois disso a posicao e a que o
  // usuario arrastou. A janela nunca sai da area visivel: a posicao e prendida a cada quadro.
  // width: largura pedida (a janela encolhe se a tela for menor).
  void place(float windowW, float windowH, float width = Metrics::kWidth, float height = Metrics::kHeight, float firstX = -1.f) {
    lastWinW_ = windowW;
    lastWinH_ = windowH;
    // Tamanho que o usuario arrastou tem prioridade sobre o pedido pelo painel.
    const float reqW = userSized_ ? userSize_.x : width, reqH = userSized_ ? userSize_.y : height;
    const float w = std::min(reqW, std::max(1.f, windowW - 2.f * Metrics::kMargin));
    const float h = std::min(reqH, std::max(1.f, windowH - 2.f * Metrics::kMargin));
    const float shown = minimized_ ? std::min(metrics_.headerHeight, h) : h;   // minimizada: so o cabecalho
    fullHeight_ = h;
    if (!placed_) {
      const float x = firstX >= 0.f ? firstX : (windowW - w) * 0.5f;
      pos_ = {std::floor(x), std::max(0.f, std::floor(windowH - h - Metrics::kBottomGap))};
      placed_ = true;
    }
    // Janela solta: o cabecalho fica na area e uma faixa visivel fica pegavel (window_bounds.hpp).
    const auto kept = editor::window_bounds::keepInside({pos_.x, pos_.y}, w, {0.f, 0.f, windowW, windowH},
                                                        metrics_.headerHeight, Metrics::kKeepGrab);
    pos_ = {kept.x, kept.y};
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
  [[nodiscard]] SU::Rect header() const { return SW::WindowFrameHeader(frame_, metrics_, docked_); }
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
  // Raio dos cantos vem de Configuracoes > Appearance: relido a cada quadro.
  void refreshStyle() { metrics_.cornerRadius = editor::ui_style::windowRadius(); }
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
    const int edge = edgeAt(p);
    if (edge != 0) {
      resizing_ = true;
      resizeMask_ = edge;
      startRect_ = frame_;
      startPointer_ = p;
      return Event::Resize;
    }
    return Event::None;
  }

  // Botoes da moldura (X, minimizar, "?"), sem iniciar arraste nem redimensionamento. Quem decide os gestos e o host
  // do dock; a moldura so responde aos botoes que ficam no proprio cabecalho.
  Event buttonAt(SU::Vec2 p) {
    if (!placed_) return Event::None;
    const SW::WindowFramePart part = SW::WindowFramePartAt(frame_, metrics_, false, p);
    if (part == SW::WindowFramePart::Close) return Event::Close;
    if (part == SW::WindowFramePart::Minimize) {
      toggleMinimized();
      return Event::Minimize;
    }
    if (inside(helpRect(), p)) return Event::Help;
    return Event::None;
  }

  // Inicia o redimensionamento quando o ponteiro esta numa borda (faixa de fora ou de dentro da moldura). Os botoes
  // Minimizar e X do cabecalho nunca iniciam redimensionamento. O host segue com pointerMove e solta com pointerUp.
  bool beginResize(SU::Vec2 p) {
    if (!placed_) return false;
    const SW::WindowFramePart part = SW::WindowFramePartAt(frame_, metrics_, false, p);
    if (part == SW::WindowFramePart::Close || part == SW::WindowFramePart::Minimize) return false;
    const int edge = edgeAt(p);
    if (edge == 0) return false;
    resizing_ = true;
    resizeMask_ = edge;
    startRect_ = frame_;
    startPointer_ = p;
    return true;
  }

  // Redimensionar: mascara das bordas sob o ponteiro (1 esquerda, 2 direita, 4 topo, 8 base); 0 = nenhuma.
  // Acoplada e minimizada nao redimensionam: o tamanho vem do dock ou do cabecalho.
  [[nodiscard]] int edgeAt(SU::Vec2 p) const {
    if (!placed_ || docked_ || minimized_) return 0;
    const float b = Metrics::kResizeBand;
    const SU::Rect outer{{frame_.position.x - b, frame_.position.y - b}, {frame_.size.x + 2.f * b, frame_.size.y + 2.f * b}};
    if (!inside(outer, p)) return 0;
    int m = 0;
    if (p.x < frame_.position.x + b) m |= 1;
    if (p.x >= frame_.position.x + frame_.size.x - b) m |= 2;
    if (p.y < frame_.position.y + b) m |= 4;
    if (p.y >= frame_.position.y + frame_.size.y - b) m |= 8;
    return m;
  }
  [[nodiscard]] bool resizing() const { return resizing_; }

  // Movimento com o botao preso: a janela segue o ponteiro (arraste) ou muda de tamanho (borda).
  void pointerMove(SU::Vec2 p) {
    if (dragging_) pos_ = {p.x - grab_.x, p.y - grab_.y};
    if (!resizing_) return;
    const float dx = p.x - startPointer_.x, dy = p.y - startPointer_.y;
    float x = startRect_.position.x, y = startRect_.position.y, w = startRect_.size.x, h = startRect_.size.y;
    const float minW = std::min(Metrics::kMinWidth, lastWinW_), minH = std::min(metrics_.headerHeight + 60.f, lastWinH_);
    if (resizeMask_ & 1) { const float nx = std::min(x + dx, x + w - minW); w = x + w - nx; x = nx; }
    if (resizeMask_ & 2) w = std::max(minW, w + dx);
    if (resizeMask_ & 4) { const float ny = std::min(y + dy, y + h - minH); h = y + h - ny; y = ny; }
    if (resizeMask_ & 8) h = std::max(minH, h + dy);
    x = std::max(x, 0.f);
    y = std::max(y, 0.f);
    w = std::min(w, lastWinW_ - x);   // nao sai da janela do editor
    h = std::min(h, lastWinH_ - y);
    pos_ = {x, y};
    userSize_ = {w, h};
    userSized_ = true;
    fullHeight_ = h;
    frame_ = SU::Rect{pos_, {w, minimized_ ? std::min(metrics_.headerHeight, h) : h}};
  }

  void pointerUp() {
    dragging_ = false;
    resizing_ = false;
  }

  // Estado de interacao que o painel precisa resetar quando a janela some.
  void reset() {
    dragging_ = false;
    resizing_ = false;
  }

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
    m.cornerRadius = editor::ui_style::windowRadius();
    return m;
  }()};
  SU::Rect frame_{};
  SU::Vec2 pos_{};
  SU::Vec2 grab_{};
  bool placed_ = false;
  bool dragging_ = false;
  bool resizing_ = false;
  int resizeMask_ = 0;
  SU::Rect startRect_{};
  SU::Vec2 startPointer_{};
  bool userSized_ = false;      // o usuario redimensionou: userSize_ manda sobre o tamanho pedido pelo painel
  SU::Vec2 userSize_{};
  float lastWinW_ = 0.f, lastWinH_ = 0.f;
  bool minimized_ = false;
  bool docked_ = false;
  float fullHeight_ = Metrics::kHeight;   // altura cheia, guardada para voltar do minimizado
};

}  // namespace editor::window_frame
