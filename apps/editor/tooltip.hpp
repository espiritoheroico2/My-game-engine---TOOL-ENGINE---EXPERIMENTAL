// ToolEngine Editor - tooltip (balao de ajuda) ao deixar o mouse parado sobre um retangulo.
//
// COMO USAR (quem integra no loop de desenho):
//   1. Um objeto editor::tooltip::Tooltip vive junto da UI (um so para o editor inteiro).
//   2. A cada quadro, ANTES de montar as janelas:   tip.beginFrame(mouseX, mouseY, dt);
//      (mouseX/Y em coordenadas de tela; dt em segundos).
//   3. Cada controle que quer ajuda chama:          tip.target(x, y, w, h, "texto explicativo");
//      (mesmas coordenadas de tela do mouse). Se varios retangulos se cruzam, vale o ULTIMO registrado (o mais por cima).
//      Para esconder o balao enquanto o botao do mouse esta apertado/ha menu aberto: tip.suppress(true) no quadro.
//   4. No FIM do quadro, depois de montar tudo:     tip.draw(canvas, screenW, screenH);
//      `canvas` e um sui::Canvas proprio, vazio (canvas.clear() antes), em coordenadas de tela, desenhado por cima de tudo
//      (use setTextZ com Z alto e o balao sai com Z alto tambem). O balao usa palette::active() e a fonte ja carregada.
// Regras: o balao so aparece depois de kDelay (0,5 s) com o mouse PARADO (mover mais de kMoveSlop pixels reinicia a espera)
// sobre o mesmo retangulo; mudar de retangulo tambem reinicia. Fica perto do mouse e nunca sai da tela.
#pragma once
#include "sui_panel.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace editor::tooltip {

inline constexpr float kDelay = 0.5f;     // segundos de mouse parado ate o balao aparecer
inline constexpr float kMoveSlop = 3.f;   // pixels de tremor do mouse que ainda contam como "parado"
inline constexpr float kFont = 14.f;      // tamanho do texto do balao
inline constexpr float kPad = 8.f;        // margem interna do balao
inline constexpr float kMaxWidth = 320.f; // largura maxima (texto longo quebra em linhas)
inline constexpr float kOffsetX = 14.f;   // distancia do cursor
inline constexpr float kOffsetY = 18.f;

// Estado do tooltip: um retangulo candidato por quadro e o tempo que o mouse ficou parado sobre ele.
class Tooltip {
 public:
  // Comeca um quadro: guarda o mouse, acumula o tempo parado e limpa o alvo do quadro anterior.
  void beginFrame(float mouseX, float mouseY, float dt) {
    if (std::fabs(mouseX - anchorX_) > kMoveSlop || std::fabs(mouseY - anchorY_) > kMoveSlop) {   // mexeu: reinicia a espera
      anchorX_ = mouseX; anchorY_ = mouseY; still_ = 0.f;
    } else {
      still_ += std::max(0.f, dt);
    }
    mx_ = mouseX; my_ = mouseY;
    hit_ = false; suppress_ = false; text_ = nullptr;
  }

  // Registra "hover neste retangulo mostra este texto". O ultimo retangulo que contem o mouse vence.
  // `text` precisa viver ate o draw() do mesmo quadro (literal ou string do chamador).
  void target(float x, float y, float w, float h, const char* text) {
    if (!text || !*text) return;
    if (mx_ >= x && my_ >= y && mx_ < x + w && my_ < y + h) { hit_ = true; text_ = text; rx_ = x; ry_ = y; rw_ = w; rh_ = h; }
  }

  // Esconde o balao neste quadro (mouse apertado, menu aberto, arrastando...). Reinicia a espera.
  void suppress(bool on = true) { if (on) { suppress_ = true; still_ = 0.f; } }

  // true se o balao deve aparecer neste quadro: ha alvo sob o mouse, nao suprimido e parado ha kDelay.
  [[nodiscard]] bool visible() const { return hit_ && !suppress_ && text_ && still_ >= kDelay; }

  // Posicao do canto superior esquerdo do balao de tamanho (w, h): ao lado do mouse, dentro da tela (vira para a esquerda/cima se nao couber).
  static void place(float mouseX, float mouseY, float w, float h, float screenW, float screenH, float& outX, float& outY) {
    float x = mouseX + kOffsetX, y = mouseY + kOffsetY;
    if (x + w > screenW) x = mouseX - kOffsetX - w;
    if (y + h > screenH) y = mouseY - kOffsetY - h;
    outX = std::clamp(x, 0.f, std::max(0.f, screenW - w));
    outY = std::clamp(y, 0.f, std::max(0.f, screenH - h));
  }

  // Desenha o balao no canvas dado (fundo, contorno e texto quebrado em linhas), se visible(). Nao faz nada caso contrario.
  void draw(sui::Canvas& canvas, float screenW, float screenH) {
    // Alvo diferente do quadro anterior (outro retangulo): a espera recomeca.
    const bool changed = hit_ && (rx_ != lastRx_ || ry_ != lastRy_ || rw_ != lastRw_ || rh_ != lastRh_);
    if (hit_) { lastRx_ = rx_; lastRy_ = ry_; lastRw_ = rw_; lastRh_ = rh_; }
    else lastRw_ = lastRh_ = 0.f;
    if (changed) still_ = 0.f;
    if (!visible()) return;
    const std::string text = text_;
    const auto pal = palette::active();
    const float natural = canvas.layout(text, kFont).size.x;
    const float inner = std::min(natural, kMaxWidth);
    // Texto: linha unica se cabe; senao paragrafo quebrado em kMaxWidth. O Z maior do fundo/contorno fica por baixo do texto (setTextZ do canvas).
    float bx = 0.f, by = 0.f, th = 0.f;
    const float lineH = canvas.lineHeight(kFont);
    place(mx_, my_, inner + 2.f * kPad, lineH + 2.f * kPad, screenW, screenH, bx, by);   // posicao provisoria (1 linha)
    if (natural > kMaxWidth) th = canvas.addParagraph(bx + kPad, by + kPad, text, kFont, sui::PaletteColor(pal.text), inner);
    else { canvas.addText(bx + kPad, by + kPad, text, kFont, sui::PaletteColor(pal.text), inner + 1.f); th = lineH; }
    const float bw = inner + 2.f * kPad, bh = th + 2.f * kPad;
    canvas.addSolid(bx - 1.f, by - 1.f, bw + 2.f, bh + 2.f, sui::PaletteColor(pal.border), 0.95f, 5.f);   // contorno
    canvas.addSolid(bx, by, bw, bh, sui::PaletteColor(pal.surface), 0.96f, 4.f);                            // fundo
  }

  // Tempo (s) que o mouse esta parado (para teste/depuracao).
  [[nodiscard]] float stillSeconds() const { return still_; }

 private:
  float mx_ = 0.f, my_ = 0.f;           // mouse deste quadro
  float anchorX_ = -1e9f, anchorY_ = -1e9f; // posicao onde o mouse comecou a ficar parado
  float still_ = 0.f;                   // segundos parado
  bool hit_ = false, suppress_ = false; // ha alvo sob o mouse / balao suprimido
  const char* text_ = nullptr;          // texto do alvo deste quadro
  float rx_ = 0.f, ry_ = 0.f, rw_ = 0.f, rh_ = 0.f;             // retangulo do alvo deste quadro
  float lastRx_ = 0.f, lastRy_ = 0.f, lastRw_ = 0.f, lastRh_ = 0.f; // retangulo do alvo do quadro anterior
};

} // namespace editor::tooltip
