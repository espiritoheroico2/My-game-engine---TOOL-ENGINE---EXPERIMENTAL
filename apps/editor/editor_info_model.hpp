#pragma once

// Janela "Editor info" (SpriteUI): modelo SEM desenho. Regras:
//  - Um rascunho com um campo por informacao; nada vai para o projeto antes de "Save info" (applyTo).
//  - Campos obrigatorios faltando vem de ProjectSettings::missingInfo() (mesmas chaves em ingles, use T()).
//  - Copyright vazio nao e obrigatorio: o pacote gera "Copyright (c) <ano> <estudio>".
//  - Limites de tamanho em bytes (UTF-8).
// Testado sem janela em apps/editor/tests/test_editor_info_model.cpp.
#include "SpriteUi/Widgets/TextEdit.hpp"
#include "project_settings.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace editor::info {

namespace Edit = ToolEngine::SpriteUi::Widgets;

// Ordem do formulario (Jogo; Quem fez; ...). Tab percorre nesta ordem.
enum class Field : std::size_t {
  Product = 0,
  Version,
  Description,
  Creator,
  Studio,
  Country,
  City,
  Website,
  Copyright,
  Count
};
inline constexpr std::size_t kFieldCount = static_cast<std::size_t>(Field::Count);

// Limite em bytes de cada campo.
[[nodiscard]] constexpr std::size_t maxBytes(Field f) {
  switch (f) {
    case Field::Product: return 100;
    case Field::Version: return 32;
    case Field::Description: return 400;
    case Field::Creator: return 100;
    case Field::Studio: return 100;
    case Field::Country: return 100;
    case Field::City: return 100;
    case Field::Website: return 200;
    case Field::Copyright: return 200;
    case Field::Count: return 0;
  }
  return 0;
}

enum class Key { Left, Right, Home, End, Backspace, Delete, Tab, Escape };

class Model {
 public:
  explicit Model(const ProjectSettings& s) {
    set(Field::Product, s.projectName);
    set(Field::Version, s.info.version);
    set(Field::Description, s.info.description);
    set(Field::Creator, s.author);
    set(Field::Studio, s.info.studio);
    set(Field::Country, s.info.country);
    set(Field::City, s.info.city);
    set(Field::Website, s.info.website);
    set(Field::Copyright, s.info.copyright);
    focus_ = Field::Product;
    focused_ = false;
  }

  // Estado para desenho e testes.
  [[nodiscard]] const Edit::TextEditState& field(Field f) const { return fields_[idx(f)]; }
  [[nodiscard]] bool hasFocus(Field f) const { return focused_ && focus_ == f; }
  [[nodiscard]] bool focused() const { return focused_; }
  [[nodiscard]] Field focusField() const { return focus_; }
  [[nodiscard]] const std::string& clipboardOut() const { return clipboardOut_; }
  [[nodiscard]] const std::string& status() const { return status_; }   // mensagem depois de salvar (use T())

  // Foco: um campo por vez. Clicar num campo chama Focus.
  void Focus(Field f) {
    focus_ = f;
    focused_ = true;
  }
  void Blur() { focused_ = false; }

  // Texto. Respeita o limite do campo: texto que passaria do limite e ignorado. Conservador: conta o texto
  // selecionado tambem, entao para trocar um valor perto do limite, apague a selecao antes (Backspace).
  void InsertText(std::string_view utf8) {
    if (!focused_) return;
    if (current().Text().size() + utf8.size() > maxBytes(focus_)) return;
    current().InsertText(utf8);
  }
  void Paste(std::string_view utf8) { InsertText(utf8); }
  void SelectAll() {
    if (focused_) current().SelectAll();
  }
  void Copy() {
    if (focused_) clipboardOut_ = current().Copy();
  }
  void Cut() {
    if (focused_) clipboardOut_ = current().Cut();
  }
  void Undo() {
    if (focused_) (void)current().Undo();
  }
  void SetCaret(std::size_t byteOffset, bool extend) {
    if (focused_) current().MoveCaretTo(byteOffset, extend);
  }

  void Press(Key key, bool shift) {
    switch (key) {
      case Key::Left: if (focused_) current().MoveLeft(shift); break;
      case Key::Right: if (focused_) current().MoveRight(shift); break;
      case Key::Home: if (focused_) current().MoveHome(shift); break;
      case Key::End: if (focused_) current().MoveEnd(shift); break;
      case Key::Backspace: if (focused_) (void)current().Backspace(); break;
      case Key::Delete: if (focused_) (void)current().Delete(); break;
      case Key::Tab: NextField(shift ? -1 : 1); break;
      case Key::Escape: Blur(); break;
    }
  }

  // Rascunho como ProjectSettings, so para validar (nao grava nada).
  [[nodiscard]] ProjectSettings draft() const {
    ProjectSettings d;
    d.projectName = text(Field::Product);
    d.author = text(Field::Creator);
    d.info.studio = text(Field::Studio);
    d.info.country = text(Field::Country);
    d.info.version = text(Field::Version);
    d.info.description = text(Field::Description);
    return d;
  }

  // Campos obrigatorios que faltam (chaves em ingles; o host traduz com T()).
  [[nodiscard]] std::vector<std::string> missing() const { return draft().missingInfo(); }

  // Copia o rascunho para o projeto. Chamado so por "Save info".
  void applyTo(ProjectSettings& s) const {
    s.projectName = text(Field::Product);
    s.author = text(Field::Creator);
    s.info.studio = text(Field::Studio);
    s.info.country = text(Field::Country);
    s.info.city = text(Field::City);
    s.info.version = text(Field::Version);
    s.info.description = text(Field::Description);
    s.info.copyright = text(Field::Copyright);
    s.info.website = text(Field::Website);
  }

  // Resultado do salvar, escrito pelo host (true = gravou project.json).
  void setSaveResult(bool ok, std::string message) {
    status_ = std::move(message);
    saved_ = ok;
  }
  [[nodiscard]] bool saved() const { return saved_; }

  // Copyright que o pacote vai usar: o digitado, ou o automatico com o ano dado.
  [[nodiscard]] std::string copyrightText(int year) const {
    const std::string c = text(Field::Copyright);
    if (!c.empty()) return c;
    return "Copyright (c) " + std::to_string(year) + " " + text(Field::Studio);
  }

  [[nodiscard]] std::string text(Field f) const { return fields_[idx(f)].Text(); }

 private:
  static constexpr std::size_t idx(Field f) { return static_cast<std::size_t>(f); }
  Edit::TextEditState& current() { return fields_[idx(focus_)]; }

  void set(Field f, const std::string& value) { fields_[idx(f)] = Edit::TextEditState{value}; }

  void NextField(int step) {
    const int n = static_cast<int>(kFieldCount);
    int i = static_cast<int>(idx(focus_));
    i = (i + step % n + n) % n;
    Focus(static_cast<Field>(i));
  }

  std::array<Edit::TextEditState, kFieldCount> fields_{};
  Field focus_ = Field::Product;
  bool focused_ = false;
  std::string clipboardOut_;
  std::string status_;
  bool saved_ = false;
};

}  // namespace editor::info
