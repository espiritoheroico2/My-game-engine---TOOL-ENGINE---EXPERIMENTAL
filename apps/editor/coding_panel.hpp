// coding_panel.hpp - Janela Coding, em SpriteUI.
//
// Duas partes: a lista dos codigos-fonte do projeto (esquerda, texto de 16 por padrao, com slider e campo numerico no
// topo) e o editor do codigo escolhido (direita, multilinha, com numeros de linha). A barra do topo tem Save, o nome do
// arquivo, um slider com campo numerico do tamanho do codigo e o botao que mostra ou oculta a lista. O slider arrasta
// numa faixa curta; o numero digitado vale de 4 a 200.
//
// A moldura vem de framed::Chrome (dock). O corpo e desenhado aqui com view::ShapeList e os itens do campo
// (Elements::TextField). Eventos chegam por handleEvent. Clicar num codigo salva o que estava aberto e
// abre o clicado. Ctrl+S salva.
#pragma once

#include "framed_window.hpp"
#include "free_values.hpp"
#include "i18n.hpp"
#include "slider_value_edit.hpp"
#include "script_check.hpp"
#include "spriteui_host.hpp"
#include "text_atlas_set.hpp"
#include "view_painter.hpp"
#include "view_shapes.hpp"
#include "ui_scrollbar.hpp"
#include "ui_style.hpp"

#include "SpriteUi/Elements/ElementTree.hpp"
#include "SpriteUi/Elements/Slider.hpp"
#include "SpriteUi/Elements/TextField.hpp"
#include "SpriteUi/Rendering/DrawList.hpp"
#include "SpriteUi/Text/TextLayout.hpp"
#include "SpriteUi/Text/TruetypeAtlas.hpp"
#include "SpriteUi/Widgets/TextEdit.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace editor::coding {

namespace fs = std::filesystem;
namespace SU = ToolEngine::SpriteUi;
namespace Elm = ToolEngine::SpriteUi::Elements;
namespace ST = ToolEngine::SpriteUi::Text;
namespace SR = ToolEngine::SpriteUi::Rendering;
namespace Wd = ToolEngine::SpriteUi::Widgets;

// Cores do painel, tiradas da paleta em uso (Configuracoes > Appearance).
struct Ink {
  view::Color text, dim, field, list, rowHover, rowSel, button, buttonOn, border, error, warning, ok;
};

// Janela Coding: lista dos codigos-fonte do projeto + editor multilinha com numeros de linha, diagnosticos de scripts .tool.js e salvar (Ctrl+S).
class Panel {
 public:
  static constexpr float kFont = 12.f;          // barra, nomes e lista: tamanho fixo
  static constexpr float kMinCodeFont = 12.f;   // tamanho do codigo (slider), em pixels
  static constexpr float kMaxCodeFont = 28.f;
  static constexpr float kPad = 8.f;
  static constexpr float kBar = 32.f;
  static constexpr float kRow = 18.f;           // altura minima da linha da lista
  static constexpr float kMinListFont = 10.f;   // faixa do slider do tamanho do texto da lista (digitado vai de 4 a 200)
  static constexpr float kMaxListFont = 32.f;
  static constexpr float kDefaultListFont = 16.f;   // texto da lista maior que o antigo (12)
  static constexpr float kListHeadH = 26.f;     // faixa do topo da lista com o slider do tamanho
  static constexpr float kBoxW = 44.f;          // caixa numerica ao lado de cada slider
  static constexpr float kListW = 220.f;
  static constexpr float kSliderW = 130.f;
  static constexpr float kThumb = 14.f;
  static constexpr std::size_t kMaxFiles = 5000;

  Panel(const sui::TextAtlasSet& text, sui::SpriteUiSdlRenderer& sdl) : text_{text}, sdl_{sdl}, chrome_{text, sdl} {
    chrome_.setBodyFill(false);   // o corpo e desenhado aqui: a moldura nao pinta por cima
    root_ = tree_.Create<Elm::Element>(SU::InvalidUIHandle);
    handle_ = tree_.Create<Elm::TextField>(root_, Elm::TextFieldOptions{.multiline = true});
    field_ = tree_.GetAs<Elm::TextField>(handle_);
    Elm::Element* r = tree_.Get(root_);
    r->transform.anchorMin = {0.0f, 0.0f};
    r->transform.anchorMax = {1.0f, 1.0f};
    r->transform.offsetMin = {0.0f, 0.0f};
    r->transform.offsetMax = {0.0f, 0.0f};
    field_->transform.anchorMin = {0.0f, 0.0f};
    field_->transform.anchorMax = {1.0f, 1.0f};
    field_->transform.offsetMin = {0.0f, 0.0f};
    field_->transform.offsetMax = {0.0f, 0.0f};
    field_->padding = {4.0f, 6.0f, 4.0f, 6.0f};
    field_->fontSize = kMinCodeFont;
    field_->Edit() = Wd::TextEditState(std::string(), true);   // o codigo tem quebras de linha
    slider_.range = {kMinCodeFont, kMaxCodeFont, 1.0f, true};
    slider_.SetValue(kMinCodeFont);
    listSlider_.range = {kMinListFont, kMaxListFont, 1.0f, true};
    listSlider_.SetValue(kDefaultListFont);
    paintList_.owner = sui::handleFor(0);
  }
  Panel(const Panel&) = delete;
  Panel& operator=(const Panel&) = delete;

  [[nodiscard]] framed::Chrome& chrome() { return chrome_; }
  [[nodiscard]] const framed::Chrome& chrome() const { return chrome_; }

  [[nodiscard]] float fontSize() const { return codeFont_; }
  // Tamanho do codigo: o slider arrasta de 12 a 28, mas o valor digitado vale de 4 a 200 (so prendido por seguranca).
  void setFontSize(float size) {
    codeFont_ = editor::free_values::ClampFontPx(size, kMinCodeFont);
    field_->fontSize = codeFont_;
    slider_.SetValue(std::clamp(codeFont_, kMinCodeFont, kMaxCodeFont));
  }

  // Tamanho do texto da lista de arquivos (esquerda): mesma regra do codigo, faixa do slider de 10 a 32.
  [[nodiscard]] float listFontSize() const { return listFont_; }
  void setListFontSize(float size) {
    listFont_ = editor::free_values::ClampFontPx(size, kDefaultListFont);
    listSlider_.SetValue(std::clamp(listFont_, kMinListFont, kMaxListFont));
  }

  // Cursor de texto sobre a area de edicao (o cursor do sistema vira I-beam).
  [[nodiscard]] bool wantsTextCursor(float x, float y) const { return focused_ && inside(editorBox_, x, y); }

  // Eventos: moldura primeiro; depois barra, slider, lista, editor e teclado. Devolve true quando consumiu o evento.
  bool handleEvent(const SDL_Event& e) {
    if (chrome_.handleEvent(e)) return true;
    switch (e.type) {
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e.button.button != SDL_BUTTON_LEFT) return false;
        return onPointerDown(e.button.x, e.button.y);
      case SDL_EVENT_MOUSE_BUTTON_UP:
        if (sliding_ != 0) slide(sliding_, Elm::PointerPhase::Up, e.button.x, e.button.y);
        sliding_ = 0;
        dragging_ = false;
        field_->PointerUp();
        return bar_.pointerUp();   // soltou o polegar da barra da lista
      case SDL_EVENT_MOUSE_MOTION:
        px_ = e.motion.x;
        py_ = e.motion.y;
        if (bar_.pointerMove(px_, py_, listScroll_)) listScroll_ = ui_scrollbar::clampScroll(listScroll_, listContentH(), layout_.list.size.y);
        if (bar_.dragging()) return true;   // o arraste segue com o mouse fora da lista
        if (sliding_ != 0) {
          slide(sliding_, Elm::PointerPhase::Move, px_, py_);
          return true;
        }
        if (dragging_ && focused_) {
          placeCaretAt(px_, py_, true);
          return true;
        }
        return false;
      case SDL_EVENT_MOUSE_WHEEL:
        if (layout_.listShown && inside(layout_.list, e.wheel.mouse_x, e.wheel.mouse_y)) {
          listScroll_ = std::clamp(listScroll_ - e.wheel.y * rowH() * 2.f, 0.f, maxListScroll());
          return true;
        }
        if (inside(editorBox_, e.wheel.mouse_x, e.wheel.mouse_y)) {
          field_->Wheel(-e.wheel.y * codeFont_ * 3.f);
          return true;
        }
        return false;
      case SDL_EVENT_TEXT_INPUT:
        if (sizeEdit_ != 0) {   // digitando no campo de tamanho: so numeros e separador
          if (e.text.text) for (const char* c = e.text.text; *c; ++c) if ((*c >= '0' && *c <= '9') || *c == '.' || *c == ',') sizeText_.InsertText(std::string(1, *c));
          return true;
        }
        if (!focused_ || !e.text.text) return false;
        edit([&](Wd::TextEditState& s) { s.InsertText(e.text.text); });
        return true;
      case SDL_EVENT_KEY_DOWN:
        if (sizeEdit_ != 0) return sizeKey(e.key.key);
        if (!focused_) return false;
        return onKey(e.key.key, e.key.mod);
      default:
        return false;
    }
  }

  // Pasta do projeto: varre os codigos quando ela muda. Trocar de projeto salva o arquivo aberto antes.
  void setRoot(const fs::path& root) {
    if (scanned_ && root == projectRoot_) return;
    if (dirty_) save();
    projectRoot_ = root;
    rescan();
  }

  // Desenha o corpo dentro de `body` (area da moldura, abaixo do cabecalho). Devolve false se o render falhar.
  bool drawBody(SDL_Renderer* renderer, const sui::Box& body, const fs::path& root) {
    if (body.h <= 0.f || body.w <= 0.f) return true;   // minimizada ou escondida
    setRoot(root);
    refreshScriptIssues(SDL_GetTicks() / 1000.0);
    window_ = SDL_GetRenderWindow(renderer);
    refreshInk();
    body_ = body;
    layoutAll(body);
    const SU::Rect bodyRect = rect(body.x, body.y, body.w, body.h);

    view::ShapeList bar;   // fundo, barra e slider (recorte: corpo)
    buildBar(bar, body);
    paintBody_.append(bar, kClipFrame, paintList_);

    view::ShapeList list;  // lista de codigos (recorte: lista)
    buildList(list);
    paintBody_.append(list, kClipList, paintList_);

    view::ShapeList pane;  // fundo do editor, numeros de linha (recorte: editor)
    buildPane(pane);
    paintBody_.append(pane, kClipEditor, paintList_);
    if (current_ >= 0) buildFieldItems();

    clips_.clear();
    clips_.push_back({bodyRect.position.x, bodyRect.position.y, bodyRect.position.x + bodyRect.size.x,
                      bodyRect.position.y + bodyRect.size.y});
    clips_.push_back({layout_.list.position.x, layout_.list.position.y, layout_.list.position.x + layout_.list.size.x,
                      layout_.list.position.y + layout_.list.size.y});
    clips_.push_back({layout_.pane.position.x, layout_.pane.position.y, layout_.pane.position.x + layout_.pane.size.x,
                      layout_.pane.position.y + layout_.pane.size.y});
    const bool ok = view::drawPaintList(renderer, sdl_, extractor_, packet_, frameId_, clips_, paintList_);
    paintList_ = view::PaintList{};
    paintList_.owner = sui::handleFor(0);
    return ok;
  }

 private:
  static constexpr std::uint32_t kClipFrame = 0u;
  static constexpr std::uint32_t kClipList = 1u;
  static constexpr std::uint32_t kClipEditor = 2u;

  // Arquivo de codigo listado: caminho relativo ao projeto e caminho absoluto.
  struct Entry {
    std::string rel;
    fs::path abs;
  };

  // Retangulos de cada parte da janela, recalculados a cada quadro em layoutAll.
  struct Layout {
    SU::Rect save, toggle, slider, sizeBox, list, listHead, listSlider, listBox, pane, gutter, editor, diagnostics;
    float sizeLabelX = 0.f;
    bool listShown = false;
  };

  // ---------- Cores ----------
  void refreshInk() {
    const palette::Palette& p = palette::active();
    auto c = [](const palette::Rgb& x) { return view::rgbaF(x.r, x.g, x.b, x.a); };
    auto mix = [](const palette::Rgb& a, const palette::Rgb& b, float t) {
      return palette::Rgb{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1.f};
    };
    const palette::Rgb list = mix(p.panel, p.surface, 0.5f);
    const palette::Rgb focus = mix(p.panel, p.surface, 0.7f);
    ink_ = Ink{c(p.text),
               c(p.textDim),
               c(p.surface),
               c(list),
               c(mix(p.panel, p.surface, 0.3f)),
               c(mix(p.panel, p.accent, 0.45f)),
               c(mix(p.surface, p.accent, 0.6f)),
               c(p.accent),
               c(p.border),
               c(p.error),
               view::rgbaF(0.95f, 0.68f, 0.12f, 1.f),
               view::rgbaF(0.25f, 0.78f, 0.38f, 1.f)};
    style_.background = sui::PaletteColor(p.surface);
    style_.focusBackground = sui::PaletteColor(focus);
    style_.text = sui::PaletteColor(p.text);
    style_.caret = sui::PaletteColor(p.text);
    palette::Rgb sel = p.accent;
    sel.a = 0.45f;
    style_.selection = sui::PaletteColor(sel);
  }

  // ---------- Geometria ----------
  static bool inside(const SU::Rect& r, float x, float y) {
    return x >= r.position.x && x < r.position.x + r.size.x && y >= r.position.y && y < r.position.y + r.size.y;
  }
  static SU::Rect rect(float x, float y, float w, float h) { return SU::Rect{{x, y}, {w, h}}; }

  // Mede um texto simples (sem estilo) no tamanho dado.
  ST::TextLayoutResult layoutPlain(const std::string& utf8, float size) const {
    const ST::TruetypeAtlas& atlas = text_.atlasFor(size);
    ST::TextLayoutConfig cfg;
    cfg.fontSize = size;
    return ST::LayoutText(utf8, ST::ShapeTruetype(utf8, atlas), atlas.metrics, cfg);
  }
  float textWidth(const std::string& s, float size = editor::ui_style::uiFont(kFont)) const { return layoutPlain(s, size).size.x; }

  // Layout do texto do campo com a largura atual (depois de cada edicao, para que cliques e setas usem o texto novo).
  ST::TextLayoutResult fieldLayout() const {
    const std::string& t = field_->Edit().Text();
    const ST::TruetypeAtlas& atlas = text_.atlasFor(codeFont_);
    return ST::LayoutText(t, ST::ShapeTruetype(t, atlas), atlas.metrics,
                          Elm::MakeTextFieldLayoutConfig(*field_, contentSize(), style_));
  }
  SU::Vec2 contentSize() const { return field_->ContentRect().size; }

  // Numero de linhas do texto (conta os 
  // ; texto vazio tem 1 linha).
  static std::size_t lineCount(const std::string& t) {
    return 1 + static_cast<std::size_t>(std::count(t.begin(), t.end(), '\n'));
  }

  // Altura de uma linha da lista: acompanha o tamanho do texto (nunca menor que kRow).
  float rowH() const { return std::max(kRow, editor::ui_style::uiFont(listFont_) * 1.5f); }

  float listContentH() const { return static_cast<float>(files_.size()) * rowH(); }

  // Rolagem maxima da lista de arquivos (0 se cabe).
  float maxListScroll() const {
    return std::max(0.f, static_cast<float>(files_.size()) * rowH() - layout_.list.size.y);
  }

  std::string toggleLabel() const { return std::string(listShown_ ? T("Hide list") : T("Show list")); }
  std::string listLabel() const { return std::string(T("List size")); }
  std::string sizeLabel() const { return std::string(T("Code size")) + ": " + std::to_string(static_cast<int>(codeFont_)); }

  // Divide a area da janela entre barra (Save, nome, slider de tamanho, mostrar lista), lista, editor e diagnosticos.
  void layoutAll(const sui::Box& b) {
    layout_ = Layout{};
    layout_.save = rect(b.x + kPad, b.y + 4.f, 56.f, kBar - 8.f);
    const float tw = textWidth(toggleLabel()) + 16.f;
    layout_.toggle = rect(b.x + b.w - kPad - tw, b.y + 4.f, tw, kBar - 8.f);
    layout_.sizeBox = rect(layout_.toggle.position.x - 12.f - kBoxW, b.y + 5.f, kBoxW, kBar - 10.f);   // campo numerico do tamanho do codigo
    const float sliderRight = layout_.sizeBox.position.x - 6.f;
    layout_.slider = rect(sliderRight - kSliderW, b.y + 8.f, kSliderW, kBar - 16.f);
    layout_.sizeLabelX = layout_.slider.position.x - 8.f - textWidth(sizeLabel());
    slider_.border = layout_.slider;

    const float top = b.y + kBar + 1.f;
    const float areaH = std::max(0.f, b.y + b.h - top);
    layout_.listShown = listShown_;
    float paneX = b.x;
    if (listShown_) {
      const float lw = std::min(kListW, b.w * 0.5f);
      const float headH = std::min(kListHeadH, areaH);   // faixa do slider do tamanho do texto da lista
      layout_.listHead = rect(b.x, top, lw, headH);
      layout_.list = rect(b.x, top + headH, lw, std::max(0.f, areaH - headH));
      layout_.listBox = rect(b.x + lw - kPad - kBoxW, top + 3.f, kBoxW, std::max(0.f, headH - 6.f));
      const float sx = b.x + kPad + textWidth(listLabel()) + 8.f;
      layout_.listSlider = rect(sx, top + 6.f, std::max(20.f, layout_.listBox.position.x - 6.f - sx), std::max(0.f, headH - 12.f));
      listSlider_.border = layout_.listSlider;
      paneX = b.x + lw + 1.f;
    }
    const float paneW = std::max(0.f, b.x + b.w - paneX);
    layout_.pane = rect(paneX, top, paneW, areaH);
    const float diagnosticH = isToolScript() ? std::min(66.f, areaH * 0.32f) : 0.f;
    const float editorH = std::max(0.f, areaH - diagnosticH);
    if (diagnosticH > 0.f) layout_.diagnostics = rect(paneX, top + editorH, paneW, diagnosticH);

    const std::size_t lines = lineCount(field_->Edit().Text());
    const float digits = static_cast<float>(std::max<std::size_t>(2, std::to_string(lines).size()));
    const float gutterW = std::min(paneW * 0.5f, std::max(40.f, digits * codeFont_ * 0.62f + 14.f));
    layout_.gutter = rect(paneX, top, gutterW, editorH);
    layout_.editor = rect(paneX + gutterW, top + 2.f, std::max(0.f, paneW - gutterW), std::max(0.f, editorH - 4.f));
    editorBox_ = layout_.editor;
    listScroll_ = std::clamp(listScroll_, 0.f, maxListScroll());
    if (layout_.listShown) {
      const SU::Rect l = layout_.list;
      bar_.layout({l.position.x + l.size.x - ui_scrollbar::kWidth, l.position.y, ui_scrollbar::kWidth, l.size.y}, listContentH(), l.size.y);
    } else {
      bar_.layout({}, 0.f, 0.f);
    }
    tree_.Arrange(root_, {{layout_.editor.position.x, layout_.editor.position.y},
                          {layout_.editor.size.x, layout_.editor.size.y}});
  }

  // ---------- Pintura ----------
  void button(view::ShapeList& s, const SU::Rect& r, const std::string& label) {
    s.rectFilled({r.position.x, r.position.y}, {r.position.x + r.size.x, r.position.y + r.size.y}, ink_.button, editor::ui_style::buttonRadius());
    s.text({r.position.x + r.size.x * 0.5f, r.position.y + (r.size.y - editor::ui_style::uiFont(kFont)) * 0.5f - 1.f}, label, editor::ui_style::uiFont(kFont), ink_.text, true);
  }

  // Desenha a barra do topo e o separador entre lista e editor.
  void buildBar(view::ShapeList& s, const sui::Box& b) {
    s.rectFilled({b.x, b.y}, {b.x + b.w, b.y + kBar}, ink_.list, 0.f);
    s.rectFilled({b.x, b.y + kBar - 1.f}, {b.x + b.w, b.y + kBar}, ink_.border, 0.f);
    if (layout_.listShown) {   // separador entre a lista e o editor
      s.rectFilled({layout_.list.position.x + layout_.list.size.x, b.y + kBar},
                   {layout_.pane.position.x, b.y + b.h}, ink_.border, 0.f);
    }
    button(s, layout_.save, T("Save"));
    button(s, layout_.toggle, toggleLabel());

    const float nameX = layout_.save.position.x + layout_.save.size.x + kPad;
    const std::string name = current_ >= 0 ? files_[static_cast<std::size_t>(current_)].rel + (dirty_ ? " *" : "")
                                           : std::string(T("No file open"));
    const float nameW = std::max(0.f, layout_.sizeLabelX - nameX - kPad);
    s.text({nameX, b.y + (kBar - editor::ui_style::uiFont(kFont)) * 0.5f - 1.f}, name, editor::ui_style::uiFont(kFont), ink_.dim, false, nameW);
    s.text({layout_.sizeLabelX, b.y + (kBar - editor::ui_style::uiFont(kFont)) * 0.5f - 1.f}, sizeLabel(), editor::ui_style::uiFont(kFont), ink_.dim);

    drawSlider(s, layout_.slider, slider_);
    drawSizeBox(s, layout_.sizeBox, 1, codeFont_);

    if (layout_.listShown && layout_.listHead.size.y > 0.f) {   // faixa do topo da lista: rotulo, slider e campo do tamanho
      const SU::Rect h = layout_.listHead;
      s.rectFilled({h.position.x, h.position.y}, {h.position.x + h.size.x, h.position.y + h.size.y}, ink_.list, 0.f);
      s.rectFilled({h.position.x, h.position.y + h.size.y - 1.f}, {h.position.x + h.size.x, h.position.y + h.size.y}, ink_.border, 0.f);
      s.text({h.position.x + kPad, h.position.y + (h.size.y - editor::ui_style::uiFont(kFont)) * 0.5f - 1.f}, listLabel(),
             editor::ui_style::uiFont(kFont), ink_.dim);
      drawSlider(s, layout_.listSlider, listSlider_);
      drawSizeBox(s, layout_.listBox, 2, listFont_);
    }
  }

  // Slider: trilha, parte preenchida e polegar (o valor vem do proprio slider).
  void drawSlider(view::ShapeList& s, const SU::Rect& sl, const Elm::Slider& slider) {
    const float trackY = sl.position.y + sl.size.y * 0.5f;
    const float x0 = sl.position.x + kThumb * 0.5f;
    const float x1 = sl.position.x + sl.size.x - kThumb * 0.5f;
    const float tx = x0 + (x1 - x0) * slider.Ratio();
    s.rectFilled({x0, trackY - 2.f}, {x1, trackY + 2.f}, ink_.button, 2.f);
    s.rectFilled({x0, trackY - 2.f}, {tx, trackY + 2.f}, ink_.buttonOn, 2.f);
    s.rectFilled({tx - kThumb * 0.5f, trackY - kThumb * 0.5f}, {tx + kThumb * 0.5f, trackY + kThumb * 0.5f}, ink_.text,
                 kThumb * 0.5f);
  }

  // Campo numerico ao lado de um slider (which: 1 = codigo, 2 = lista): mostra o valor ou o texto em edicao com caret.
  void drawSizeBox(view::ShapeList& s, const SU::Rect& r, int which, float value) {
    if (r.size.x <= 0.f || r.size.y <= 0.f) return;
    const bool editing = sizeEdit_ == which;
    const float fs = editor::ui_style::uiFont(kFont);
    s.rectFilled({r.position.x, r.position.y}, {r.position.x + r.size.x, r.position.y + r.size.y}, editing ? ink_.field : ink_.button,
                 editor::ui_style::buttonRadius());
    const std::string shown = editing ? sizeText_.Text() : std::to_string(static_cast<int>(value + 0.5f));
    s.text({r.position.x + 6.f, r.position.y + (r.size.y - fs) * 0.5f - 1.f}, shown, fs, ink_.text, false, r.size.x - 8.f);
    if (editing) {
      const float cx = r.position.x + 6.f + textWidth(shown.substr(0, sizeText_.Caret()), fs);
      s.rectFilled({cx, r.position.y + 3.f}, {cx + 1.5f, r.position.y + r.size.y - 3.f}, ink_.text, 0.f);
    }
  }

  // Retangulo da linha i da lista, ja com a rolagem aplicada.
  SU::Rect rowRect(std::size_t i) const {
    const SU::Rect& l = layout_.list;
    return rect(l.position.x, l.position.y + static_cast<float>(i) * rowH() - listScroll_, l.size.x, rowH());
  }

  // Desenha a lista de arquivos (ou a mensagem de vazio), destacando o aberto.
  void buildList(view::ShapeList& s) {
    if (!layout_.listShown) return;
    const SU::Rect l = layout_.list;
    s.rectFilled({l.position.x, l.position.y}, {l.position.x + l.size.x, l.position.y + l.size.y}, ink_.list, 0.f);
    if (files_.empty()) {
      s.text({l.position.x + kPad, l.position.y + 6.f}, T("No code files found in the project."), editor::ui_style::uiFont(kFont), ink_.dim, false,
             l.size.x - 2.f * kPad);
      return;
    }
    const float maxW = l.size.x - 2.f * kPad;
    for (std::size_t i = 0; i < files_.size(); ++i) {
      const SU::Rect r = rowRect(i);
      if (r.position.y + r.size.y < l.position.y || r.position.y > l.position.y + l.size.y) continue;
      const bool selected = static_cast<int>(i) == current_;
      const bool hover = inside(r, px_, py_);
      if (selected) {
        s.rectFilled({r.position.x, r.position.y}, {r.position.x + r.size.x, r.position.y + r.size.y}, ink_.rowSel, 0.f);
      } else if (hover) {
        s.rectFilled({r.position.x, r.position.y}, {r.position.x + r.size.x, r.position.y + r.size.y}, ink_.rowHover, 0.f);
      }
      const float lf = editor::ui_style::uiFont(listFont_);
      s.text({r.position.x + kPad, r.position.y + (r.size.y - lf) * 0.5f - 1.f}, files_[i].rel, lf,
             selected ? ink_.text : ink_.dim, false, maxW);
    }
    // Barra de rolagem unica do editor (ui_scrollbar.hpp).
    for (const ui_scrollbar::Quad& q : bar_.quads(palette::active(), listScroll_)) {
      if (q.rect.w <= 0.f || q.rect.h <= 0.f) continue;
      s.rectFilled({q.rect.x, q.rect.y}, {q.rect.x + q.rect.w, q.rect.y + q.rect.h}, view::rgbaF(q.color.r, q.color.g, q.color.b, q.color.a), 0.f);
    }
  }

  // Fundo do editor, mensagem sem arquivo, e os numeros de linha (um por linha logica, mesmo quando a linha quebra).
  void buildPane(view::ShapeList& s) {
    const SU::Rect p = layout_.pane;
    if (p.size.x <= 0.f || p.size.y <= 0.f) return;
    s.rectFilled({p.position.x, p.position.y}, {p.position.x + p.size.x, p.position.y + p.size.y}, ink_.field, 0.f);
    if (current_ < 0) {
      s.text({p.position.x + kPad, p.position.y + kPad}, T("Choose a code file in the list to edit it."), editor::ui_style::uiFont(kFont), ink_.dim,
             false, p.size.x - 2.f * kPad);
      return;
    }
    const SU::Rect g = layout_.gutter;
    s.rectFilled({g.position.x, g.position.y}, {g.position.x + g.size.x, g.position.y + g.size.y}, ink_.list, 0.f);

    const ST::TextLayoutResult layout = fieldLayout();
    const std::string& t = field_->Edit().Text();
    const SU::Rect content = field_->ContentRect();
    const float scroll = field_->ScrollY();
    std::size_t number = 0;
    for (const ST::TextLine& line : layout.lines) {
      const std::size_t at = line.byteBegin;
      const bool start = at == 0 || (at <= t.size() && t[at - 1] == '\n');
      if (!start) continue;
      ++number;
      const float y = content.position.y + line.y - scroll;
      if (y + codeFont_ < p.position.y || y > p.position.y + p.size.y) continue;
      const std::string n = std::to_string(number);
      const float w = textWidth(n, codeFont_);
      s.text({g.position.x + g.size.x - 6.f - w, y}, n, codeFont_, issueColor(static_cast<int>(number)));
    }
    drawDiagnostics(s);
  }

  // Cor do numero da linha: vermelho se tem erro, amarelo se aviso, apagado se nada.
  view::Color issueColor(int line) const {
    for (const ScriptIssue& issue : issues_) if (issue.line == line)
      return issue.sev == tool::Severity::Error ? ink_.error : ink_.warning;
    return ink_.dim;
  }

  // Painel de diagnosticos do script (erros e avisos com a linha); so para arquivos .tool.js.
  void drawDiagnostics(view::ShapeList& s) {
    if (!isToolScript() || layout_.diagnostics.size.y <= 0.f) return;
    const SU::Rect r = layout_.diagnostics;
    s.rectFilled({r.position.x, r.position.y}, {r.position.x + r.size.x, r.position.y + r.size.y}, ink_.list, 0.f);
    if (issues_.empty()) {
      s.text({r.position.x + kPad, r.position.y + 4.f}, T("ToolObject script check: OK"), editor::ui_style::uiFont(kFont), ink_.ok, false,
             r.size.x - 2.f * kPad);
      return;
    }
    const std::size_t visible = std::min<std::size_t>(3, issues_.size());
    for (std::size_t i = 0; i < visible; ++i) {
      const ScriptIssue& issue = issues_[i];
      const std::string message = "L" + std::to_string(issue.line) + " [" + issue.code + "] " + issue.msg;
      const view::Color color = issue.sev == tool::Severity::Error ? ink_.error : ink_.warning;
      s.text({r.position.x + kPad, r.position.y + 3.f + static_cast<float>(i) * 18.f}, message,
             editor::ui_style::uiFont(kFont), color, false, r.size.x - 2.f * kPad);
    }
    if (issues_.size() > visible) {
      s.text({r.position.x + kPad, r.position.y + 3.f + static_cast<float>(visible) * 18.f},
             "+" + std::to_string(issues_.size() - visible) + " more", editor::ui_style::uiFont(kFont), ink_.dim);
    }
  }

  // Itens do campo (fundo ja pintado; aqui texto, selecao e caret), recortados pelo editor.
  void buildFieldItems() {
    const ST::TextLayoutResult layout = fieldLayout();
    const ST::TextLayoutResult placeholder = layoutPlain("", codeFont_);
    Elm::TextTarget target;
    target.textureSlice = text_.sliceFor(codeFont_);
    target.paintOrder = paintList_.paint;
    const SU::Rect content = field_->ContentRect();
    const auto items = Elm::BuildTextFieldItems(*field_, style_, layout, placeholder, content, target, kClipEditor);
    paintList_.items.insert(paintList_.items.end(), items.begin(), items.end());
    paintList_.paint += items.size() + 1;
  }

  // ---------- Ponteiro ----------
  bool onPointerDown(float x, float y) {
    const SU::Rect body = rect(body_.x, body_.y, body_.w, body_.h);
    if (!inside(body, x, y)) {
      commitSizeEdit();
      focus(false);
      return false;
    }
    if (inside(layout_.save, x, y)) { save(); return true; }
    if (inside(layout_.toggle, x, y)) { listShown_ = !listShown_; return true; }
    if (sizeEdit_ != 0 && !inside(sizeEdit_ == 1 ? layout_.sizeBox : layout_.listBox, x, y)) commitSizeEdit();   // clicar fora confirma
    if (inside(layout_.sizeBox, x, y)) { beginSizeEdit(1); return true; }
    if (layout_.listShown && inside(layout_.listBox, x, y)) { beginSizeEdit(2); return true; }
    if (inside(layout_.slider, x, y)) {
      sliding_ = 1;
      slide(1, Elm::PointerPhase::Down, x, y);
      return true;
    }
    if (layout_.listShown && inside(layout_.listSlider, x, y)) {
      sliding_ = 2;
      slide(2, Elm::PointerPhase::Down, x, y);
      return true;
    }
    if (layout_.listShown && bar_.pointerDown(x, y, listScroll_)) {   // barra de rolagem unica da lista
      listScroll_ = ui_scrollbar::clampScroll(listScroll_, listContentH(), layout_.list.size.y);
      return true;
    }
    if (layout_.listShown && inside(layout_.list, x, y)) {
      const float rel = (y - layout_.list.position.y + listScroll_) / rowH();
      if (rel >= 0.f && static_cast<std::size_t>(rel) < files_.size()) {
        open(static_cast<std::size_t>(rel));
        focus(true);
      }
      return true;
    }
    if (current_ >= 0 && inside(editorBox_, x, y)) {
      focus(true);
      placeCaretAt(x, y, false);
      dragging_ = true;
      return true;
    }
    return true;
  }

  // Arraste de um slider (which: 1 = tamanho do codigo, 2 = tamanho do texto da lista).
  void slide(int which, Elm::PointerPhase phase, float x, float y) {
    Elm::Slider& sl = which == 1 ? slider_ : listSlider_;
    Elm::PointerEvent ev;
    ev.phase = phase;
    ev.position = {x, y};
    ev.inside = inside(which == 1 ? layout_.slider : layout_.listSlider, x, y);
    const Elm::SliderEvents s = sl.OnPointer(ev);
    if (s.valueChanged) {
      if (which == 1) setFontSize(sl.Value());
      else setListFontSize(sl.Value());
    }
  }

  // Abre a digitacao do campo numerico (which: 1 = codigo, 2 = lista), com o valor atual como texto de partida.
  void beginSizeEdit(int which) {
    if (sizeEdit_ == which) return;
    if (sizeEdit_ != 0) commitSizeEdit();
    focus(false);   // o editor de codigo larga o foco; o texto digitado vai para o campo
    sizeEdit_ = which;
    sizeText_ = Wd::TextEditState(std::to_string(static_cast<int>((which == 1 ? codeFont_ : listFont_) + 0.5f)));
    sizeText_.MoveEnd();
    if (window_) SDL_StartTextInput(window_);
  }

  // Confirma o numero digitado: texto invalido nao muda nada; valido vale de 4 a 200.
  void commitSizeEdit() {
    if (sizeEdit_ == 0) return;
    float typed = 0.f;
    if (slider_edit::ParseTypedNumber(sizeText_.Text(), typed)) {
      if (sizeEdit_ == 1) setFontSize(typed);
      else setListFontSize(typed);
    }
    endSizeEdit();
  }

  // Termina a digitacao do tamanho do codigo e desliga a entrada de texto do SDL.
  void endSizeEdit() {
    sizeEdit_ = 0;
    if (window_) SDL_StopTextInput(window_);
  }

  // Tecla durante a digitacao do tamanho: Enter confirma, Esc cancela, o resto edita o texto. Sempre consome.
  bool sizeKey(SDL_Keycode key) {
    switch (key) {
      case SDLK_RETURN: case SDLK_KP_ENTER: commitSizeEdit(); break;
      case SDLK_ESCAPE: endSizeEdit(); break;
      case SDLK_BACKSPACE: (void)sizeText_.Backspace(); break;
      case SDLK_DELETE: (void)sizeText_.Delete(); break;
      case SDLK_LEFT: sizeText_.MoveLeft(); break;
      case SDLK_RIGHT: sizeText_.MoveRight(); break;
      case SDLK_HOME: sizeText_.MoveHome(); break;
      case SDLK_END: sizeText_.MoveEnd(); break;
      default: break;
    }
    return true;
  }

  // Coloca o cursor do editor no ponto clicado (extend mantem a selecao).
  void placeCaretAt(float x, float y, bool extend) {
    const SU::Rect c = field_->ContentRect();
    const ST::TextLayoutResult layout = fieldLayout();
    field_->PointerDown({x - c.position.x, y - c.position.y}, c.size, layout, style_, extend);
  }

  // Foco do editor; liga/desliga a entrada de texto do SDL.
  void focus(bool on) {
    if (on == focused_) return;
    focused_ = on;
    field_->SetFocused(on);
    if (window_) {
      if (on) SDL_StartTextInput(window_);
      else SDL_StopTextInput(window_);
    }
  }

  // ---------- Teclado ----------
  bool onKey(SDL_Keycode key, SDL_Keymod mod) {
    const bool ctrl = (mod & SDL_KMOD_CTRL) != 0;
    const bool shift = (mod & SDL_KMOD_SHIFT) != 0;
    if (ctrl) {
      switch (key) {
        case SDLK_S: save(); return true;
        case SDLK_A: edit([](Wd::TextEditState& s) { s.SelectAll(); }); return true;
        case SDLK_C: SDL_SetClipboardText(field_->Edit().Copy().c_str()); return true;
        case SDLK_X: { std::string cut = field_->Edit().Cut(); SDL_SetClipboardText(cut.c_str()); edited(); return true; }
        case SDLK_V: if (char* clip = SDL_GetClipboardText()) { edit([&](Wd::TextEditState& s) { s.Paste(clip); }); SDL_free(clip); } return true;
        case SDLK_Z: edit([](Wd::TextEditState& s) { s.Undo(); }); return true;
        default: return false;
      }
    }
    switch (key) {
      case SDLK_RETURN: case SDLK_KP_ENTER: edit([](Wd::TextEditState& s) { s.InsertText("\n"); }); return true;
      case SDLK_TAB: edit([](Wd::TextEditState& s) { s.InsertText("    "); }); return true;
      case SDLK_BACKSPACE: edit([](Wd::TextEditState& s) { s.Backspace(); }); return true;
      case SDLK_DELETE: edit([](Wd::TextEditState& s) { s.Delete(); }); return true;
      case SDLK_LEFT: edit([&](Wd::TextEditState& s) { s.MoveLeft(shift); }, false); return true;
      case SDLK_RIGHT: edit([&](Wd::TextEditState& s) { s.MoveRight(shift); }, false); return true;
      case SDLK_HOME: edit([&](Wd::TextEditState& s) { s.MoveHome(shift); }, false); return true;
      case SDLK_END: edit([&](Wd::TextEditState& s) { s.MoveEnd(shift); }, false); return true;
      case SDLK_UP: moveVertical(-1, shift); return true;
      case SDLK_DOWN: moveVertical(1, shift); return true;
      case SDLK_PAGEUP: for (int i = 0; i < 10; ++i) moveVertical(-1, shift); return true;
      case SDLK_PAGEDOWN: for (int i = 0; i < 10; ++i) moveVertical(1, shift); return true;
      default: return false;
    }
  }

  // Seta para cima/baixo: move o cursor uma linha e mantem ele visivel.
  void moveVertical(int dir, bool shift) {
    const ST::TextLayoutResult layout = fieldLayout();
    if (dir < 0) field_->MoveUp(layout, shift); else field_->MoveDown(layout, shift);
    field_->KeepCaretVisible(fieldLayout(), contentSize(), style_);
  }

  // Aplica uma edicao do texto (sem mudar o caret por ela). Marca como alterado.
  // Aplica uma alteracao ao texto do editor; se o texto mudou marca como editado, senao so mantem o cursor visivel.
  template <class F>
  void edit(F&& fn, bool changes = true) {
    const std::string before = field_->Edit().Text();
    fn(field_->Edit());
    if (changes && before != field_->Edit().Text()) edited();
    else field_->KeepCaretVisible(fieldLayout(), contentSize(), style_);
  }

  // Marca o arquivo como alterado e, se for script, agenda a nova checagem de diagnosticos.
  void edited() {
    dirty_ = true;
    if (isToolScript()) {
      issuesPending_ = true;
      issuesChangedAt_ = SDL_GetTicks() / 1000.0;
    }
    field_->KeepCaretVisible(fieldLayout(), contentSize(), style_);
  }

  // O arquivo aberto e um script de jogo (.tool.js)? So esses tem diagnosticos.
  bool isToolScript() const {
    if (current_ < 0 || static_cast<std::size_t>(current_) >= files_.size()) return false;
    const std::string& path = files_[static_cast<std::size_t>(current_)].rel;
    constexpr char suffix[] = ".tool.js";
    constexpr std::size_t suffixLength = sizeof(suffix) - 1;
    return path.size() >= suffixLength && path.compare(path.size() - suffixLength, suffixLength, suffix) == 0;
  }

  // Roda a checagem do script depois de 0,4 s sem digitar (nao a cada tecla).
  void refreshScriptIssues(double now) {
    if (!issuesPending_ || current_ < 0 || static_cast<std::size_t>(current_) >= files_.size() || now - issuesChangedAt_ < 0.4) return;
    issues_ = checkScriptSource(field_->Edit().Text(), files_[static_cast<std::size_t>(current_)].rel);
    issuesPending_ = false;
  }

  // Troca o texto do editor e poe o cursor na posicao dada.
  void setText(const std::string& text, std::size_t cursor) {
    field_->Edit() = Wd::TextEditState(text, true);
    field_->Edit().MoveCaretTo(cursor);
  }

  // ---------- Arquivos ----------
  void rescan() {
    scanned_ = true;
    files_.clear();
    current_ = -1;
    setText(std::string(), 0);
    dirty_ = false;
    std::error_code ec;
    if (projectRoot_.empty() || !fs::is_directory(projectRoot_, ec)) return;
    fs::recursive_directory_iterator it(projectRoot_, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
      const fs::path p = it->path();
      std::error_code e2;
      if (it->is_directory(e2)) {
        if (skipDirectory(p.filename().string())) it.disable_recursion_pending();
        continue;
      }
      if (!it->is_regular_file(e2) || !isCodeFile(p)) continue;
      files_.push_back({fs::relative(p, projectRoot_, e2).generic_string(), p});
      if (files_.size() >= kMaxFiles) break;
    }
    std::sort(files_.begin(), files_.end(), [](const Entry& a, const Entry& b) { return a.rel < b.rel; });
  }

  // Pastas ignoradas ao procurar codigo (ocultas, build, third_party, node_modules...).
  static bool skipDirectory(const std::string& name) {
    static const char* const kSkip[] = {".git", ".vs", ".vscode", "out", "build", "Build", "third_party", "node_modules"};
    if (!name.empty() && name[0] == '.') return true;
    return std::any_of(std::begin(kSkip), std::end(kSkip), [&](const char* s) { return name == s; });
  }

  // Extensao (ou CMakeLists.txt) que conta como codigo-fonte para a lista.
  static bool isCodeFile(const fs::path& p) {
    if (p.filename() == "CMakeLists.txt") return true;
    static const char* const kExt[] = {".cpp", ".cc", ".cxx", ".c", ".hpp", ".hh", ".hxx", ".h", ".inl", ".cmake",
                                       ".py", ".lua", ".glsl", ".hlsl", ".vert", ".frag", ".comp", ".sh", ".bat",
                                       ".ps1", ".js", ".ts", ".cs"};
    const std::string ext = p.extension().string();
    return std::any_of(std::begin(kExt), std::end(kExt), [&](const char* e) { return ext == e; });
  }

  // Abre um codigo: salva o atual (se mudou) e carrega o escolhido.
  void open(std::size_t index) {
    if (static_cast<int>(index) == current_) return;
    if (dirty_) save();
    std::ifstream f(files_[index].abs, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string raw = ss.str();
    crlf_ = raw.find("\r\n") != std::string::npos;
    std::string text;
    text.reserve(raw.size());
    for (char c : raw) {
      if (c != '\r') text.push_back(c);   // o campo trabalha com \n; save() devolve \r\n se o arquivo era assim
    }
    current_ = static_cast<int>(index);
    dirty_ = false;
    setText(text, 0);
    issues_.clear();
    issuesPending_ = false;
    if (isToolScript()) issues_ = checkScriptSource(text, files_[index].rel);
  }

  // Grava o arquivo aberto (restaura CRLF se era o original) e limpa o marcador de alterado.
  void save() {
    if (current_ < 0) return;
    std::string out = field_->Edit().Text();
    if (crlf_) {
      std::string withCr;
      withCr.reserve(out.size() + out.size() / 16);
      for (char c : out) {
        if (c == '\n') withCr += "\r\n";
        else withCr.push_back(c);
      }
      out = std::move(withCr);
    }
    std::ofstream f(files_[static_cast<std::size_t>(current_)].abs, std::ios::binary | std::ios::trunc);
    if (!f) return;   // falha ao salvar: o texto continua na tela e o arquivo fica marcado
    f << out;
    dirty_ = false;
    if (isToolScript()) {
      issues_ = checkScriptSource(field_->Edit().Text(), files_[static_cast<std::size_t>(current_)].rel);
      issuesPending_ = false;
    }
  }

  // ---------- Membros ----------
  const sui::TextAtlasSet& text_;
  sui::SpriteUiSdlRenderer& sdl_;
  framed::Chrome chrome_;
  Elm::ElementTree tree_;
  SU::UIHandle root_{}, handle_{};
  Elm::TextField* field_ = nullptr;
  Elm::TextFieldStyle style_;
  Elm::Slider slider_, listSlider_;
  Layout layout_;
  int sizeEdit_ = 0;               // campo numerico em edicao: 0 = nenhum, 1 = codigo, 2 = lista
  Wd::TextEditState sizeText_;     // texto do campo numerico em edicao
  SU::Rect editorBox_{};
  sui::Box body_{};
  view::ShapePainter paintBody_{text_};
  view::PaintList paintList_;
  std::vector<SR::ClipRectGPU> clips_;
  SR::RenderExtractor extractor_;
  SR::FramePacket packet_;
  std::uint64_t frameId_ = 0;
  SDL_Window* window_ = nullptr;
  Ink ink_{};

  fs::path projectRoot_;
  bool scanned_ = false;
  std::vector<Entry> files_;
  int current_ = -1;
  bool crlf_ = false;
  bool dirty_ = false;
  bool issuesPending_ = false;
  double issuesChangedAt_ = -10.0;
  std::vector<ScriptIssue> issues_;
  bool focused_ = false, dragging_ = false, listShown_ = true;
  int sliding_ = 0;   // slider em arraste: 0 = nenhum, 1 = codigo, 2 = lista
  ui_scrollbar::Scrollbar bar_;
  float listScroll_ = 0.f;
  float codeFont_ = kMinCodeFont;
  float listFont_ = kDefaultListFont;
  float px_ = 0.f, py_ = 0.f;
};

}  // namespace editor::coding
