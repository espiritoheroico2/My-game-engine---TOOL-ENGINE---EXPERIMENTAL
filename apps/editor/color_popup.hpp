#pragma once

// Seletor de cor em lista (popup), o mesmo no Inspector (cor do objeto) e nas Configuracoes (cor customizada).
// O popup guarda o proprio estado: a cor editada (ponteiro para 3 canais, 0 a 1), a caixa na area do painel dono,
// os arrastes (saturacao/valor e matiz) e o campo HEX com edicao propria. O dono so abre, repassa ponteiro, texto
// e teclas, e chama append no quadro. Cada mudanca grava em rgb e avisa `onChange` com a cor nova.
//
// Exemplo: arrastar a area de saturacao de uma Caixa grava o vermelho/verde/azul dela (o Inspector mostra na hora).
// Nas Configuracoes, onChange recebe a cor nova e manda um pedido para cada canal que mudou.
//
// Coordenadas: a caixa e os textos do overlay sao locais ao painel dono; `origin` e a posicao do painel na tela, e
// `visible` (recorte do campo HEX) e em tela.

#include "inspector_bridge.hpp"
#include "inspector_model.hpp"
#include "sui_panel.hpp"
#include "text_edit_keys.hpp"

#include "SpriteUi/Elements/Panel.hpp"
#include "SpriteUi/Rendering/DrawList.hpp"
#include "SpriteUi/Rendering/SpriteInstanceGPU.hpp"
#include "SpriteUi/Widgets/ColorPicker.hpp"
#include "SpriteUi/Widgets/TextField.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace editor::color_popup {

namespace SU = ToolEngine::SpriteUi;
namespace Elm = SU::Elements;
namespace Wd = SU::Widgets;
namespace Edit = SU::Widgets;
namespace SR = SU::Rendering;
namespace ST = SU::Text;
namespace im = editor::inspector_model;
namespace ib = editor::inspector_bridge;

// Visual do popup, copiado do painel dono a cada quadro (o tema pode mudar).
struct Look {
  Elm::PanelStyle frame{};
  SU::Vec4 field{}, fieldFocus{}, text{}, selection{};
  float font = 14.f;        // tamanho do texto medido e desenhado no overlay
  float fieldFont = 14.f;   // tamanho do texto dentro do campo HEX (com a escala da interface)
  float zPanel = 0.95f;     // fundo e controles do seletor
};

// Popup flutuante de cor (seletor SpriteUI + campo HEX + cores comuns + alfa opcional). Edita direto o float* do dono e avisa por callback a cada mudanca.
class ColorPopup {
public:
  using Changed = std::function<void(const float rgb[3])>;

  // canvas: mede o texto e fornece o slice da fonte do campo HEX; overlay: textos do popup (o dono desenha depois).
  ColorPopup(sui::Canvas& canvas, sui::Canvas& overlay) : canvas_{canvas}, overlay_{overlay} {}

  void setLook(const Look& look) { look_ = look; }
  void setPickerStyle(const Wd::ColorPickerStyle& style) { style_ = style; }
  void onChange(Changed fn) { changed_ = std::move(fn); }

  // Altura extra que o popup soma a caixa recebida: grade de cores comuns e a linha da cor atual (com o alfa).
  static constexpr float kSwatchCols = 8.f, kSwatchRows = 6.f, kSwatchH = 16.f, kSwatchGap = 3.f;
  static constexpr float kAlphaRowH = 18.f, kExtraGap = 6.f;
  static constexpr float kExtraH = kExtraGap + kSwatchRows * kSwatchH + (kSwatchRows - 1.f) * kSwatchGap + kExtraGap + kAlphaRowH;

  // Abre sobre `rgb` (0 a 1), na caixa `box` (coordenadas locais do painel dono). `alpha` (opcional) e o ponteiro do
  // alfa (0 a 1): com ele, a linha de baixo mostra o slider de transparencia. A caixa cresce kExtraH para as cores comuns.
  void open(std::uint32_t key, float* rgb, const SU::Rect& box, float* alpha = nullptr) {
    close();
    open_ = true;
    key_ = key;
    rgb_ = rgb;
    alpha_ = alpha;
    box_ = box;
    box_.size.y += kExtraH;
    ib::SyncColorPicker(picker_, rgb);
  }

  // Fecha e encerra a edicao do HEX. window: janela do SDL para desligar o texto (nullptr se nao houver).
  void close(SDL_Window* window = nullptr) {
    open_ = false;
    rgb_ = nullptr;
    alpha_ = nullptr;
    dragArea_ = 0;
    endEdit(window);
  }

  [[nodiscard]] bool isOpen() const { return open_; }
  [[nodiscard]] std::uint32_t key() const { return key_; }
  [[nodiscard]] bool editingHex() const { return editing_; }
  [[nodiscard]] const SU::Rect& box() const { return box_; }

  // Ponto (local ao dono) dentro da caixa do popup: quem chama repassa o ponteiro para pointer().
  [[nodiscard]] bool contains(SU::Vec2 p) const { return open_ && inside(box_, p); }
  // Ponto sobre o campo HEX enquanto ele esta em edicao (o cursor vira de texto).
  [[nodiscard]] bool overHex(SU::Vec2 p) const { return editing_ && inside(pickerLayout().hexField, p); }

  // Ponteiro (local ao dono). Quem chama so repassa o que esta dentro da caixa (contains) ou um arrasto em curso.
  void pointer(Elm::PointerPhase phase, SU::Vec2 p, SDL_Window* window) {
    if (!open_) return;
    const Wd::ColorPickerLayout layout = pickerLayout();
    if (phase == Elm::PointerPhase::Down && editing_ && !inside(layout.hexField, p)) endEdit(window);
    if (phase == Elm::PointerPhase::Down && inside(layout.hexField, p)) {
      if (!editing_) startHexEdit(window);
      const ST::TextLayoutResult lay = canvas_.layout(hex_.Edit().Text(), look_.font);
      hex_.PointerDown(SU::Vec2{p.x - layout.hexField.position.x, p.y - layout.hexField.position.y}, layout.hexField.size, lay,
                       fieldStyle(layout.hexField));
      return;
    }
    if (editing_ && hex_.DraggingSelection()) {
      if (phase == Elm::PointerPhase::Move) {
        const ST::TextLayoutResult lay = canvas_.layout(hex_.Edit().Text(), look_.font);
        hex_.PointerMove(SU::Vec2{p.x - layout.hexField.position.x, p.y - layout.hexField.position.y}, layout.hexField.size, lay,
                         fieldStyle(layout.hexField));
        return;
      }
      if (phase == Elm::PointerPhase::Up) {
        hex_.PointerUp();
        return;
      }
    }
    if (phase == Elm::PointerPhase::Move && dragArea_ == 0) return;   // passar o ponteiro sem botao nao muda a cor
    if (phase == Elm::PointerPhase::Down && rgb_) {   // cor comum da grade: troca o RGB (o alfa fica)
      for (std::size_t i = 0; i < kSwatchCount; ++i) {
        if (!inside(swatchRect(i), p)) continue;
        swatchColor(i, rgb_);   // a cor da amostra vai direto para o RGB atual
        ib::SyncColorPicker(picker_, rgb_);
        emit();
        return;
      }
    }
    if (alpha_ && ((phase == Elm::PointerPhase::Down && inside(alphaTrack(), p)) || (phase == Elm::PointerPhase::Move && dragArea_ == 3))) {
      if (phase == Elm::PointerPhase::Down) dragArea_ = 3;
      const SU::Rect t = alphaTrack();
      *alpha_ = clamp01((p.x - t.position.x) / t.size.x);   // faixa do alfa: arrastar muda a transparencia
      emit();
      return;
    }
    if (phase == Elm::PointerPhase::Down || phase == Elm::PointerPhase::Move) {
      bool changed = false;
      if (inside(layout.saturationValue, p) || (phase == Elm::PointerPhase::Move && dragArea_ == 1)) {
        if (phase == Elm::PointerPhase::Down) dragArea_ = 1;
        const float s = clamp01((p.x - layout.saturationValue.position.x) / layout.saturationValue.size.x);
        const float v = clamp01(1.f - (p.y - layout.saturationValue.position.y) / layout.saturationValue.size.y);
        changed = picker_.DragSaturationValue(s, v);
      } else if (inside(layout.hue, p) || (phase == Elm::PointerPhase::Move && dragArea_ == 2)) {
        if (phase == Elm::PointerPhase::Down) dragArea_ = 2;
        const float h = clamp01((p.x - layout.hue.position.x) / layout.hue.size.x);
        changed = picker_.DragHue(h);
      }
      if (changed && rgb_) {
        ib::ReadColorPicker(picker_, rgb_);
        emit();
      }
    }
    if (phase == Elm::PointerPhase::Up) dragArea_ = 0;
  }

  // Texto digitado: vai para o campo HEX quando ele esta em edicao. Devolve true se o popup consumiu.
  bool text(const char* s, SDL_Window* window) {
    (void)window;
    if (!editing_ || !s) return false;
    hex_.Edit().InsertText(s);
    commitHex();
    return true;
  }

  // Tecla no campo HEX: Enter e Esc encerram a edicao; o resto e edicao de texto. Devolve true se o popup consumiu.
  bool keyDown(SDL_Keycode code, SDL_Keymod mod, SDL_Window* window) {
    if (!editing_) return false;
    if (code == SDLK_ESCAPE || code == SDLK_RETURN || code == SDLK_KP_ENTER) {
      endEdit(window);
      return true;
    }
    if (text_keys::ApplyEditKey(hex_, code, mod)) commitHex();
    return true;
  }

  // Desenho: fundo, seletor, campo HEX (editavel ou so leitura) e os canais R, G, B. Os itens entram em `items` com o
  // recorte `clipIndex`; os textos vao para o overlay do dono, na mesma ordem.
  void append(std::vector<SR::UIRenderItem>& items, std::uint64_t& paint, SU::UIHandle owner, SU::Vec2 origin,
              std::uint32_t clipIndex, const SU::Rect& visible) {
    if (!open_) return;
    Elm::Panel frame;
    frame.border = box_;
    appendShifted(items, Elm::BuildPanelItems(frame, look_.frame, look_.zPanel, paint), origin, owner, clipIndex);
    const Wd::ColorPickerLayout layout = pickerLayout();
    appendShifted(items, Wd::BuildColorPickerItems(picker_, layout, owner, style_, look_.zPanel, paint), origin, owner, clipIndex);
    if (editing_) {
      const SU::Rect screen = shifted(layout.hexField, origin);
      std::vector<SR::ClipRectGPU> clips{{visible.position.x, visible.position.y, visible.position.x + visible.size.x,
                                          visible.position.y + visible.size.y}};
      Edit::TextFieldRenderTarget target;
      target.owner = owner;
      target.textureSlice = canvas_.sliceFor(look_.font);
      target.zOrder = look_.zPanel;
      target.paintOrder = paint;
      const ST::TextLayoutResult lay = canvas_.layout(hex_.Edit().Text(), look_.font);
      std::vector<SR::UIRenderItem> built = hex_.Build(screen, lay, fieldStyle(layout.hexField), target, clips);
      for (SR::UIRenderItem& item : built) item.clipIndex = clipIndex;
      paint += built.size() + 1;
      items.insert(items.end(), built.begin(), built.end());
    } else {
      overlay_.addText(layout.hexField.position.x + 6.f, layout.hexField.position.y + 5.f, im::HexText(rgb_), look_.font, look_.text,
                       layout.hexField.size.x - 10.f);
    }
    static const Wd::ColorChannel channels[3] = {Wd::ColorChannel::Red, Wd::ColorChannel::Green, Wd::ColorChannel::Blue};
    for (std::size_t i = 0; i < 3; ++i) {   // so leitura: a digitacao e pelo HEX
      const SU::Rect r = layout.channelFields[i];
      overlay_.addText(r.position.x + 6.f, r.position.y + 5.f, picker_.ChannelText(channels[i]), look_.font - 1.f, look_.text,
                       r.size.x - 8.f);
    }
    appendExtras();
  }

private:
  // Ponto dentro do retangulo (borda direita/inferior exclusiva).
  [[nodiscard]] static bool inside(const SU::Rect& r, SU::Vec2 p) {
    return p.x >= r.position.x && p.y >= r.position.y && p.x < r.position.x + r.size.x && p.y < r.position.y + r.size.y;
  }
  static float clamp01(float v) { return std::clamp(v, 0.f, 1.f); }
  // Retangulo deslocado pela origem (local -> tela).
  static SU::Rect shifted(const SU::Rect& r, SU::Vec2 origin) {
    return {{r.position.x + origin.x, r.position.y + origin.y}, r.size};
  }

  // Itens montados em coordenadas locais: somam a origem e vao para o dono e o recorte do chamador.
  static void appendShifted(std::vector<SR::UIRenderItem>& items, std::vector<SR::UIRenderItem> built, SU::Vec2 origin,
                            SU::UIHandle owner, std::uint32_t clipIndex) {
    for (SR::UIRenderItem& item : built) {
      item.handle = owner;
      item.clipIndex = clipIndex;
      item.layout.resolvedPosition.x += origin.x;
      item.layout.resolvedPosition.y += origin.y;
    }
    items.insert(items.end(), built.begin(), built.end());
  }

  // Grade de cores comuns: 6 linhas de 8 = 48 amostras. Linha 0 = cinzas (branco a preto); linhas 1 a 5 = 8 matizes
  // em 5 variacoes (pastel, claro, vivo, escuro, muito escuro). Calculada, para nao digitar 48 cores na mao.
  static constexpr std::size_t kSwatchCount = 48;
  static void swatchColor(std::size_t i, float out[3]) {
    const std::size_t row = i / 8, col = i % 8;
    if (row == 0) {   // cinzas
      const float g = 1.f - static_cast<float>(col) / 7.f;
      out[0] = out[1] = out[2] = g;
      return;
    }
    static constexpr float sat[5] = {0.35f, 0.7f, 1.f, 1.f, 1.f};    // saturacao por linha
    static constexpr float val[5] = {1.f, 1.f, 1.f, 0.7f, 0.45f};    // brilho por linha
    const float s = sat[row - 1], v = val[row - 1];
    const float h = static_cast<float>(col) / 8.f * 6.f;               // matiz em setores de 0 a 6
    const int sector = static_cast<int>(h) % 6;
    const float f = h - static_cast<float>(static_cast<int>(h));
    const float p = v * (1.f - s), q = v * (1.f - s * f), t = v * (1.f - s * (1.f - f));
    switch (sector) {   // conversao HSV para RGB, setor a setor
      case 0: out[0] = v; out[1] = t; out[2] = p; break;
      case 1: out[0] = q; out[1] = v; out[2] = p; break;
      case 2: out[0] = p; out[1] = v; out[2] = t; break;
      case 3: out[0] = p; out[1] = q; out[2] = v; break;
      case 4: out[0] = t; out[1] = p; out[2] = v; break;
      default: out[0] = v; out[1] = p; out[2] = q; break;
    }
  }

  // Area interna da caixa (sem a margem e sem a faixa extra de baixo): e onde o seletor principal cabe.
  [[nodiscard]] SU::Rect pickerInner() const {
    return SU::Rect{{box_.position.x + 8.f, box_.position.y + 8.f}, {box_.size.x - 16.f, box_.size.y - 16.f - kExtraH}};
  }

  // Caixa da cor comum de indice i na grade (abaixo do seletor).
  [[nodiscard]] SU::Rect swatchRect(std::size_t i) const {
    const SU::Rect in = pickerInner();
    const float w = (in.size.x - kSwatchGap * (kSwatchCols - 1.f)) / kSwatchCols;
    const float col = static_cast<float>(i % static_cast<std::size_t>(kSwatchCols));
    const float row = static_cast<float>(i / static_cast<std::size_t>(kSwatchCols));
    return SU::Rect{{in.position.x + col * (w + kSwatchGap), in.position.y + in.size.y + kExtraGap + row * (kSwatchH + kSwatchGap)},
                    {w, kSwatchH}};
  }

  // Linha de baixo: a amostra da cor atual (a esquerda) e a faixa do alfa (o resto, menos o texto da porcentagem).
  [[nodiscard]] SU::Rect currentRect() const {
    const SU::Rect in = pickerInner();
    const float y = in.position.y + in.size.y + kExtraGap + kSwatchRows * kSwatchH + (kSwatchRows - 1.f) * kSwatchGap + kExtraGap;
    return SU::Rect{{in.position.x, y}, {24.f, kAlphaRowH}};
  }
  // Trilho do alfa: ocupa o espaco ao lado da amostra, deixando 44 px a direita para o texto da porcentagem.
  [[nodiscard]] SU::Rect alphaTrack() const {
    const SU::Rect c = currentRect();
    const SU::Rect in = pickerInner();
    const float x = c.position.x + c.size.x + 10.f;
    return SU::Rect{{x, c.position.y + 4.f}, {std::max(10.f, in.position.x + in.size.x - 44.f - x), kAlphaRowH - 8.f}};
  }

  // Desenha a grade de cores comuns, a amostra da cor atual e, se houver alfa, a faixa de transparencia (no overlay).
  void appendExtras() {
    if (!rgb_) return;
    const float z = look_.zPanel + 0.02f;
    for (std::size_t i = 0; i < kSwatchCount; ++i) {
      const SU::Rect r = swatchRect(i);
      float c[3];
      swatchColor(i, c);
      overlay_.addSolid(r.position.x, r.position.y, r.size.x, r.size.y, SU::Vec4{c[0], c[1], c[2], 1.f}, z, 2.f);
    }
    const SU::Rect c = currentRect();
    const float a = alpha_ ? *alpha_ : 1.f;
    overlay_.addSolid(c.position.x, c.position.y, c.size.x, c.size.y, SU::Vec4{0.f, 0.f, 0.f, 1.f}, z, 2.f);   // fundo escuro sob a cor
    overlay_.addSolid(c.position.x, c.position.y, c.size.x, c.size.y, SU::Vec4{rgb_[0], rgb_[1], rgb_[2], a}, z + 0.005f, 2.f);
    if (!alpha_) return;
    const SU::Rect t = alphaTrack();
    overlay_.addSolid(t.position.x, t.position.y, t.size.x, t.size.y, look_.field, z, 2.f);
    overlay_.addSolid(t.position.x, t.position.y, t.size.x * a, t.size.y, SU::Vec4{rgb_[0], rgb_[1], rgb_[2], 1.f}, z + 0.005f, 2.f);
    overlay_.addSolid(t.position.x + t.size.x * a - 2.f, t.position.y - 2.f, 4.f, t.size.y + 4.f, look_.text, z + 0.01f, 1.f);
    overlay_.addText(t.position.x + t.size.x + 8.f, c.position.y + 2.f, "A " + std::to_string(static_cast<int>(a * 100.f + 0.5f)) + "%",
                     look_.font - 1.f, look_.text, 40.f);
  }

  // Layout do seletor principal dentro da area interna.
  [[nodiscard]] Wd::ColorPickerLayout pickerLayout() const { return Wd::LayoutColorPicker(pickerInner(), style_); }

  // Estilo do campo HEX a partir do visual atual (cores do tema e escala da fonte).
  [[nodiscard]] Edit::TextFieldStyle fieldStyle(const SU::Rect& box) const {
    Edit::TextFieldStyle style;
    style.background = look_.field;
    style.focusBackground = look_.fieldFocus;
    style.text = look_.text;
    style.selection = look_.selection;
    style.caret = look_.text;
    style.paddingX = 6.f;
    style.height = box.size.y;
    style.fontSize = look_.fieldFont;
    return style;
  }

  // Entra em edicao do HEX: campo recebe o texto da cor atual, ganha foco e liga a entrada de texto do SDL.
  void startHexEdit(SDL_Window* window) {
    hex_ = Edit::TextField(im::HexText(rgb_));
    hex_.SetFocus(true);
    editing_ = true;
    if (window) SDL_StartTextInput(window);
  }

  // Sai da edicao do HEX e desliga a entrada de texto do SDL.
  void endEdit(SDL_Window* window) {
    if (!editing_) return;
    hex_.SetFocus(false);
    editing_ = false;
    if (window) SDL_StopTextInput(window);
  }

  // Campo HEX mudou: cor valida vira a amostra, o canal e o dono (rgb) e o aviso de mudanca.
  void commitHex() {
    float rgb[3] = {0.f, 0.f, 0.f};
    if (!open_ || !im::ApplyHexColor(hex_.Edit().Text(), rgb)) return;
    (void)picker_.SetColor(SU::Vec4{rgb[0], rgb[1], rgb[2], 1.0f});
    if (rgb_) {
      rgb_[0] = rgb[0];
      rgb_[1] = rgb[1];
      rgb_[2] = rgb[2];
    }
    emit();
  }

  // Avisa o dono (callback) que a cor mudou.
  void emit() {
    if (rgb_ && changed_) changed_(rgb_);
  }

  sui::Canvas& canvas_;
  sui::Canvas& overlay_;
  Look look_{};
  Wd::ColorPickerStyle style_{};
  Changed changed_;

  bool open_ = false;
  std::uint32_t key_ = 0;
  float* rgb_ = nullptr;
  float* alpha_ = nullptr;   // alfa (0 a 1), ou nullptr quando a cor nao tem transparencia
  SU::Rect box_{};
  Wd::ColorPickerControl picker_;
  int dragArea_ = 0;   // 1 = saturacao/valor, 2 = matiz: a area do arrasto em curso

  bool editing_ = false;
  Edit::TextField hex_;
};

}  // namespace editor::color_popup
