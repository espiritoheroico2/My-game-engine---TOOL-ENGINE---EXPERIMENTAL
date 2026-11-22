// script_window.hpp - Janela de edicao de script, em SpriteUI (sem ImGui).
//
// "Edit script" no Inspector abre esta janela flutuante. O corpo tem:
//  - barra: Save (Ctrl+S), Complete (Ctrl+Espaco), Snippets (lista de exemplos do Help) e Close;
//  - editor multilinha: Elements::TextField da biblioteca SpriteUI (quebra, rolagem, selecao, desfazer);
//  - autocomplete da API (aparece ao digitar 2+ letras ou com Ctrl+Espaco; clique aceita);
//  - verificador de scripts: OK ou a lista de erros e avisos, com a linha de cada um.
//
// A moldura vem de framed::Chrome; o corpo e desenhado com view::ShapeList e os itens do campo. Eventos chegam por
// handleEvent (antes do ImGui). Nada aqui inclui imgui.h.
#pragma once

#include "editor_state.hpp"
#include "i18n.hpp"
#include "script.hpp"
#include "script_check.hpp"
#include "script_complete.hpp"
#include "spriteui_host.hpp"
#include "text_atlas_set.hpp"
#include "framed_window.hpp"
#include "view_painter.hpp"
#include "view_shapes.hpp"

#include "SpriteUi/Elements/ElementTree.hpp"
#include "SpriteUi/Elements/TextField.hpp"
#include "SpriteUi/Rendering/DrawList.hpp"
#include "SpriteUi/Text/TextLayout.hpp"
#include "SpriteUi/Text/TruetypeAtlas.hpp"
#include "SpriteUi/Widgets/TextEdit.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace editor::script_window {

namespace SU = ToolEngine::SpriteUi;
namespace Elm = ToolEngine::SpriteUi::Elements;
namespace ST = ToolEngine::SpriteUi::Text;
namespace SR = ToolEngine::SpriteUi::Rendering;
namespace Wd = ToolEngine::SpriteUi::Widgets;

class Panel {
 public:
  static constexpr float kFont = 12.f;
  static constexpr float kWinW = 760.f, kWinH = 640.f;   // tamanho inicial
  static constexpr float kPad = 8.f;
  static constexpr float kBtnH = 22.f;
  static constexpr float kCompleteH = 96.f;               // lista de sugestoes
  static constexpr float kRow = 14.f;                     // linha de erro e de sugestao
  static constexpr float kEditorMinH = 120.f;

  Panel(const sui::TextAtlasSet& text, sui::SpriteUiSdlRenderer& sdl) : text_{text}, sdl_{sdl}, chrome_{text, sdl} {
    root_ = tree_.Create<Elm::Element>(SU::InvalidUIHandle);
    handle_ = tree_.Create<Elm::TextField>(root_, Elm::TextFieldOptions{.multiline = true});
    field_ = tree_.GetAs<Elm::TextField>(handle_);
    root_transform_fill(root_);
    field_->transform.anchorMin = {0.0f, 0.0f};
    field_->transform.anchorMax = {1.0f, 1.0f};
    field_->transform.offsetMin = {0.0f, 0.0f};
    field_->transform.offsetMax = {0.0f, 0.0f};
    field_->padding = {4.0f, 8.0f, 4.0f, 8.0f};
    field_->fontSize = kFont;
    field_->Edit() = Wd::TextEditState(std::string(), true);   // a area aceita quebra de linha
    chrome_.setDock(std::nullopt);                              // janela flutuante
    style_.background = {0.07f, 0.09f, 0.13f, 1.0f};
    style_.focusBackground = {0.09f, 0.12f, 0.17f, 1.0f};
    style_.text = {0.92f, 0.93f, 0.94f, 1.0f};
    style_.caret = {0.95f, 0.95f, 1.0f, 1.0f};
    style_.selection = {0.20f, 0.30f, 0.50f, 0.60f};
    paintList_.owner = sui::handleFor(0);
  }
  Panel(const Panel&) = delete;
  Panel& operator=(const Panel&) = delete;

  [[nodiscard]] bool isOpen() const { return open_; }

  // Abre a janela para o objeto selecionado (cria o arquivo do script se ainda nao existe).
  void open(Editor& ed) {
    auto& w = ed.scene.world();
    auto* sc = w.get<ScriptComp>(ed.selected);
    if (!sc) return;
    if (!sc->path[0]) {
      const std::string rel = createScriptFile(ed.project, w.get<Name>(ed.selected)->text);
      if (!rel.empty()) ser::copyStr(sc->path, sizeof sc->path, rel);
    }
    target_ = ed.selected;
    name_ = w.get<Name>(ed.selected)->text;
    path_ = sc->path;
    msg_.clear();
    std::string text;
    if (!readScript(ed.project, path_, text)) msg_ = "Could not read the script file.";
    setText(text, 0);
    issues_ = checkScriptSource(text, path_);
    dirty_ = checkPending_ = showAll_ = snippets_ = false;
    open_ = true;
  }

  void close() {
    open_ = false;
    focused_ = false;
    dragging_ = false;
    if (window_) SDL_StopTextInput(window_);
    chrome_.hide();
  }

  // Eventos: moldura primeiro; depois botoes, lista de exemplos e sugestoes, campo de texto e teclado.
  // Devolve true quando o painel consumiu o evento.
  bool handleEvent(const SDL_Event& e, Editor& ed) {
    if (!open_) return false;
    if (chrome_.handleEvent(e)) return true;
    switch (e.type) {
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e.button.button != SDL_BUTTON_LEFT) return false;
        return onPointerDown(e.button.x, e.button.y, ed);
      case SDL_EVENT_MOUSE_BUTTON_UP:
        dragging_ = false;
        field_->PointerUp();
        return false;
      case SDL_EVENT_MOUSE_MOTION:
        if (dragging_ && focused_) {
          placeCaretAt(e.motion.x, e.motion.y, true);
          return true;
        }
        return false;
      case SDL_EVENT_MOUSE_WHEEL:
        if (inside(editorBox_, e.wheel.mouse_x, e.wheel.mouse_y)) {
          field_->Wheel(-e.wheel.y * kFont * 3.f);
          return true;
        }
        return false;
      case SDL_EVENT_TEXT_INPUT:
        if (!focused_ || !e.text.text) return false;
        edit([&](Wd::TextEditState& s) { s.InsertText(e.text.text); });
        return true;
      case SDL_EVENT_KEY_DOWN:
        if (!focused_) return false;
        return onKey(e.key.key, e.key.mod, ed);
      default:
        return false;
    }
  }

  // Desenha a janela (se aberta). `now` em segundos (relogio monotonico), para a verificacao com atraso.
  bool render(SDL_Renderer* renderer, SDL_Window* window, Editor& ed, double now) {
    if (!open_) return true;
    if (!renderer || !window) return false;
    window_ = window;
    int winW = 0, winH = 0;
    SDL_GetWindowSize(window, &winW, &winH);
    if (winW <= 0 || winH <= 0) return true;

    const sui::Box body = chrome_.prepare(static_cast<float>(winW), static_cast<float>(winH), kWinW, kWinH);
    if (chrome_.consumeClose()) { close(); return true; }
    chrome_.consumeHelp();
    if (body.h <= 0.f) return chrome_.draw(renderer, title());

    if (checkPending_ && now - dirtyAt_ >= 0.4) {
      issues_ = checkScriptSource(field_->Edit().Text(), path_);
      checkPending_ = false;
    }
    const bool alive = ed.scene.world().alive(target_) && ed.scene.world().has<ScriptComp>(target_);
    if (!chrome_.draw(renderer, title())) return false;
    if (!alive) return drawMessage(renderer, body, "The object of this script was removed or has no script any more.");

    layoutAll(body);
    buildBody(ed, body);
    return drawBody(renderer);
  }

 private:
  // ---------- Layout ----------
  struct Layout {
    SU::Rect save, complete, snippets, close;
    SU::Rect editor;
    std::vector<SU::Rect> snippetRows, completeRows;
    bool showComplete = false;
    bool showSnippets = false;
  };

  // A raiz ocupa a area que o Arrange recebe (preenchimento total).
  void root_transform_fill(SU::UIHandle root) {
    Elm::Element* r = tree_.Get(root);
    r->transform.anchorMin = {0.0f, 0.0f};
    r->transform.anchorMax = {1.0f, 1.0f};
    r->transform.offsetMin = {0.0f, 0.0f};
    r->transform.offsetMax = {0.0f, 0.0f};
  }

  static bool inside(const SU::Rect& r, float x, float y) {
    return x >= r.position.x && x < r.position.x + r.size.x && y >= r.position.y && y < r.position.y + r.size.y;
  }
  static SU::Rect rect(float x, float y, float w, float h) { return SU::Rect{{x, y}, {w, h}}; }

  std::string title() const {
    return std::string("Script: ") + (name_.empty() ? "?" : name_) + (dirty_ ? " *" : "");
  }

  ST::TextLayoutResult layoutPlain(const std::string& utf8, float size) const {
    const ST::TruetypeAtlas& atlas = text_.atlasFor(size);
    ST::TextLayoutConfig cfg;
    cfg.fontSize = size;
    return ST::LayoutText(utf8, ST::ShapeTruetype(utf8, atlas), atlas.metrics, cfg);
  }
  float textWidth(const std::string& s) const { return layoutPlain(s, kFont).size.x; }

  // Layout do texto do campo com a largura atual (depois de cada edicao, para que cliques e setas usem o texto novo).
  ST::TextLayoutResult fieldLayout() const {
    const std::string& t = field_->Edit().Text();
    const ST::TruetypeAtlas& atlas = text_.atlasFor(kFont);
    return ST::LayoutText(t, ST::ShapeTruetype(t, atlas), atlas.metrics,
                          Elm::MakeTextFieldLayoutConfig(*field_, contentSize(), style_));
  }
  SU::Vec2 contentSize() const { return field_->ContentRect().size; }

  void layoutAll(const sui::Box& b) {
    layout_ = Layout{};
    const float top = b.y + kPad;
    float x = b.x + kPad;
    layout_.save = rect(x, top, 56.f, kBtnH); x += 56.f + 6.f;
    layout_.complete = rect(x, top, textWidth(T("Complete (Ctrl+Space)")) + 16.f, kBtnH);
    x += layout_.complete.size.x + 6.f;
    layout_.snippets = rect(x, top, textWidth(T("Snippets")) + 16.f, kBtnH);
    layout_.close = rect(b.x + b.w - kPad - 56.f, top, 56.f, kBtnH);

    const float editorTop = top + kBtnH + 8.f;
    layout_.showComplete = showComplete();
    const float reserve = (layout_.showComplete ? kCompleteH + 6.f : 0.f) + kRow + 6.f + 40.f;
    const float editorH = std::max(kEditorMinH, b.y + b.h - kPad - editorTop - reserve);
    layout_.editor = rect(b.x + kPad, editorTop, b.w - 2.f * kPad, editorH);
    layout_.showSnippets = snippets_;
    if (layout_.showSnippets) {
      const std::vector<Snippet> list = scriptSnippets();
      const float rowsTop = top + kBtnH + 2.f;
      for (std::size_t i = 0; i < list.size() && i < 20; ++i) {
        layout_.snippetRows.push_back(rect(layout_.snippets.position.x, rowsTop + static_cast<float>(i) * kRow * 1.4f,
                                           420.f, kRow * 1.4f));
      }
    }
    if (layout_.showComplete) {
      const float y = layout_.editor.position.y + layout_.editor.size.y + 6.f;
      for (std::size_t i = 0; i < completions_.size(); ++i) {
        layout_.completeRows.push_back(rect(layout_.editor.position.x, y + 4.f + static_cast<float>(i) * kRow,
                                            layout_.editor.size.x, kRow));
      }
    }
    // Area de contorno do campo, usada pelos eventos de ponteiro (campo + bordas).
    editorBox_ = layout_.editor;
    // Ajusta o campo ao retangulo da area do editor.
    tree_.Arrange(root_, {{layout_.editor.position.x, layout_.editor.position.y}, {layout_.editor.size.x, layout_.editor.size.y}});
  }

  bool showComplete() const { return showAll_ || completionWord(field_->Edit().Text(), field_->Edit().Caret()).size() >= 2; }

  // ---------- Pintura ----------
  void buildBody(Editor& ed, const sui::Box& b) {
    view::ShapeList shapes;
    const float top = b.y + kPad;
    // Botoes.
    button(shapes, layout_.save, T("Save"));
    button(shapes, layout_.complete, T("Complete (Ctrl+Space)"), showAll_);
    button(shapes, layout_.snippets, T("Snippets"), snippets_);
    button(shapes, layout_.close, T("Close"));
    float statusX = layout_.snippets.position.x + layout_.snippets.size.x + 12.f;
    const float statusEnd = layout_.close.position.x - 10.f;
    if (dirty_) { shapes.text({statusX, top + 4.f}, T("(unsaved)"), kFont, view::Rgba(140, 150, 170)); statusX += 70.f; }
    if (!msg_.empty() && statusX < statusEnd) shapes.text({statusX, top + 4.f}, msg_, kFont, view::Rgba(140, 150, 170), false, statusEnd - statusX);
    // Fundo do editor e o texto (painel proprio, mesma cor do campo do Inspector).
    shapes.rectFilled({layout_.editor.position.x, layout_.editor.position.y},
                      {layout_.editor.position.x + layout_.editor.size.x, layout_.editor.position.y + layout_.editor.size.y},
                      view::Rgba(18, 23, 34), 0.f);
    paintBody_.append(shapes, 0u, paintList_);
    buildFieldItems();

    // Sugestoes do autocomplete.
    view::ShapeList lists;
    if (layout_.showComplete) {
      const float y = layout_.editor.position.y + layout_.editor.size.y + 6.f;
      lists.rectFilled({layout_.editor.position.x, y}, {layout_.editor.position.x + layout_.editor.size.x, y + kCompleteH},
                       view::Rgba(22, 28, 40), 0.f);
      if (completions_.empty()) lists.text({layout_.editor.position.x + 6.f, y + 4.f}, T("No suggestions."), kFont, view::Rgba(140, 150, 170));
      for (std::size_t i = 0; i < completions_.size() && i < layout_.completeRows.size(); ++i) {
        const SU::Rect& r = layout_.completeRows[i];
        const ApiItem& it = completions_[i];
        lists.text({r.position.x + 6.f, r.position.y}, it.name + "   (" + it.kind + ")   " + it.sig, kFont, view::Rgba(220, 224, 232));
      }
    }
    // Verificador: linha de resumo e lista de erros (com a linha de cada um).
    float y = (layout_.showComplete ? layout_.editor.position.y + layout_.editor.size.y + kCompleteH + 12.f
                                    : layout_.editor.position.y + layout_.editor.size.y + 6.f);
    bool anyError = false;
    for (const ScriptIssue& is : issues_) if (is.sev == tool::Severity::Error) anyError = true;
    if (issues_.empty()) {
      lists.text({b.x + kPad, y}, T("Check: OK (no problems found)"), kFont, view::Rgba(130, 255, 150));
    } else {
      lists.text({b.x + kPad, y}, "Check: " + issueSummary(issues_), kFont,
                 anyError ? view::Rgba(255, 102, 102) : view::Rgba(255, 217, 77));
    }
    y += kRow + 6.f;
    const float listBottom = b.y + b.h - kPad;
    lists.rectFilled({b.x + kPad, y}, {b.x + b.w - kPad, listBottom}, view::Rgba(22, 28, 40), 0.f);
    const std::vector<std::string> src = splitLines(field_->Edit().Text());
    float ly = y + 4.f;
    for (std::size_t i = 0; i < issues_.size() && i < 60; ++i) {
      if (ly + kRow > listBottom) break;
      const ScriptIssue& is = issues_[i];
      const view::Color c = is.sev == tool::Severity::Error ? view::Rgba(255, 102, 102) : view::Rgba(255, 217, 77);
      lists.text({b.x + kPad + 6.f, ly}, "line " + std::to_string(is.line) + "  " + is.code + "  " + is.msg, kFont, c);
      ly += kRow;
      if (is.line >= 1 && static_cast<std::size_t>(is.line) <= src.size() && ly + kRow <= listBottom) {
        lists.text({b.x + kPad + 26.f, ly}, src[static_cast<std::size_t>(is.line) - 1], kFont, view::Rgba(120, 130, 150));
        ly += kRow;
      }
    }
    if (issues_.size() > 60) lists.text({b.x + kPad + 6.f, ly}, "... " + std::to_string(issues_.size() - 60) + " more (see Console: Check scripts)", kFont, view::Rgba(120, 130, 150));
    paintBody_.append(lists, kClipFrame, paintList_);

    // Lista de exemplos (Snippets), por cima de tudo do corpo.
    if (layout_.showSnippets) {
      view::ShapeList drop;
      const std::vector<Snippet> list = scriptSnippets();
      if (!layout_.snippetRows.empty()) {
        const float h = layout_.snippetRows.back().position.y + layout_.snippetRows.back().size.y - layout_.snippetRows.front().position.y;
        drop.rectFilled({layout_.snippetRows.front().position.x, layout_.snippetRows.front().position.y},
                        {layout_.snippetRows.front().position.x + 420.f, layout_.snippetRows.front().position.y + h},
                        view::Rgba(30, 38, 56), 0.f);
      }
      for (std::size_t i = 0; i < layout_.snippetRows.size() && i < list.size(); ++i) {
        const SU::Rect& r = layout_.snippetRows[i];
        drop.text({r.position.x + 6.f, r.position.y + 2.f}, std::to_string(list[i].n) + ". " + list[i].title, kFont, view::Rgba(230, 232, 238));
      }
      paintBody_.append(drop, kClipFrame, paintList_);
    }
  }

  void button(view::ShapeList& shapes, const SU::Rect& r, const std::string& label, bool on = false) {
    const view::Color fill = on ? view::Rgba(74, 130, 200) : view::Rgba(42, 64, 104);
    shapes.rectFilled({r.position.x, r.position.y}, {r.position.x + r.size.x, r.position.y + r.size.y}, fill, 3.f);
    shapes.text({r.position.x + r.size.x * 0.5f, r.position.y + (r.size.y - kFont) * 0.5f - 1.f}, label, kFont,
                view::Rgba(233, 234, 238), true);
  }

  // Itens do campo (fundo do texto ja pintado; aqui texto, selecao e caret) com o recorte da area do editor.
  void buildFieldItems() {
    const ST::TextLayoutResult layout = fieldLayout();
    const ST::TextLayoutResult placeholder = layoutPlain("", kFont);
    Elm::TextTarget target;
    target.textureSlice = text_.sliceFor(kFont);
    target.paintOrder = paintList_.paint;
    const SU::Rect content = field_->ContentRect();
    const auto items = Elm::BuildTextFieldItems(*field_, style_, layout, placeholder, content, target, kClipEditor);
    paintList_.items.insert(paintList_.items.end(), items.begin(), items.end());
    paintList_.paint += items.size() + 1;
  }

  bool drawMessage(SDL_Renderer* renderer, const sui::Box& b, const char* text) {
    view::ShapeList shapes;
    shapes.text({b.x + kPad, b.y + kPad}, T(text), kFont, view::Rgba(255, 153, 102));
    paintBody_.append(shapes, kClipFrame, paintList_);
    return drawBody(renderer);
  }

  bool drawBody(SDL_Renderer* renderer) {
    const SU::Rect f = chrome_.frameRect();
    clips_.clear();
    clips_.push_back({f.position.x, f.position.y, f.position.x + f.size.x, f.position.y + f.size.y});
    clips_.push_back({layout_.editor.position.x, layout_.editor.position.y,
                      layout_.editor.position.x + layout_.editor.size.x, layout_.editor.position.y + layout_.editor.size.y});
    const bool ok = view::drawPaintList(renderer, sdl_, extractor_, packet_, frameId_, clips_, paintList_);
    paintList_ = view::PaintList{};
    paintList_.owner = sui::handleFor(0);
    return ok;
  }

  // ---------- Eventos de ponteiro ----------
  bool onPointerDown(float x, float y, Editor& ed) {
    if (!inside(chrome_.frameRect(), x, y)) {
      focus(false);
      return false;
    }
    if (inside(layout_.save, x, y)) { save(ed); return true; }
    if (inside(layout_.close, x, y)) { close(); return true; }
    if (inside(layout_.complete, x, y)) { showAll_ = !showAll_; return true; }
    if (inside(layout_.snippets, x, y)) { snippets_ = !snippets_; return true; }
    if (layout_.showSnippets) {
      const std::vector<Snippet> list = scriptSnippets();
      for (std::size_t i = 0; i < layout_.snippetRows.size() && i < list.size(); ++i) {
        if (inside(layout_.snippetRows[i], x, y)) {
          const TextEdit te = insertAt(field_->Edit().Text(), field_->Edit().Caret(), list[i].code + "\n");
          setText(te.text, te.cursor);
          edited();
          snippets_ = false;
          return true;
        }
      }
      snippets_ = false;   // clique fora da lista fecha
    }
    if (layout_.showComplete) {
      for (std::size_t i = 0; i < layout_.completeRows.size() && i < completions_.size(); ++i) {
        if (inside(layout_.completeRows[i], x, y)) {
          const TextEdit te = applyCompletion(field_->Edit().Text(), field_->Edit().Caret(), completions_[i].ins);
          setText(te.text, te.cursor);
          showAll_ = false;
          edited();
          return true;
        }
      }
    }
    if (inside(layout_.editor, x, y)) {
      focus(true);
      placeCaretAt(x, y, false);
      dragging_ = true;
      return true;
    }
    return true;
  }

  void placeCaretAt(float x, float y, bool extend) {
    const SU::Rect c = field_->ContentRect();
    const ST::TextLayoutResult layout = fieldLayout();
    field_->PointerDown({x - c.position.x, y - c.position.y}, c.size, layout, style_, extend);
  }

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
  bool onKey(SDL_Keycode key, SDL_Keymod mod, Editor& ed) {
    const bool ctrl = (mod & SDL_KMOD_CTRL) != 0;
    const bool shift = (mod & SDL_KMOD_SHIFT) != 0;
    if (ctrl) {
      switch (key) {
        case SDLK_S: save(ed); return true;
        case SDLK_SPACE: showAll_ = !showAll_; return true;
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
      case SDLK_ESCAPE: showAll_ = snippets_ = false; return true;
      default: return false;
    }
  }

  void moveVertical(int dir, bool shift) {
    const ST::TextLayoutResult layout = fieldLayout();
    if (dir < 0) field_->MoveUp(layout, shift); else field_->MoveDown(layout, shift);
    field_->KeepCaretVisible(fieldLayout(), contentSize(), style_);
  }

  // Aplica uma edicao do texto (sem mudar o caret por ela). Marca como alterado.
  template <class F>
  void edit(F&& fn, bool changes = true) {
    const std::string before = field_->Edit().Text();
    fn(field_->Edit());
    if (changes && before != field_->Edit().Text()) edited();
    else field_->KeepCaretVisible(fieldLayout(), contentSize(), style_);
  }

  void edited() {
    dirty_ = true;
    checkPending_ = true;
    dirtyAt_ = SDL_GetTicks() / 1000.0;
    field_->KeepCaretVisible(fieldLayout(), contentSize(), style_);
    completions_ = showComplete() ? completionsFor(completionWord(field_->Edit().Text(), field_->Edit().Caret()), showAll_) : std::vector<ApiItem>{};
  }

  void setText(const std::string& text, std::size_t cursor) {
    field_->Edit() = Wd::TextEditState(text, true);
    field_->Edit().MoveCaretTo(cursor);
    completions_.clear();
  }

  void save(Editor& ed) {
    const std::string& text = field_->Edit().Text();
    msg_ = writeScript(ed.project, path_, text) ? "Script saved." : "Could not save the script.";
    dirty_ = false;
    issues_ = checkScriptSource(text, path_);
    checkPending_ = false;
    const std::string who = ed.scene.world().alive(target_) ? ed.scene.world().get<Name>(target_)->text : name_;
    for (const ScriptIssue& is : issues_) {
      tool::debug().report(is.code.c_str(), is.sev, "[" + who + "] line " + std::to_string(is.line) + ": " + is.msg, "", is.line);
    }
    tool::logInfo("[" + who + "] saved: " + issueSummary(issues_));
  }

  static std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> out;
    for (std::size_t p = 0; p <= s.size();) {
      std::size_t e = s.find('\n', p);
      if (e == std::string::npos) e = s.size();
      out.push_back(s.substr(p, e - p));
      p = e + 1;
    }
    return out;
  }

  static constexpr std::uint32_t kClipFrame = 0u;
  static constexpr std::uint32_t kClipEditor = 1u;

  const sui::TextAtlasSet& text_;
  sui::SpriteUiSdlRenderer& sdl_;
  framed::Chrome chrome_;
  Elm::ElementTree tree_;
  SU::UIHandle root_{}, handle_{};
  Elm::TextField* field_ = nullptr;
  Elm::TextFieldStyle style_;
  Layout layout_;
  SU::Rect editorBox_{};
  view::ShapePainter paintBody_{text_};
  view::PaintList paintList_;
  std::vector<SR::ClipRectGPU> clips_;
  SR::RenderExtractor extractor_;
  SR::FramePacket packet_;
  std::uint64_t frameId_ = 0;

  bool open_ = false, focused_ = false, dragging_ = false;
  bool dirty_ = false, checkPending_ = false, showAll_ = false, snippets_ = false;
  double dirtyAt_ = -10.0;
  tool::Entity target_{};
  std::string name_, path_, msg_;
  std::vector<ScriptIssue> issues_;
  std::vector<ApiItem> completions_;
  SDL_Window* window_ = nullptr;
};

}  // namespace editor::script_window
