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

class ColorPopup {
public:
  using Changed = std::function<void(const float rgb[3])>;

  // canvas: mede o texto e fornece o slice da fonte do campo HEX; overlay: textos do popup (o dono desenha depois).
  ColorPopup(sui::Canvas& canvas, sui::Canvas& overlay) : canvas_{canvas}, overlay_{overlay} {}

  void setLook(const Look& look) { look_ = look; }
  void setPickerStyle(const Wd::ColorPickerStyle& style) { style_ = style; }
  void onChange(Changed fn) { changed_ = std::move(fn); }

  // Abre sobre `rgb` (0 a 1), na caixa `box` (coordenadas locais do painel dono).
  void open(std::uint32_t key, float* rgb, const SU::Rect& box) {
    close();
    open_ = true;
    key_ = key;
    rgb_ = rgb;
    box_ = box;
    ib::SyncColorPicker(picker_, rgb);
  }

  // Fecha e encerra a edicao do HEX. window: janela do SDL para desligar o texto (nullptr se nao houver).
  void close(SDL_Window* window = nullptr) {
    open_ = false;
    rgb_ = nullptr;
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
  }

private:
  [[nodiscard]] static bool inside(const SU::Rect& r, SU::Vec2 p) {
    return p.x >= r.position.x && p.y >= r.position.y && p.x < r.position.x + r.size.x && p.y < r.position.y + r.size.y;
  }
  static float clamp01(float v) { return std::clamp(v, 0.f, 1.f); }
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

  [[nodiscard]] Wd::ColorPickerLayout pickerLayout() const {
    const SU::Rect inner{{box_.position.x + 8.f, box_.position.y + 8.f}, {box_.size.x - 16.f, box_.size.y - 16.f}};
    return Wd::LayoutColorPicker(inner, style_);
  }

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

  void startHexEdit(SDL_Window* window) {
    hex_ = Edit::TextField(im::HexText(rgb_));
    hex_.SetFocus(true);
    editing_ = true;
    if (window) SDL_StartTextInput(window);
  }

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
  SU::Rect box_{};
  Wd::ColorPickerControl picker_;
  int dragArea_ = 0;   // 1 = saturacao/valor, 2 = matiz: a area do arrasto em curso

  bool editing_ = false;
  Edit::TextField hex_;
};

}  // namespace editor::color_popup
